#include "CastleMassEnemyLaserAttackProcessor.h"

#include "CastleMainTowerComponent.h"
#include "CastleMassDeathSplitFragments.h"
#include "CastleMassEnemyLaserAttackFragments.h"
#include "CastleMassEnemyLaserBeamSubsystem.h"
#include "CastleMassDeathSplitProcessor.h"
#include "CastleMassFragments.h"
#include "CastleMassProcessors.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"
#include "Subsystems/CastleShieldTowerSubsystem.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"

UCastleMassEnemyLaserAttackProcessor::UCastleMassEnemyLaserAttackProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
	ExecutionOrder.ExecuteAfter.Add(UCastleMassMovementProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteAfter.Add(UCastleMassSpawnBurstProcessor::StaticClass()->GetFName());
	bRequiresGameThreadExecution = true;
}

void UCastleMassEnemyLaserAttackProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassSpawnBurstFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
	Query.AddRequirement<FCastleMassEnemyLaserAttackFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassAttackAnimationFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
}

void UCastleMassEnemyLaserAttackProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = GetWorld();
	UCastleShieldTowerSubsystem* Shields = World ? World->GetSubsystem<UCastleShieldTowerSubsystem>() : nullptr;
	UCastleMainTowerSubsystem* Main = World ? World->GetSubsystem<UCastleMainTowerSubsystem>() : nullptr;
	UCastleSupportTowerSubsystem* Support = World ? World->GetSubsystem<UCastleSupportTowerSubsystem>() : nullptr;
	if (!Support)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 激光索敌失败：世界 %s 缺少支援塔子系统。"), *GetPathNameSafe(World));
		return;
	}
	bool bAnyMissingTarget = false;
	Query.ForEachEntityChunk(Context, [this, World, Shields, Main, Support, &bAnyMissingTarget](FMassExecutionContext& Chunk)
	{
		const float DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
		const auto Units = Chunk.GetMutableFragmentView<FCastleMassUnitFragment>();
		const auto Attacks = Chunk.GetMutableFragmentView<FCastleMassEnemyLaserAttackFragment>();
		const auto Animations = Chunk.GetMutableFragmentView<FCastleMassAttackAnimationFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			FCastleMassEnemyLaserAttackFragment& Attack = Attacks[It];
			if (Units[It].Health <= 0.0f)
			{
				Attack.bInAttackRange = false;
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
				continue;
			}
			FTransform& Transform = Transforms[It].GetMutableTransform();
			const FVector Origin = Transform.GetLocation();
			FVector TargetLocation = FVector::ZeroVector;
			// 光束的终点和伤害都使用同一帧选择的气球目标。
			uint64 BalloonId = 0;
			const bool bBalloonTarget = Support->FindBalloonTarget(Origin, BalloonId, TargetLocation);
			uint64 ShieldId = 0;
			const bool bShieldTarget = !bBalloonTarget && Shields && Shields->FindNearestActiveShield(Origin, Attack.AttackRange, false, ShieldId, TargetLocation);
			const bool bTowerTarget = !bBalloonTarget && !bShieldTarget && Main && Main->GetMainTowerTargetLocation(TargetLocation);
			if (!bBalloonTarget && !bShieldTarget && !bTowerTarget)
			{
				bAnyMissingTarget = true;
				Attack.bInAttackRange = false;
				Attack.AttackCooldown = 0.0f;
				Targets[It] = FCastleMassMoveTargetFragment();
				Velocities[It].Value = FVector::ZeroVector;
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
				continue;
			}
			Attack.bInAttackRange = FVector::DistSquared(Origin, TargetLocation) <= FMath::Square(static_cast<double>(Attack.AttackRange));
			if (!Attack.bInAttackRange)
			{
				// 再次进入射程视为新的引导，首次伤害立即发生。
				Attack.AttackCooldown = 0.0f;
				Targets[It].Target = TargetLocation;
				Targets[It].DesiredVelocity = FVector::ZeroVector;
				Targets[It].bHasTarget = true;
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
				continue;
			}
			Targets[It] = FCastleMassMoveTargetFragment();
			Velocities[It].Value = FVector::ZeroVector;
			FVector Facing = TargetLocation - Origin;
			Facing.Z = 0.0f;
			if (!Facing.IsNearlyZero()) { Transform.SetRotation(Facing.Rotation().Quaternion()); }
			if (!Animations.IsEmpty()) { Animations[It].bInAttackState = true; }
			const FVector Muzzle = Transform.TransformPosition(Attack.MuzzleLocalOffset);
			if (!Attack.BeamMesh.IsValid() || !Attack.BeamMaterial.IsValid())
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("敌方激光运行失败：实体 %d 的光束资产失效。"), Chunk.GetEntity(It).Index);
				continue;
			}
			Attack.BeamStart = Muzzle;
			Attack.BeamEnd = TargetLocation;

			Attack.AttackCooldown = FMath::Max(0.0f, Attack.AttackCooldown - DeltaTime);
			if (Attack.AttackCooldown > 0.0f) { continue; }
			FCastleMassUnitHandle Attacker;
			Attacker.World = World;
			Attacker.Entity = Chunk.GetEntity(It);
			int64 RemainingBlocks = 0;
			const bool bApplied = bBalloonTarget ? Support->DamageBalloon(BalloonId, Attack.AttackDamage)
				: (bShieldTarget ? Shields->ApplyAttackToShield(ShieldId, Attacker, RemainingBlocks)
					: Main->ApplyMainTowerDamage(Attack.AttackDamage));
			if (bApplied)
			{
				++Attack.AttackCount;
				Attack.AttackCooldown = Attack.AttackInterval;
			}
			if (Units[It].Health <= 0.0f)
			{
				// 护盾即时反伤可在本次结算中致死，不能继续保留攻击表现态。
				Attack.bInAttackRange = false;
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
			}
		}
	});
	if (!bAnyMissingTarget) { bLoggedMissingTargets = false; }
	if (bAnyMissingTarget && !bLoggedMissingTargets)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 激光处理器：世界 %s 没有激活护盾或存活主塔，光束已收束并停止移动。"), *GetNameSafe(World));
		bLoggedMissingTargets = true;
	}
}
