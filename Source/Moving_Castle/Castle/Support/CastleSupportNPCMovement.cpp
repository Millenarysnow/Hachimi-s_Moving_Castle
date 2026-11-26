#include "CastleSupportTowerSubsystem.h"

#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleMassFragments.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleSupportTowerData.h"
#include "Engine/World.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Mass/CastlePatrolMovement.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"
#include "Units/BuildingInteraction/CastleBuildingTriggerSubsystem.h"

bool UCastleSupportTowerSubsystem::RallyNPCs(FMassEntityManager& Manager, ACastleBuildingBase& Source,
	const FVector& Destination, TConstArrayView<FCastleSupportUnit> NPCs)
{
	UCastleNavigationSubsystem* Nav = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	UCastleBuildingTriggerSubsystem* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>();
	const auto Shared = Nav->AcquireSharedPath();
	TMap<FMassEntityHandle, FNPCJourney> Prepared;
	auto Fail = [&Source](const FMassEntityHandle Entity, const FString& Reason)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 集结人员 %d 失败：%s；未发布本批命令。"), *Source.GetPathName(), Entity.Index, *Reason);
		return false;
	};
	for (const FCastleSupportUnit& NPC : NPCs)
	{
		const FMassEntityHandle Entity = NPC.Handle.Entity;
		const auto* Patrol = Manager.GetFragmentDataPtr<FCastleMassPatrolFragment>(Entity);
		const auto* Move = Manager.GetFragmentDataPtr<FCastleMassMoveTargetFragment>(Entity);
		if (!Patrol || !Move) { return Fail(Entity, TEXT("NPC 缺少移动状态")); }
		const FNPCJourney* Previous = Journeys.Find(Entity);
		const bool bPatrolling = Patrol->bEnabled && !Patrol->bBlocked && Shared && Shared->bValid && Shared->Length > 0.0;
		const bool bDirectMove = !Patrol->bEnabled && (Move->bHasTarget || !Move->DesiredVelocity.IsNearlyZero());
		// 只调动在途人员，待命者不是集结对象。新落点可抢占旧落点，但仍记住最初的接驳点。
		if (!Previous && !bPatrolling && !bDirectMove) { continue; }
		FNPCJourney Candidate;
		Candidate.Source = &Source;
		Candidate.bRally = true;
		FVector Rejoin = NPC.Location;
		if (Previous)
		{
			Rejoin = Previous->Path.PatrolPoints.Last();
			Candidate.JoinDistance = Previous->JoinDistance;
			Candidate.JoinRevision = Previous->JoinRevision;
		}
		else if (bPatrolling)
		{
			if (Patrol->PathRevision != Shared->PathRevision) { return Fail(Entity, TEXT("人员尚未绑定当前闭环版本")); }
			Candidate.JoinDistance = Patrol->Distance;
			Candidate.JoinRevision = Shared->PathRevision;
			FVector Direction;
			Shared->SamplePatrol(Candidate.JoinDistance, Rejoin, Direction);
		}
		FText Reason;
		const FVector Points[] = {NPC.Location, Destination, Rejoin};
		if (!Nav->BuildTemporaryPath(Points, Candidate.Path, Reason)
			|| !Triggers->CompileTemporaryPath(Candidate.Path, Candidate.Triggers, Reason)) { return Fail(Entity, Reason.ToString()); }
		Prepared.Add(Entity, MoveTemp(Candidate));
	}
	for (auto& Pair : Prepared)
	{
		Manager.GetFragmentDataChecked<FCastleMassPatrolFragment>(Pair.Key).bSupportJourney = true;
		Journeys.Add(Pair.Key, MoveTemp(Pair.Value));
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("空投塔 %s 集结 %d 名在途人员到 %s。"), *Source.GetName(), Prepared.Num(), *Destination.ToCompactString());
	return true;
}

