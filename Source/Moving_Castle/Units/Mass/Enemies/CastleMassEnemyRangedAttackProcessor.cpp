#include "CastleMassEnemyRangedAttackProcessor.h"
#include "Audio/CastleAudioSubsystem.h"

#include "CastleMainTowerComponent.h"
#include "CastleMassEnemyRangedAttackFragments.h"
#include "CastleMassFragments.h"
#include "CastleMassDeathSplitFragments.h"
#include "CastleMassProcessors.h"
#include "CastleMassUnitSubsystem.h"
#include "Enemies/Flight/CastleMassFlightTargeting.h"
#include "Engine/World.h"
#include "MassCommands.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassEntityConfigAsset.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"
#include "Subsystems/CastleShieldTowerSubsystem.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"

UCastleMassEnemyRangedAttackProcessor::UCastleMassEnemyRangedAttackProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
	ExecutionOrder.ExecuteAfter.Add(UCastleMassMovementProcessor::StaticClass()->GetFName());
	bRequiresGameThreadExecution = true;
}

void UCastleMassEnemyRangedAttackProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassSpawnBurstFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassFlightIntentFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassEnemyRangedAttackFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassAttackAnimationFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
}

void UCastleMassEnemyRangedAttackProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UCastleShieldTowerSubsystem* Shields = GetWorld()->GetSubsystem<UCastleShieldTowerSubsystem>();
	UCastleMainTowerSubsystem* Main = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>();
	UCastleSupportTowerSubsystem* Support = GetWorld()->GetSubsystem<UCastleSupportTowerSubsystem>();
	if (!Support)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 远程索敌失败：世界 %s 缺少支援塔子系统。"), *GetPathNameSafe(GetWorld()));
		return;
	}
	bool bAnyMissing = false;
	Query.ForEachEntityChunk(Context, [this, Shields, Main, Support, &bAnyMissing](FMassExecutionContext& Chunk)
	{
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
		const auto FlightIntents = Chunk.GetMutableFragmentView<FCastleMassFlightIntentFragment>();
		const auto Radii = Chunk.GetFragmentView<FAgentRadiusFragment>();
		const auto Attacks = Chunk.GetMutableFragmentView<FCastleMassEnemyRangedAttackFragment>();
		const auto Animations = Chunk.GetMutableFragmentView<FCastleMassAttackAnimationFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (Units[It].Health <= 0.0f) { continue; }
			auto& Attack = Attacks[It];
			FTransform& Transform = Transforms[It].GetMutableTransform();
			const FVector Origin = Transform.GetLocation();
			FVector TargetLocation;
			// 仇恨距离和武器射程相互独立：获选气球即使尚未进入射程也会被追踪。
			uint64 BalloonId = 0;
			const bool bBalloonTarget = Support->FindBalloonTarget(Origin, BalloonId, TargetLocation);
			uint64 ShieldId = 0;
			const bool bShieldTarget = !bBalloonTarget && Shields->FindNearestActiveShield(Origin, Attack.AttackRange, false, ShieldId, TargetLocation);
			const bool bTowerTarget = !bBalloonTarget && !bShieldTarget && Main
				&& Main->GetMainTowerTargetLocation(TargetLocation);
			if (!bBalloonTarget && !bShieldTarget && !bTowerTarget)
			{
				bAnyMissing = true;
				if (!bLoggedMissingTargets)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("Mass 远程处理器：世界 %s 没有射程内激活护盾或存活主塔，停止移动。"), *GetNameSafe(GetWorld()));
					bLoggedMissingTargets = true;
				}
				Attack.bInAttackRange = false;
				CastleMassFlightTargeting::SetStopIntent(Targets[It], FlightIntents[It]);
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
				continue;
			}
			FVector AttackPoint = TargetLocation;
			if (bTowerTarget)
			{
				const float EffectiveRadius = Radii[It].Radius * Transform.GetScale3D().GetAbsMax();
				const float IdealStandOff = FMath::Max(EffectiveRadius + 20.0f, Attack.AttackRange - 40.0f);
				float SurfaceDistance = 0.0f;
				if (!CastleMassFlightTargeting::SetMainTowerTarget(Main, Transform, Radii[It].Radius,
					Attack.AttackRange, IdealStandOff, Chunk.GetEntity(It).Index, Targets[It],
					FlightIntents[It], AttackPoint, SurfaceDistance))
				{
					bAnyMissing = true;
					Attack.bInAttackRange = false;
					CastleMassFlightTargeting::SetStopIntent(Targets[It], FlightIntents[It]);
					if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
					continue;
				}
				Attack.bInAttackRange = FlightIntents[It].bInAttackRange;
			}
			else
			{
				Attack.bInAttackRange = FVector::DistSquared(Origin, TargetLocation)
					<= FMath::Square(static_cast<double>(Attack.AttackRange));
				CastleMassFlightTargeting::SetAlternateTarget(TargetLocation, Attack.AttackRange,
					Attack.bInAttackRange, Targets[It], FlightIntents[It]);
			}
			if (!Attack.bInAttackRange)
			{
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
				continue;
			}
			FVector Facing = AttackPoint - Origin;
			Facing.Z = 0.0f;
			if (!Facing.IsNearlyZero()) { Transform.SetRotation(Facing.Rotation().Quaternion()); }
			if (!Animations.IsEmpty()) { Animations[It].bInAttackState = true; }
			Attack.AttackCooldown = FMath::Max(0.0, Attack.AttackCooldown - Chunk.GetDeltaTimeSeconds());
			if (Attack.AttackCooldown > 0.0f) { continue; }
			FCastleMassEnemyProjectileSpawn Launch;
			Launch.Origin = Origin;
			Launch.TargetLocation = AttackPoint;
			Launch.bHasTargetLocation = true;
			Launch.Attacker.World = GetWorld();
			Launch.Attacker.Entity = Chunk.GetEntity(It);
			Launch.ShieldId = ShieldId;
			Launch.BalloonId = BalloonId;
			Launch.Damage = Attack.AttackDamage;
			if (!bBalloonTarget && !bShieldTarget) { Launch.TargetTower = Main->GetRegisteredMainTower(); }
			UMassEntityConfigAsset* Configuration = Attack.ProjectileConfiguration.Get();
			TWeakObjectPtr<UWorld> World = GetWorld();
			// 结构性创建只能发生在安全边界，发射原点和目标保持本次决策的值，不重新索敌。
			Chunk.Defer().PushCommand<FMassDeferredCreateCommand>([World, Configuration, Launch](FMassEntityManager& Manager)
			{
				if (!World.IsValid())
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("敌方远程发射失败：世界已失效，配置 %s。"), *GetPathNameSafe(Configuration));
					return;
				}
				TArray<FCastleMassUnitHandle> Projectiles;
				FText Reason;
				auto* UnitSubsystem = World->GetSubsystem<UCastleMassUnitSubsystem>();
				if (!UnitSubsystem)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("敌方远程发射失败：世界 %s 缺少 Mass 单位子系统，配置 %s。"),
						*GetPathNameSafe(World.Get()), *GetPathNameSafe(Configuration));
					return;
				}
				if (!UnitSubsystem->SpawnEnemyProjectiles(Configuration, {Launch}, Projectiles, Reason)) { return; }
				// 延迟命令成功创建吐息弹后才反馈开火。
				UCastleAudioSubsystem::PlaySFX(World.Get(), ECastleSFXEvent::DragonFire);
				if (Manager.IsEntityActive(Launch.Attacker.Entity))
				{
					if (auto* State = Manager.GetFragmentDataPtr<FCastleMassEnemyRangedAttackFragment>(Launch.Attacker.Entity))
					{
						++State->AttackCount;
						// 以弹体实际创建成功时的世界倍率重置冷却，模板间隔保持独立。
						State->AttackCooldown = static_cast<double>(State->AttackInterval) / UnitSubsystem->GetGlobalEnemyAttackSpeedMultiplier();
					}
				}
			});
		}
	});
	if (!bAnyMissing) { bLoggedMissingTargets = false; }
}
