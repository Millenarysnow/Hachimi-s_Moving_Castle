#include "Mass/CastleMassPatrolProcessor.h"

#include "CastleMassFragments.h"
#include "CastleMassProcessors.h"
#include "CastleNavigationSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"

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
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddConstSharedRequirement<FCastleMassMovementParameters>();
}

void UCastleMassPatrolProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const auto Path = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->AcquireSharedPath();
	TAtomic<int32> NewlyBlocked(0);
	Query.ParallelForEachEntityChunk(Context, [Path, &NewlyBlocked](FMassExecutionContext& Chunk)
	{
		const double DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Patrols = Chunk.GetMutableFragmentView<FCastleMassPatrolFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto& Settings = Chunk.GetConstSharedFragment<FCastleMassMovementParameters>().Settings;
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			auto& Patrol = Patrols[It];
			if (!Patrol.bEnabled || Units[It].Health <= 0.0f) { continue; }
			FTransform& Transform = Transforms[It].GetMutableTransform();
			FVector& Velocity = Velocities[It].Value;
			Targets[It] = FCastleMassMoveTargetFragment();
			// 几何开始变化后立即停速；即使持有旧快照，也不允许沿过期的楼板继续走。
			if (!Path || !Path->bValid || !Path->bClosedLoop)
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
			const double PreviousSpeed = Patrol.Speed;
			Patrol.Speed = Settings.Acceleration > 0.0f
				? FMath::Min(static_cast<double>(Settings.MaxSpeed), Patrol.Speed + Settings.Acceleration * DeltaTime)
				: Settings.MaxSpeed;
			// 标量进度在拐角处也严格沿折线走，不把速度向量平滑成穿墙的弧线。
			const double Travel = Settings.Acceleration > 0.0f ? (PreviousSpeed + Patrol.Speed) * 0.5 * DeltaTime : Patrol.Speed * DeltaTime;
			if (Path->Length > UE_DOUBLE_SMALL_NUMBER)
			{
				const double Total = Patrol.Distance + Travel;
				Patrol.CompletedLaps += static_cast<int64>(Total / Path->Length);
				Patrol.Distance = FMath::Fmod(Total, Path->Length);
			}
			else { Patrol.Speed = 0.0; Patrol.Distance = 0.0; }
			FVector Location;
			FVector Direction;
			Path->SamplePatrol(Patrol.Distance, Location, Direction);
			Transform.SetLocation(Location);
			Velocity = Direction * Patrol.Speed;
			Patrol.Leg = Path->FindPatrolLeg(Patrol.Distance);
			if (Settings.bKeepUpright) { Direction.Z = 0.0; }
			if (!Direction.IsNearlyZero() && Settings.TurnRate > 0.0f)
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
}
