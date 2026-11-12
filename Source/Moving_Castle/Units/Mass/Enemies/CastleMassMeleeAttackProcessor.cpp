#include "CastleMassMeleeAttackProcessor.h"

#include "CastleMainTowerComponent.h"
#include "CastleMassFragments.h"
#include "CastleMassMeleeAttackFragments.h"
#include "CastleMassProcessors.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"

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
	Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassMeleeAttackFragment>(EMassFragmentAccess::ReadWrite);
}

void UCastleMassMeleeAttackProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UCastleMainTowerSubsystem* TowerSubsystem = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>() : nullptr;
	FVector TargetLocation = FVector::ZeroVector;
	const bool bHasTower = TowerSubsystem != nullptr && TowerSubsystem->GetMainTowerTargetLocation(TargetLocation);
	bool bLoggedMissingTower = false;
	Query.ForEachEntityChunk(Context, [TowerSubsystem, TargetLocation, bHasTower, &bLoggedMissingTower](FMassExecutionContext& Chunk)
	{
		const float DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const auto Transforms = Chunk.GetFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Melees = Chunk.GetMutableFragmentView<FCastleMassMeleeAttackFragment>();

		// 仅世界确有近战实体时才报缺失主塔，避免普通敌人或 NPC 世界产生无关错误。
		if (!bHasTower && !bLoggedMissingTower)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("Mass 近战处理器没有找到存活的主塔目标，当前近战批次停止移动。"));
			bLoggedMissingTower = true;
		}
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (Units[It].Health <= 0.0f) { continue; }
			FCastleMassMeleeAttackFragment& Melee = Melees[It];
			if (!bHasTower)
			{
				// 不把缺失目标解释为世界原点，不借用其他对象；明确停止等待配置修复。
				Melee.bInAttackRange = false;
				Targets[It] = FCastleMassMoveTargetFragment();
				Velocities[It].Value = FVector::ZeroVector;
				continue;
			}
			const FVector ToTower = TargetLocation - Transforms[It].GetTransform().GetLocation();
			Melee.bInAttackRange = ToTower.SizeSquared() <= FMath::Square(Melee.AttackRange);
			if (!Melee.bInAttackRange)
			{
				FCastleMassMoveTargetFragment& Target = Targets[It];
				Target.Target = TargetLocation;
				Target.DesiredVelocity = FVector::ZeroVector;
				Target.bHasTarget = true;
				continue;
			}

			// 近身后清除移动目标与速度，避免普通移动在下一帧继续穿过主塔。
			Targets[It] = FCastleMassMoveTargetFragment();
			Velocities[It].Value = FVector::ZeroVector;
			Melee.AttackCooldown = FMath::Max(0.0f, Melee.AttackCooldown - DeltaTime);
			if (Melee.AttackCooldown > 0.0f) { continue; }
			if (TowerSubsystem->ApplyMainTowerDamage(Melee.AttackDamage))
			{
				++Melee.AttackCount;
				Melee.AttackCooldown = Melee.AttackInterval;
			}
		}
	});
}
