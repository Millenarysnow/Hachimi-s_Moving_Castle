#include "Units/BuildingInteraction/Mass/CastleMassBuildingTriggerProcessor.h"

#include "CastleMassFragments.h"
#include "CastleNavigationSubsystem.h"
#include "Containers/Queue.h"
#include "Engine/World.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Mass/CastleMassPatrolProcessor.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "Units/BuildingInteraction/CastleBuildingTriggerSubsystem.h"
#include "Units/BuildingInteraction/Mass/CastleMassBuildingTraversalFragment.h"

UCastleMassBuildingTriggerProcessor::UCastleMassBuildingTriggerProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
	ExecutionOrder.ExecuteAfter.Add(UCastleMassPatrolProcessor::StaticClass()->GetFName());
	// 只在取得快照及最终派发时访问 UObject；工作线程没有 Actor 或组件访问。
	bRequiresGameThreadExecution = true;
}

void UCastleMassBuildingTriggerProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassNPCTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassPatrolFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassBuildingTraversalFragment>(EMassFragmentAccess::ReadWrite);
}

void UCastleMassBuildingTriggerProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UCastleBuildingTriggerSubsystem* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>();
	const auto Path = Triggers->AcquireTriggerPath(GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->AcquireSharedPath());
	TQueue<TArray<FCastleBuildingEntryEvent>, EQueueMode::Mpsc> Batches;
	Query.ParallelForEachEntityChunk(Context, [Path, &Batches](FMassExecutionContext& Chunk)
	{
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Patrols = Chunk.GetFragmentView<FCastleMassPatrolFragment>();
		const auto Traversals = Chunk.GetMutableFragmentView<FCastleMassBuildingTraversalFragment>();
		TArray<FCastleBuildingEntryEvent> Events;
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			auto& Traversal = Traversals[It];
			if (Traversal.ConsumedMovementSerial == Traversal.MovementSerial) { continue; }
			Traversal.ConsumedMovementSerial = Traversal.MovementSerial;
			if (!Path || Traversal.PathRevision != Path->PathRevision || !Patrols[It].bEnabled
				|| Patrols[It].bBlocked || Units[It].Health <= 0.0f) { continue; }
			const FMassEntityHandle Entity = Chunk.GetEntity(It);
			Path->ForEachEntry(Traversal.StartDistance, Traversal.TravelDistance,
				[&Events, Entity](const FCastleBuildingEntryMarker& Marker)
				{
					Events.Add({Entity, Marker.BuildingId, Marker.Location});
				});
		}
		// 每 Chunk 只入队一次，避免每名 NPC 或每个进入事件都争用同一把锁。
		if (!Events.IsEmpty()) { Batches.Enqueue(MoveTemp(Events)); }
	});
	TArray<FCastleBuildingEntryEvent> Events;
	while (Batches.Dequeue(Events)) { Triggers->DispatchEntries(*Path, Events); }
}