bool UCastleSupportTowerSubsystem::RecruitNPCs(FMassEntityManager& Manager, ACastleBuildingBase& Source,
	const FVector& Destination, const int32 Count)
{
	if (Count == 0)
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("蓝色空投塔 %s 的数字和个位为 0，本次招募 0 名。"), *Source.GetName());
		return true;
	}
	UCastleNavigationSubsystem* Nav = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	UCastleBuildingTriggerSubsystem* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>();
	const auto Shared = Nav->AcquireSharedPath();
	FText Reason;
	auto* Config = Source.GetBuildingDefinition()->SupportData->NPCConfiguration.Get();
	FNPCJourney Journey;
	Journey.Source = &Source;
	FVector Join = Destination;
	const bool bHasPatrol = Shared && Shared->bValid && Shared->bClosedLoop;
	if (!bHasPatrol && !Nav->GetRequestedWaypoints().IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("蓝色空投塔 %s 招募失败：已设定的玩家路线无效，不能把人员改为原地待命。"), *Source.GetPathName());
		return false;
	}
	if (bHasPatrol)
	{
		double Separation;
		FVector Direction;
		if (!Shared->FindClosestPatrolDistance(Destination, INDEX_NONE, Journey.JoinDistance, Separation))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("蓝色空投塔 %s 招募失败：不能在当前闭环选取接驳点。"), *Source.GetPathName());
			return false;
		}
		Shared->SamplePatrol(Journey.JoinDistance, Join, Direction);
		Journey.JoinRevision = Shared->PathRevision;
	}
	const FVector Points[] = { Destination, Join };
	if (!Nav->BuildTemporaryPath(Points, Journey.Path, Reason)
		|| !Triggers->CompileTemporaryPath(Journey.Path, Journey.Triggers, Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("蓝色空投塔 %s 招募失败，人员资产=%s：%s"),
			*Source.GetPathName(), *GetPathNameSafe(Config), *Reason.ToString());
		return false;
	}
	// 位置使用本次落地面的同一导航坐标；一批人员共用一次路径求解。
	TArray<FTransform> Transforms;
	Transforms.Init(FTransform(Journey.Path.PatrolPoints[0]), Count);
	TArray<FCastleMassUnitHandle> Recruits;
	if (!GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>()->SpawnUnits(Config, Transforms, Recruits, Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("蓝色空投塔 %s 招募失败，人员资产=%s：%s"), *Source.GetPathName(), *GetPathNameSafe(Config), *Reason.ToString());
		return false;
	}
	RecruitedNPCs.Append(Recruits);
	if (Recruits.Num() != Count)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("蓝色空投塔 %s 招募数量不符：要求 %d，得到 %d，人员资产=%s。"),
			*Source.GetPathName(), Count, Recruits.Num(), *GetPathNameSafe(Config));
		return false;
	}
	if (bHasPatrol)
	{
		for (const FCastleMassUnitHandle& Unit : Recruits)
		{
			Manager.GetFragmentDataChecked<FCastleMassPatrolFragment>(Unit.Entity).bSupportJourney = true;
			Journeys.Add(Unit.Entity, Journey);
		}
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("蓝色空投塔 %s 在落点招募 %d 名人员，%s。"),
		*Source.GetName(), Count, bHasPatrol ? TEXT("步行接入闭环") : TEXT("没有玩家路线，人员在落点待命"));
	return true;
}

