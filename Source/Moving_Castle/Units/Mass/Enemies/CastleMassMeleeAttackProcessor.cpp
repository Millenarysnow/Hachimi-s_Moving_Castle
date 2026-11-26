#include "CastleMassMeleeAttackProcessor.h"

#include "CastleMainTowerComponent.h"
#include "CastleMassFragments.h"
#include "CastleMassDeathSplitFragments.h"
#include "CastleMassMeleeAttackFragments.h"
#include "CastleMassProcessors.h"
#include "CastleMassUnitSubsystem.h"
#include "Enemies/Flight/CastleMassFlightTargeting.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"
#include "Subsystems/CastleShieldTowerSubsystem.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"

UCastleMassMeleeAttackProcessor::UCastleMassMeleeAttackProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
	ExecutionOrder.ExecuteAfter.Add(UCastleMassMovementProcessor::StaticClass()->GetFName());
	// 主塔生命结算会访问 UObject；保留单个批量处理器的游戏线程边界。
	bRequiresGameThreadExecution = true;
}

void UCastleMassMeleeAttackProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassSpawnBurstFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassFlightIntentFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	// 护盾反伤即时修改攻击者生命，必须向调度器声明写权限。
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassMeleeAttackFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassAttackAnimationFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
}

void UCastleMassMeleeAttackProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UCastleMainTowerSubsystem* TowerSubsystem = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>() : nullptr;
	UCastleShieldTowerSubsystem* Shields = GetWorld()->GetSubsystem<UCastleShieldTowerSubsystem>();
	UCastleSupportTowerSubsystem* Support = GetWorld()->GetSubsystem<UCastleSupportTowerSubsystem>();
	if (!Support)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 近战索敌失败：世界 %s 缺少支援塔子系统。"), *GetPathNameSafe(GetWorld()));
		return;
	}
	const auto* UnitSubsystem = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
	if (!UnitSubsystem)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 近战处理器：世界 %s 缺少 Mass 单位子系统，无法读取敌人攻速。"), *GetPathNameSafe(GetWorld()));
		return;
	}
	bool bAnyMissingTarget = false;
	Query.ForEachEntityChunk(Context, [this, TowerSubsystem, Shields, Support, UnitSubsystem, &bAnyMissingTarget](FMassExecutionContext& Chunk)
	{
		const float DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
		const auto FlightIntents = Chunk.GetMutableFragmentView<FCastleMassFlightIntentFragment>();
		const auto Radii = Chunk.GetFragmentView<FAgentRadiusFragment>();
		const auto Units = Chunk.GetMutableFragmentView<FCastleMassUnitFragment>();
		const auto Melees = Chunk.GetMutableFragmentView<FCastleMassMeleeAttackFragment>();
		const auto Animations = Chunk.GetMutableFragmentView<FCastleMassAttackAnimationFragment>();

		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (Units[It].Health <= 0.0f) { continue; }
			FCastleMassMeleeAttackFragment& Melee = Melees[It];
			FVector TargetLocation;
			// 在气球的仇恨圈内优先追气球；没有气球时沿用近战护盾吸引及主塔规则。
			uint64 BalloonId = 0;
			const bool bBalloonTarget = Support->FindBalloonTarget(Transforms[It].GetTransform().GetLocation(), BalloonId, TargetLocation);
			uint64 ShieldId = 0;
			const bool bShieldTarget = !bBalloonTarget && Shields->FindNearestActiveShield(
				Transforms[It].GetTransform().GetLocation(), MAX_flt, true, ShieldId, TargetLocation);
			const bool bTowerTarget = !bBalloonTarget && !bShieldTarget
				&& TowerSubsystem && TowerSubsystem->GetMainTowerTargetLocation(TargetLocation);
			const bool bHasTarget = bBalloonTarget || bShieldTarget || bTowerTarget;
			if (!bHasTarget)
			{
				bAnyMissingTarget = true;
				if (!bLoggedMissingTargets)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("Mass 近战处理器：世界 %s 没有吸引圈内激活护盾或存活主塔，停止移动。"), *GetNameSafe(GetWorld()));
					bLoggedMissingTargets = true;
				}
				// 不把缺失目标解释为世界原点，不借用其他对象；明确停止等待配置修复。
				Melee.bInAttackRange = false;
				CastleMassFlightTargeting::SetStopIntent(Targets[It], FlightIntents[It]);
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
				continue;
			}
			FTransform& Transform = Transforms[It].GetMutableTransform();
			FVector AttackPoint = TargetLocation;
			if (bTowerTarget)
			{
				const float Radius = Radii[It].Radius * Transform.GetScale3D().GetAbsMax();
				const float IdealStandOff = FMath::Min(Radius + 18.0f,
					FMath::Max(Radius + 4.0f, Melee.AttackRange * 0.7f));
				float SurfaceDistance = 0.0f;
				if (!CastleMassFlightTargeting::SetMainTowerTarget(TowerSubsystem, Transform, Radii[It].Radius,
					Melee.AttackRange, IdealStandOff, Chunk.GetEntity(It).Index, Targets[It],
					FlightIntents[It], AttackPoint, SurfaceDistance))
				{
					Melee.bInAttackRange = false;
					CastleMassFlightTargeting::SetStopIntent(Targets[It], FlightIntents[It]);
					if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
					continue;
				}
				Melee.bInAttackRange = FlightIntents[It].bInAttackRange;
			}
			else
			{
				Melee.bInAttackRange = FVector::DistSquared(Transform.GetLocation(), TargetLocation)
					<= FMath::Square(static_cast<double>(Melee.AttackRange));
				CastleMassFlightTargeting::SetAlternateTarget(TargetLocation, Melee.AttackRange,
					Melee.bInAttackRange, Targets[It], FlightIntents[It]);
			}
			if (!Melee.bInAttackRange)
			{
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
				continue;
			}

			FVector Facing = AttackPoint - Transform.GetLocation();
			Facing.Z = 0.0f;
			if (!Facing.IsNearlyZero()) { Transform.SetRotation(Facing.Rotation().Quaternion()); }
			if (!Animations.IsEmpty()) { Animations[It].bInAttackState = true; }
			Melee.AttackCooldown = FMath::Max(0.0, Melee.AttackCooldown - DeltaTime);
			if (Melee.AttackCooldown > 0.0f) { continue; }
			FCastleMassUnitHandle Attacker;
			Attacker.World = GetWorld();
			Attacker.Entity = Chunk.GetEntity(It);
			int64 RemainingBlocks = 0;
			const bool bApplied = bBalloonTarget ? Support->DamageBalloon(BalloonId, Melee.AttackDamage)
				: (bShieldTarget ? Shields->ApplyAttackToShield(ShieldId, Attacker, RemainingBlocks)
					: TowerSubsystem->ApplyMainTowerDamage(Melee.AttackDamage));
			if (bApplied)
			{
				++Melee.AttackCount;
				// 每次命中重新读取世界倍率，当前已经开始的冷却计时维持原速度。
				Melee.AttackCooldown = static_cast<double>(Melee.AttackInterval) / UnitSubsystem->GetGlobalEnemyAttackSpeedMultiplier();
			}
		}
	});
	if (!bAnyMissingTarget) { bLoggedMissingTargets = false; }
}
