#include "CastleMassEnemyProjectileProcessor.h"

#include "CastleMainTowerComponent.h"
#include "CastleMassEnemyProjectileFragments.h"
#include "CastleMassFragments.h"
#include "CastleMassProcessors.h"
#include "Enemies/CastleMassEnemyRangedAttackProcessor.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"
#include "Subsystems/CastleShieldTowerSubsystem.h"

UCastleMassEnemyProjectileProcessor::UCastleMassEnemyProjectileProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
	ExecutionOrder.ExecuteAfter.Add(UCastleMassMovementProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteAfter.Add(UCastleMassEnemyRangedAttackProcessor::StaticClass()->GetFName());
	bRequiresGameThreadExecution = true;
}

void UCastleMassEnemyProjectileProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassProjectileTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FCastleMassEnemyProjectileFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassProjectileFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddConstSharedRequirement<FCastleMassMovementParameters>();
}

void UCastleMassEnemyProjectileProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const float DeltaTime = Context.GetDeltaTimeSeconds();
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("敌方追踪弹帧时间无效：世界 %s。"), *GetNameSafe(GetWorld()));
		return;
	}
	UCastleShieldTowerSubsystem* Shields = GetWorld()->GetSubsystem<UCastleShieldTowerSubsystem>();
	UCastleMainTowerSubsystem* Main = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>();
	Query.ForEachEntityChunk(Context, [Shields, Main, DeltaTime](FMassExecutionContext& Chunk)
	{
		const auto Launches = Chunk.GetFragmentView<FCastleMassEnemyProjectileFragment>();
		const auto Projectiles = Chunk.GetMutableFragmentView<FCastleMassProjectileFragment>();
		const auto Units = Chunk.GetMutableFragmentView<FCastleMassUnitFragment>();
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Radii = Chunk.GetFragmentView<FAgentRadiusFragment>();
		const float Speed = Chunk.GetConstSharedFragment<FCastleMassMovementParameters>().Settings.MaxSpeed;
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (Units[It].Health <= 0.0f) { continue; }
			const auto& Launch = Launches[It].Launch;
			FVector TargetLocation;
			if (Launch.ShieldId != 0)
			{
				if (!Shields->GetActiveShieldLocation(Launch.ShieldId, TargetLocation))
				{
					// D18：护盾在途中破灭只回收这一发，不穿透、不重新锁定其他目标。
					UE_LOG(LogMoving_Castle, Warning, TEXT("敌方追踪弹 %d：锁定护盾 %llu 已提前消失，本发回收且不改打其他目标。"), Chunk.GetEntity(It).Index, Launch.ShieldId);
					Units[It].Health = 0.0f;
					Velocities[It].Value = FVector::ZeroVector;
					continue;
				}
			}
			else if (Launch.TargetTower.IsValid() && Launch.TargetTower->IsAlive()
				&& Main->GetRegisteredMainTower() == Launch.TargetTower.Get())
			{
				TargetLocation = Launch.TargetTower->GetTargetLocation();
			}
			else
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("敌方追踪弹 %d 的主塔目标 %s 无效或已死亡，命中失败。"), Chunk.GetEntity(It).Index, *GetPathNameSafe(Launch.TargetTower.Get()));
				Units[It].Health = 0.0f;
				Velocities[It].Value = FVector::ZeroVector;
				continue;
			}
			FTransform& Transform = Transforms[It].GetMutableTransform();
			auto& Projectile = Projectiles[It];
			if (TargetLocation.ContainsNaN() || Transform.ContainsNaN() || !FMath::IsFinite(Speed) || Speed <= 0.0f
				|| !FMath::IsFinite(Radii[It].Radius) || Radii[It].Radius < 0.0f
				|| !FMath::IsFinite(Projectile.Damage) || Projectile.Damage < 0.0f)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("敌方追踪弹 %d 的位置、速度、半径或伤害无效，命中失败。"), Chunk.GetEntity(It).Index);
				Units[It].Health = 0.0f;
				Velocities[It].Value = FVector::ZeroVector;
				continue;
			}
			Projectile.PreviousLocation = Transform.GetLocation();
			const FVector ToTarget = TargetLocation - Projectile.PreviousLocation;
			const double Distance = ToTarget.Size();
			Velocities[It].Value = Distance > 0.0 ? ToTarget * (Speed / Distance) : FVector::ZeroVector;
			Transform.AddToTranslation(Velocities[It].Value * DeltaTime);
			if (Distance > 0.0) { Transform.SetRotation(ToTarget.ToOrientationQuat()); }
			// 建筑契约提供一个目标点，按弹体半径扫掠，避免大时间步穿过该点。
			if (FMath::PointDistToSegmentSquared(TargetLocation, Projectile.PreviousLocation, Transform.GetLocation()) <= FMath::Square(static_cast<double>(Radii[It].Radius)))
			{
				int64 Remaining = 0;
				const bool bApplied = Launch.ShieldId != 0 ? Shields->ApplyAttackToShield(Launch.ShieldId, Launch.Attacker, Remaining)
					: Main->ApplyMainTowerDamage(Projectile.Damage);
				if (!bApplied)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("敌方追踪弹 %d 命中结算失败，护盾 %llu，主塔 %s。"), Chunk.GetEntity(It).Index, Launch.ShieldId, *GetPathNameSafe(Launch.TargetTower.Get()));
				}
				Units[It].Health = 0.0f;
				Velocities[It].Value = FVector::ZeroVector;
			}
		}
	});
}
