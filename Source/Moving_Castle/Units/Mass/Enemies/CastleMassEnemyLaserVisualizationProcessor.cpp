#include "CastleMassEnemyLaserVisualizationProcessor.h"

#include "CastleMassDeathSplitFragments.h"
#include "CastleMassEnemyLaserAttackFragments.h"
#include "CastleMassEnemyLaserBeamSubsystem.h"
#include "CastleMassFragments.h"
#include "CastleMassProcessors.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "Engine/World.h"
#include "Moving_Castle.h"
#include "Projectiles/CastleMassHomingProjectileProcessor.h"
#include "Projectiles/CastleMassEnemyProjectileProcessor.h"

UCastleMassEnemyLaserVisualizationProcessor::UCastleMassEnemyLaserVisualizationProcessor() : Query(*this)
{
	// 此处理器访问世界级渲染批次，只参与游戏世界，不参加无 UWorld 的 Mass 编辑器调度。
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteAfter.Add(UCastleMassHomingProjectileProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteAfter.Add(UCastleMassEnemyProjectileProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteBefore.Add(UCastleMassLifetimeProcessor::StaticClass()->GetFName());
	bRequiresGameThreadExecution = true;
	// 最后一个幼骸被删除时也必须执行空批次提交，不能剪去清理帧。
	QueryBasedPruning = EMassQueryBasedPruning::Never;
}

void UCastleMassEnemyLaserVisualizationProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddRequirement<FCastleMassEnemyLaserAttackFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassSpawnBurstFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
}

void UCastleMassEnemyLaserVisualizationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TArray<FCastleMassEnemyLaserBeam> Beams;
	Query.ForEachEntityChunk(Context, [&Beams](FMassExecutionContext& Chunk)
	{
		const auto Attacks = Chunk.GetFragmentView<FCastleMassEnemyLaserAttackFragment>();
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			// 仅存活且处于射程态的实体拥有可见光束；这是能力状态的定义。
			if (Units[It].Health <= 0.0f || !Attacks[It].bInAttackRange) { continue; }
			const auto& Attack = Attacks[It];
			FCastleMassEnemyLaserBeam& Beam = Beams.AddDefaulted_GetRef();
			Beam.Entity = Chunk.GetEntity(It);
			Beam.Start = Attack.BeamStart;
			Beam.End = Attack.BeamEnd;
			Beam.Thickness = Attack.BeamThickness;
			Beam.Mesh = Attack.BeamMesh.Get();
			Beam.Material = Attack.BeamMaterial.Get();
		}
	});
	FString Reason;
	if (!GetWorld()->GetSubsystem<UCastleMassEnemyLaserBeamSubsystem>()->UpdateBeams(Beams, Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 激光批次更新失败：%s"), *Reason);
	}
}

UCastleMassEnemyLaserRemovalObserver::UCastleMassEnemyLaserRemovalObserver() : Query(*this)
{
	ObservedTypes.Add(FCastleMassEnemyLaserAttackFragment::StaticStruct());
	ObservedOperations = EMassObservedOperationFlags::Remove;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	bRequiresGameThreadExecution = true;
}

void UCastleMassEnemyLaserRemovalObserver::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddRequirement<FCastleMassEnemyLaserAttackFragment>(EMassFragmentAccess::ReadOnly);
}

void UCastleMassEnemyLaserRemovalObserver::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UCastleMassEnemyLaserBeamSubsystem* Renderer = GetWorld()->GetSubsystem<UCastleMassEnemyLaserBeamSubsystem>();
	Query.ForEachEntityChunk(Context, [Renderer](FMassExecutionContext& Chunk)
	{
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It) { Renderer->RemoveBeam(Chunk.GetEntity(It)); }
	});
}
