#include "Mass/CastleMassPatrolProcessor.h"

#include "CastleMassFragments.h"
#include "CastleMassProcessors.h"
#include "CastleNavigationSubsystem.h"
#include "CastleTowerDispatchSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Items/CastleBadgeRuntimeSubsystem.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Mass/CastlePatrolMovement.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"
#include "Units/BuildingInteraction/Mass/CastleMassBuildingTraversalFragment.h"

namespace
{
	bool AdvancePatrolWithLateAdvantage(const FCastleSharedNavigationPath& Path, const FCastleMassMovementSettings& BaseSettings,
		double Distance, double Time, const int64 CompletedLaps, const bool bStopAtEnd, double& InOutSpeed, double& OutTravel)
	{
		OutTravel = 0.0;
		for (int64 Lap = CompletedLaps; Time > UE_DOUBLE_SMALL_NUMBER; ++Lap)
		{
			FCastleMassMovementSettings Settings = BaseSettings;
			Settings.MaxSpeed *= UCastleBadgeSubsystem::CalculateLateAdvantageSpeedMultiplier(Lap);
			if (!FMath::IsFinite(Settings.MaxSpeed))
			{
				InOutSpeed = 0.0;
				return false;
			}
			const double InitialSpeed = InOutSpeed;
			double LapTravel = 0.0;
			// 每段最多到门 B 或闭环末端。未跨圈的正常帧仍只调用一次原路线积分。
			const bool bValid = CastlePatrolMovement::Advance(Path, Settings, Distance, Time, InOutSpeed, LapTravel, true);
			OutTravel += LapTravel;
			const double ToEnd = Path.Length - Distance;
			if (!bValid || bStopAtEnd || LapTravel < ToEnd || Path.Length <= UE_DOUBLE_SMALL_NUMBER) { return bValid; }

			// 原积分同时处理梯子、道路和加速。只在实际跨圈的帧查找抵达时刻，保留终点前的速度，
			// 用同一帧剩余时间按新圈倍率继续，避免一整个大帧都误用首圈倍率。
			double Lower = 0.0;
			double Upper = Time;
			InOutSpeed = InitialSpeed;
			while (Upper - Lower > UE_DOUBLE_SMALL_NUMBER)
			{
				const double Mid = Lower + (Upper - Lower) * 0.5;
				if (Mid == Lower || Mid == Upper) { break; }
				double MidSpeed = InitialSpeed;
				double MidTravel = 0.0;
				if (!CastlePatrolMovement::Advance(Path, Settings, Distance, Mid, MidSpeed, MidTravel, true)) { return false; }
				if (MidTravel >= ToEnd)
				{
					Upper = Mid;
				}
				else
				{
					Lower = Mid;
					InOutSpeed = MidSpeed;
				}
			}
			Distance = 0.0;
			Time -= Upper;
		}
		return true;
	}
}

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
	const auto* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	const float LadderSpeedMultiplier = Badges ? Badges->GetQuickLadderSpeedMultiplier() : 1.0f;
	const bool bHasLateAdvantage = Badges && Badges->HasBadge(ECastleBadgeType::LateAdvantage);
	auto* Runtime = GetWorld()->GetSubsystem<UCastleBadgeRuntimeSubsystem>();
	const double Now = GetWorld()->GetTimeSeconds();
	const auto MovementBonuses = Runtime ? Runtime->CaptureNPCMovement(Now) : TMap<FMassEntityHandle, FCastleBadgeNPCMovement>();
	TAtomic<int64> NewFormalLaps(0);
	TAtomic<int32> NewlyBlocked(0);
	TAtomic<int32> InvalidLadderSettings(0);
	TAtomic<int32> InvalidSpeedSettings(0);
	Query.ParallelForEachEntityChunk(Context, [Path, bAutoDispatch, LadderSpeedMultiplier, bHasLateAdvantage, Now,
		&MovementBonuses, &NewFormalLaps, &NewlyBlocked, &InvalidLadderSettings, &InvalidSpeedSettings](FMassExecutionContext& Chunk)
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
			EffectiveSettings.LadderSpeedMultiplier *= LadderSpeedMultiplier;
			if (const FCastleBadgeNPCMovement* Buffs = MovementBonuses.Find(Chunk.GetEntity(It)))
			{
				EffectiveSettings.MaxSpeed *= Buffs->At(Now);
			}
			if (!FMath::IsFinite(EffectiveSettings.MaxSpeed))
			{
				++InvalidSpeedSettings;
				Patrol.Speed = 0.0;
				Velocity = FVector::ZeroVector;
				continue;
			}
			const bool bFinishAtEntrance = Patrol.bTowerDispatched && (!bAutoDispatch || Patrol.bTemporaryOneLap);
			const bool bAdvanced = bHasLateAdvantage
				? AdvancePatrolWithLateAdvantage(*Path, EffectiveSettings, Patrol.Distance, DeltaTime, Patrol.CompletedLaps,
					bFinishAtEntrance, Patrol.Speed, Travel)
				: CastlePatrolMovement::Advance(*Path, EffectiveSettings, Patrol.Distance, DeltaTime, Patrol.Speed, Travel, bFinishAtEntrance);
			if (!bAdvanced)
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
				int64 LapsFinished = 0;
				if (bFinishAtEntrance && Total >= Path->Length - UE_DOUBLE_SMALL_NUMBER)
				{
					// 只写值状态，绝不在并行查询中改变实体组成；调度器会在安全边界确认销毁。
					Patrol.Distance = Path->Length;
					Patrol.Speed = 0.0;
					Patrol.bReachedDispatchEntrance = true;
					LapsFinished = 1;
				}
				else
				{
					LapsFinished = static_cast<int64>(Total / Path->Length);
					Patrol.Distance = FMath::Fmod(Total, Path->Length);
				}
				Patrol.CompletedLaps += LapsFinished;
				if (Patrol.bTowerDispatched && !Patrol.bTemporaryOneLap && LapsFinished > 0)
				{
					NewFormalLaps += LapsFinished;
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
	if (Runtime && NewFormalLaps.Load() > 0)
	{
		// 即使某名士兵稍后在同帧销毁，其本帧已跨过门 B 的圈数也已安全提交。
		Runtime->RecordCompletedPatrolLaps(NewFormalLaps.Load());
	}
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
	if (InvalidSpeedSettings.Load() > 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("NPC 巡逻：%d 个单位的有效最大移速溢出，单位已停止。"), InvalidSpeedSettings.Load());
	}
}
