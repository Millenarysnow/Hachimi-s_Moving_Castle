#include "CastleMassEnemyRangedAttackProcessor.h"

#include "CastleMainTowerComponent.h"
#include "CastleMassEnemyRangedAttackFragments.h"
#include "CastleMassFragments.h"
#include "CastleMassDeathSplitFragments.h"
#include "CastleMassProcessors.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "MassCommands.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassEntityConfigAsset.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"
#include "Subsystems/CastleShieldTowerSubsystem.h"

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
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassSpawnBurstFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
	Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassEnemyRangedAttackFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassAttackAnimationFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
}

void UCastleMassEnemyRangedAttackProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UCastleShieldTowerSubsystem* Shields = GetWorld()->GetSubsystem<UCastleShieldTowerSubsystem>();
	UCastleMainTowerSubsystem* Main = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>();
	bool bAnyMissing = false;
	Query.ForEachEntityChunk(Context, [this, Shields, Main, &bAnyMissing](FMassExecutionContext& Chunk)
	{
		const auto Transforms = Chunk.GetFragmentView<FTransformFragment>();
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
		const auto Attacks = Chunk.GetMutableFragmentView<FCastleMassEnemyRangedAttackFragment>();
		const auto Animations = Chunk.GetMutableFragmentView<FCastleMassAttackAnimationFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (Units[It].Health <= 0.0f) { continue; }
			auto& Attack = Attacks[It];
			const FVector Origin = Transforms[It].GetTransform().GetLocation();
			FVector TargetLocation;
			uint64 ShieldId = 0;
			const bool bShieldTarget = Shields->FindNearestActiveShield(Origin, Attack.AttackRange, false, ShieldId, TargetLocation);
			if (!bShieldTarget && !Main->GetMainTowerTargetLocation(TargetLocation))
			{
				bAnyMissing = true;
				if (!bLoggedMissingTargets)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("Mass 远程处理器：世界 %s 没有射程内激活护盾或存活主塔，停止移动。"), *GetNameSafe(GetWorld()));
					bLoggedMissingTargets = true;
				}
				Attack.bInAttackRange = false;
				Targets[It] = FCastleMassMoveTargetFragment();
				Velocities[It].Value = FVector::ZeroVector;
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
				continue;
			}
			Attack.bInAttackRange = FVector::DistSquared(Origin, TargetLocation) <= FMath::Square(static_cast<double>(Attack.AttackRange));
			if (!Attack.bInAttackRange)
			{
				if (!Animations.IsEmpty()) { Animations[It].bInAttackState = false; }
				Targets[It].Target = TargetLocation;
				Targets[It].DesiredVelocity = FVector::ZeroVector;
				Targets[It].bHasTarget = true;
				continue;
			}
			Targets[It] = FCastleMassMoveTargetFragment();
			Velocities[It].Value = FVector::ZeroVector;
			if (!Animations.IsEmpty()) { Animations[It].bInAttackState = true; }
			Attack.AttackCooldown = FMath::Max(0.0f, Attack.AttackCooldown - Chunk.GetDeltaTimeSeconds());
			if (Attack.AttackCooldown > 0.0f) { continue; }
			FCastleMassEnemyProjectileSpawn Launch;
			Launch.Origin = Origin;
			Launch.Attacker.World = GetWorld();
			Launch.Attacker.Entity = Chunk.GetEntity(It);
			Launch.ShieldId = ShieldId;
			Launch.Damage = Attack.AttackDamage;
			if (!bShieldTarget) { Launch.TargetTower = Main->GetRegisteredMainTower(); }
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
				if (!World->GetSubsystem<UCastleMassUnitSubsystem>()->SpawnEnemyProjectiles(Configuration, {Launch}, Projectiles, Reason)) { return; }
				if (Manager.IsEntityActive(Launch.Attacker.Entity))
				{
					if (auto* State = Manager.GetFragmentDataPtr<FCastleMassEnemyRangedAttackFragment>(Launch.Attacker.Entity))
					{
						++State->AttackCount;
						State->AttackCooldown = State->AttackInterval;
					}
				}
			});
		}
	});
	if (!bAnyMissing) { bLoggedMissingTargets = false; }
}
