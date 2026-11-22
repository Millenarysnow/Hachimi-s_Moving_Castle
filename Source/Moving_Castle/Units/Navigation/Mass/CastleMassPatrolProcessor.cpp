#include "Mass/CastleMassPatrolProcessor.h"

#include "CastleMassFragments.h"
#include "CastleMassProcessors.h"
#include "CastleNavigationSubsystem.h"
#include "CastleTowerDispatchSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Mass/CastlePatrolMovement.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"
#include "Units/BuildingInteraction/Mass/CastleMassBuildingTraversalFragment.h"

UCastleMassPatrolProcessor::UCastleMassPatrolProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
	ExecutionOrder.ExecuteBefore.Add(UCastleMassMovementProcessor::StaticClass()->GetFName());
	// 游戏线程只负责取得快照与汇总诊断；实际位移按 Chunk 并行，没有工作线程 UObject 访问。
	bRequiresGameThreadExecution = true;
}

void UCastleMassPatrolProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassNPCTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassPatrolFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassBuildingTraversalFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddConstSharedRequirement<FCastleMassMovementParameters>();
}

void UCastleMassPatrolProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const auto Path = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->AcquireSharedPath();
	const auto* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>();
	const bool bAutoDispatch = Dispatch && Dispatch->IsAutoDispatchEnabled();
	TAtomic<int32> NewlyBlocked(0);
	TAtomic<int32> InvalidLadderSettings(0);
	Query.ParallelForEachEntityChunk(Context, [Path, bAutoDispatch, &NewlyBlocked, &InvalidLadderSettings](FMassExecutionContext& Chunk)
	{
		const double DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Patrols = Chunk.GetMutableFragmentView<FCastleMassPatrolFragment>();
		const auto Traversals = Chunk.GetMutableFragmentView<FCastleMassBuildingTraversalFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto& Settings = Chunk.GetConstSharedFragment<FCastleMassMovementParameters>().Settings;
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			auto& Patrol = Patrols[It];
			auto& Traversal = Traversals[It];
			// 先结束上轮记录；停止、死亡、失效路线和绑定失败都不能残留可再次消费的旧位移。
			++Traversal.MovementSerial;
			Traversal.PathRevision = INDEX_NONE;
			Traversal.TravelDistance = 0.0;
			if (!Patrol.bEnabled || Patrol.bSupportJourney || Units[It].Health <= 0.0f) { continue; }
			FTransform& Transform = Transforms[It].GetMutableTransform();
			FVector& Velocity = Velocities[It].Value;
			Targets[It] = FCastleMassMoveTargetFragment();
			// 几何开始变化后立即停速；即使持有旧快照，也不允许沿过期的楼板继续走。
			if (!Path || !Path->bValid || Patrol.bReachedDispatchEntrance
				|| (Patrol.bTowerDispatched ? !Path->bTowerDispatch : !Path->bClosedLoop))
			{
				Velocity = FVector::ZeroVector;
				Patrol.Speed = 0.0;
				continue;
			}
			if (Patrol.PathRevision != Path->PathRevision)
			{
				const bool bSameRoute = Patrol.RouteRevision == Path->RouteRevision;
				Patrol.PathRevision = Path->PathRevision;
				Patrol.RouteRevision = Path->RouteRevision;
				double Separation;
				// 网格重建保留原路段，不能因返程与去程重叠而反向、跳过玩家路径点。
				const bool bLocated = Path->FindClosestPatrolDistance(Transform.GetLocation(), bSameRoute ? Patrol.Leg : INDEX_NONE, Patrol.Distance, Separation);
				Patrol.bBlocked = !bLocated || Separation > 50.0;
				if (Patrol.bBlocked) { ++NewlyBlocked; }
				else { Patrol.Leg = Path->FindPatrolLeg(Patrol.Distance); }
			}
			if (Patrol.bBlocked)
			{
				Velocity = FVector::ZeroVector;
				Patrol.Speed = 0.0;
				continue;
			}
			double Travel = 0.0;
			FCastleMassMovementSettings EffectiveSettings = Settings;
			EffectiveSettings.MaxSpeed *= Units[It].SupportMovementMultiplier;
			const bool bFinishAtEntrance = Patrol.bTowerDispatched && !bAutoDispatch;
			if (!CastlePatrolMovement::Advance(*Path, EffectiveSettings, Patrol.Distance, DeltaTime, Patrol.Speed, Travel, bFinishAtEntrance))
			{
				++InvalidLadderSettings;
			}
			if (Path->Length > UE_DOUBLE_SMALL_NUMBER)
			{
				// 必须在版本重绑定完成后记录真实起点，且保留未取模的行进长度。
				// 只比较本帧两个终点会漏掉整栋穿越、同帧多次进入和跨越多圈的情况。
				Traversal.PathRevision = Path->PathRevision;
				Traversal.StartDistance = Patrol.Distance;
				Traversal.TravelDistance = Travel;
				const double Total = Patrol.Distance + Travel;
				if (bFinishAtEntrance && Total >= Path->Length - UE_DOUBLE_SMALL_NUMBER)
				{
					// 只写值状态，绝不在并行查询中改变实体组成；调度器会在安全边界确认销毁。
					Patrol.Distance = Path->Length;
					Patrol.Speed = 0.0;
					Patrol.bReachedDispatchEntrance = true;
				}
				else
				{
					Patrol.CompletedLaps += static_cast<int64>(Total / Path->Length);
					Patrol.Distance = FMath::Fmod(Total, Path->Length);
				}
			}
			else { Patrol.Speed = 0.0; Patrol.Distance = 0.0; }
			FVector Location;
			FVector Direction;
			Path->SamplePatrol(Patrol.Distance, Location, Direction);
			Transform.SetLocation(Location);
			Velocity = Direction * Patrol.Speed;
			Patrol.Leg = Path->FindPatrolLeg(Patrol.Distance);
			double LadderEndDistance;
			FVector LadderFacingDirection;
			const bool bOnLadder = Path->FindLadderRange(Patrol.Distance, LadderEndDistance, LadderFacingDirection);
			if (Settings.bKeepUpright || bOnLadder)
			{
				FRotator UprightRotation = Transform.Rotator();
				UprightRotation.Pitch = 0.0f;
				UprightRotation.Roll = 0.0f;
				const FVector Facing = bOnLadder ? LadderFacingDirection : FVector(Direction.X, Direction.Y, 0.0);
				if (!Facing.IsNearlyZero())
				{
					FRotator TargetRotation = Facing.Rotation();
					TargetRotation.Pitch = 0.0f;
					TargetRotation.Roll = 0.0f;
					UprightRotation = Settings.TurnRate > 0.0f
						? FMath::RInterpConstantTo(UprightRotation, TargetRotation, DeltaTime, Settings.TurnRate)
						: TargetRotation;
					UprightRotation.Pitch = 0.0f;
					UprightRotation.Roll = 0.0f;
				}
				Transform.SetRotation(UprightRotation.Quaternion());
			}
			else if (!Direction.IsNearlyZero() && Settings.TurnRate > 0.0f)
			{
				Transform.SetRotation(FMath::RInterpConstantTo(Transform.Rotator(), Direction.Rotation(), DeltaTime, Settings.TurnRate).Quaternion());
			}
		}
	});
	if (NewlyBlocked.Load() > 0)
	{
		// 只在新版本绑定失败时汇总一次；不每帧刷屏，也不偷偷传送或退回直线穿障碍。
		const FString Message = FString::Printf(TEXT("NPC 巡逻：%d 个单位距当前共享路段超过 50 cm，已停止。请将生成器设为沿共享路线生成，或调整路径点覆盖这些单位。"), NewlyBlocked.Load());
		UE_LOG(LogMoving_Castle, Warning, TEXT("%s"), *Message);
		if (GEngine) { GEngine->AddOnScreenDebugMessage(2026111101, 8.0f, FColor::Yellow, Message); }
	}
	if (InvalidLadderSettings.Load() > 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("NPC 巡逻：%d 个单位遇到非法梯子速度倍率；要求有限数且位于 (0.01, 10.0]，单位已停止。"), InvalidLadderSettings.Load());
	}
}