void UCastleSupportTowerSubsystem::TickJourneys(FMassEntityManager& Manager, const float DeltaTime)
{
	if (Journeys.IsEmpty()) { return; }
	const auto* Nav = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	const auto Shared = Nav->AcquireSharedPath();
	UCastleBuildingTriggerSubsystem* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>();
	for (auto It = Journeys.CreateIterator(); It; ++It)
	{
		const FMassEntityHandle Entity = It.Key();
		if (!Manager.IsEntityActive(Entity)) { It.RemoveCurrent(); continue; }
		const auto& Unit = Manager.GetFragmentDataChecked<FCastleMassUnitFragment>(Entity);
		if (Unit.Health <= 0.0f) { It.RemoveCurrent(); continue; }
		FNPCJourney& Journey = It.Value();
		auto& Patrol = Manager.GetFragmentDataChecked<FCastleMassPatrolFragment>(Entity);
		// 显式停止、改道或重启共享巡逻已交还移动权限；不能在下一帧用旧集结路径盖回去。
		if (!Patrol.bSupportJourney) { It.RemoveCurrent(); continue; }
		auto& Velocity = Manager.GetFragmentDataChecked<FMassVelocityFragment>(Entity).Value;
		if (!Nav->IsNavigationReady() || Nav->NavigationRevision != Journey.Path.NavigationRevision
			|| !Triggers->IsTemporaryPathCurrent(Journey.Triggers)
			|| (Journey.JoinRevision != INDEX_NONE && (!Shared || !Shared->bValid || Shared->PathRevision != Journey.JoinRevision)))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 的人员 %d 调动因导航/建筑版本变更终止，人员原地停止。"),
				*GetPathNameSafe(Journey.Source.Get()), Entity.Index);
			Patrol.bSupportJourney = false;
			Patrol.bEnabled = false;
			Manager.GetFragmentDataChecked<FCastleMassMoveTargetFragment>(Entity) = {};
			Velocity = FVector::ZeroVector;
			It.RemoveCurrent();
			continue;
		}
		FCastleMassMovementSettings Settings = Manager.GetConstSharedFragmentDataChecked<FCastleMassMovementParameters>(Entity).Settings;
		Settings.MaxSpeed *= Unit.SupportMovementMultiplier;
		double Travel = 0.0;
		if (!CastlePatrolMovement::Advance(Journey.Path, Settings, Journey.Distance, DeltaTime, Journey.Speed, Travel, true))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 的人员 %d 梯子速度配置非法，调动失败并停在原地。"),
				*GetPathNameSafe(Journey.Source.Get()), Entity.Index);
			Patrol.bSupportJourney = false;
			Patrol.bEnabled = false;
			Manager.GetFragmentDataChecked<FCastleMassMoveTargetFragment>(Entity) = {};
			Velocity = FVector::ZeroVector;
			It.RemoveCurrent();
			continue;
		}
		const double Previous = Journey.Distance;
		Journey.Distance += Travel;
		FVector Position, Direction;
		Journey.Path.SamplePatrol(Journey.Distance, Position, Direction);
		FTransform& Transform = Manager.GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform();
		Transform.SetLocation(Position);
		Velocity = Direction * Journey.Speed;
		double LadderEnd;
		FVector LadderFacing;
		FVector Facing = Journey.Path.FindLadderRange(Journey.Distance, LadderEnd, LadderFacing) ? LadderFacing : FVector(Direction.X, Direction.Y, 0.0);
		if (!Facing.IsNearlyZero()) { Transform.SetRotation(Facing.ToOrientationQuat()); }
		TArray<FCastleBuildingEntryEvent> Entries;
		Journey.Triggers.ForEachEntry(Previous, Travel, [&](const FCastleBuildingEntryMarker& Marker)
		{
			Entries.Add({Entity, Marker.BuildingId, Marker.Location});
		});
		if (Journey.bRally && Previous < Journey.Path.WaypointDistances[1] && Journey.Distance >= Journey.Path.WaypointDistances[1])
		{
			UE_LOG(LogMoving_Castle, Display, TEXT("人员 %d 抵达空投塔 %s 的落点，开始返回原路线。"), Entity.Index, *GetNameSafe(Journey.Source.Get()));
		}
		if (!Entries.IsEmpty()) { Triggers->DispatchEntries(Journey.Triggers, Entries); }
		if (Journey.Distance + UE_DOUBLE_SMALL_NUMBER >= Journey.Path.Length)
		{
			Patrol.bSupportJourney = false;
			Velocity = FVector::ZeroVector;
			if (Journey.JoinRevision != INDEX_NONE)
			{
				Patrol.bEnabled = true;
				Patrol.bBlocked = false;
				Patrol.PathRevision = Shared->PathRevision;
				Patrol.RouteRevision = Shared->RouteRevision;
				Patrol.Distance = Journey.JoinDistance;
				Patrol.Leg = Shared->FindPatrolLeg(Patrol.Distance);
				Patrol.Speed = 0.0;
			}
			It.RemoveCurrent();
		}
	}
}
