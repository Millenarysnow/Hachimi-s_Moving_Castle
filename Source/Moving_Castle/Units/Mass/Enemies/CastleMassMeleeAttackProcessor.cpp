#include "CastleMassMeleeAttackProcessor.h"

#include "CastleMainTowerComponent.h"
#include "CastleMassFragments.h"
#include "CastleMassDeathSplitFragments.h"
#include "CastleMassMeleeAttackFragments.h"
#include "CastleMassProcessors.h"
#include "CastleMassUnitSubsystem.h"
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
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassSpawnBurstFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
	Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
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
		const auto Transforms = Chunk.GetFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
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
			const bool bHasTarget = bBalloonTarget || bShieldTarget || (TowerSubsystem && TowerSubsystem->GetMainTowerTargetLocation(TargetLocation));
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
				Targets[It] = FCastleMassMoveTargetFragment();
				Velocities[It].Value = FVector::ZeroVector;
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
				continue;
			}
			const FVector ToTower = TargetLocation - Transforms[It].GetTransform().GetLocation();
			Melee.bInAttackRange = ToTower.SizeSquared() <= FMath::Square(Melee.AttackRange);
			if (!Melee.bInAttackRange)
			{
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
				FCastleMassMoveTargetFragment& Target = Targets[It];
				Target.Target = TargetLocation;
				Target.DesiredVelocity = FVector::ZeroVector;
				Target.bHasTarget = true;
				continue;
			}

			// 近身后清除移动目标与速度，避免普通移动在下一帧继续穿过主塔。
			Targets[It] = FCastleMassMoveTargetFragment();
			Velocities[It].Value = FVector::ZeroVector;
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
