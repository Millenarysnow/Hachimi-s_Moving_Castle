#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleNavigationSubsystem.h"
#include "CastleMassSpawner.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Demo/CastleNavigationDemo.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Mass/CastleMassPatrolProcessor.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/AutomationTest.h"

namespace CastleMassPatrolTests
{
	/** 在真实地图和异步 NavMesh 上跑；只加速 Mass 时间步，不伪造寻路结果或导航完成事件。 */
	class FSharedLoopCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSharedLoopCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - StartTime > 60.0) { Test->AddError(TEXT("共享闭环验证超时。")); return true; }
			if (!Demo)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType != EWorldType::Game) { continue; }
					for (TActorIterator<ACastleNavigationDemo> It(Context.World()); It; ++It) { Demo = *It; break; }
				}
				if (!Test->TestNotNull(TEXT("在导航楼梯演示地图中运行"), Demo)) { return true; }
				World = Demo->GetWorld();
				Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
				Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
				Manager = &World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
			}
			if (Stage == 0)
			{
				if (Demo->Phase != 1) { return false; }
				InitialQueries = Navigation->PathQueryCount;
				const FTransform Transform = Demo->GetActorTransform();
				Waypoints = {Transform.TransformPosition(FVector(-1200, 0, 5)), Transform.TransformPosition(FVector(2000, 0, 605)), Transform.TransformPosition(FVector(-1100, -330, 5))};
				FText Failure;
				if (!Test->TestTrue(TEXT("一次提交三个玩家路径点"), Navigation->RequestSharedPatrolRoute(Waypoints, Failure))) { Test->AddError(Failure.ToString()); return true; }
				Path = Navigation->AcquireSharedPath();
				if (!Test->TestTrue(TEXT("三段全部完成后发布完整闭环"), Path && Path->bValid && Path->bClosedLoop)) { Test->AddError(Navigation->LastFailure.ToString()); return true; }
				Test->TestEqual(TEXT("三个路径点恰好求解三段，包含末点回首点"), Navigation->PathQueryCount, InitialQueries + 3);
				Test->TestEqual(TEXT("玩家路径点没有被求解器转折点替代"), Path->Waypoints.Num(), 3);
				Test->TestEqual(TEXT("首末路线采样形成闭环"), Path->PatrolPoints.Last(), Path->PatrolPoints[0]);
				Test->TestEqual(TEXT("路段距离含完整闭合终点"), Path->WaypointDistances.Num(), 4);
				Test->TestTrue(TEXT("楼梯额外采样供所有单位共享"), Path->PatrolPoints.Num() > Path->Points.Num());
				Test->TestTrue(TEXT("共享采样保留上层高度"), Path->PatrolPoints.ContainsByPredicate([](FVector Point) { return Point.Z > 550.0; }));
				for (int32 Index = 0; Index < 1000; ++Index) { Navigation->RequestSharedPatrolRoute(Waypoints, Failure); }
				Test->TestEqual(TEXT("相同整组请求不重算"), Navigation->PathQueryCount, InitialQueries + 3);
				Test->TestTrue(TEXT("所有消费者持有同一快照"), Path.Get() == Navigation->AcquireSharedPath().Get());
				// 使用实际生成器验证等待/批量生成入口，模拟配置让测试与渲染和美术资源无关。
				Config = NewObject<UMassEntityConfigAsset>(World);
				auto* Trait = NewObject<UCastleMassNPCTrait>(Config);
				Trait->Movement.Acceleration = 0.0f;
				Trait->Movement.MaxSpeed = 400.0f;
				Config->GetMutableConfig().AddTrait(*Trait);
				Spawner = World->SpawnActorDeferred<ACastleMassSpawner>(ACastleMassSpawner::StaticClass(), FTransform::Identity);
				Spawner->Configuration = Config;
				Spawner->Count = 1024;
				Spawner->bFollowSharedPatrolRoute = true;
				Spawner->FinishSpawning(FTransform::Identity);
				if (!Test->TestEqual(TEXT("沿共享路线批量生成一千零二十四个 NPC"), Spawner->SpawnedUnits.Num(), 1024)) { return true; }
				Manager->FlushCommands();
				Processor = NewObject<UCastleMassPatrolProcessor>(World);
				Processor->CallInitialize(World, Manager->AsShared());
				Step(0.0f);
				// 一步跨越多个转折和两整圈，检查大帧间隔也不会走直线抄近道。
				Step(static_cast<float>(Path->Length * 2.2 / Trait->Movement.MaxSpeed));
				for (int32 Index : {0, 511, 1023})
				{
					const auto& Handle = Spawner->SpawnedUnits[Index];
					const auto& Patrol = Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Handle.Entity);
					FVector Expected, Direction;
					Path->SamplePatrol(Patrol.Distance, Expected, Direction);
					Test->TestTrue(TEXT("跨 Chunk 实体至少完成两圈"), Patrol.CompletedLaps >= 2);
					Test->TestFalse(TEXT("沿路线生成的实体没有绑定失败"), Patrol.bBlocked);
					Test->TestTrue(TEXT("位移严格取自共享路线"), Manager->GetFragmentDataChecked<FTransformFragment>(Handle.Entity).GetTransform().GetLocation().Equals(Expected));
				}
				Test->TestEqual(TEXT("所有单位移动和绕圈都不再寻路"), Navigation->PathQueryCount, InitialQueries + 3);
				// 用普通地形更新信号验证失效暂停与恢复，保持几何不变以核对进度没有跳段。
				Navigation->BeginGeometryChange(Demo);
				BeforePause = Manager->GetFragmentDataChecked<FTransformFragment>(Spawner->SpawnedUnits[0].Entity).GetTransform().GetLocation();
				Step(1.0f);
				Test->TestTrue(TEXT("失效期间不使用持有的旧路线"), Manager->GetFragmentDataChecked<FTransformFragment>(Spawner->SpawnedUnits[0].Entity).GetTransform().GetLocation().Equals(BeforePause));
				Test->TestTrue(TEXT("旧快照仍保持不可变"), Path->bValid);
				Navigation->EndGeometryChange(Demo);
				Stage = 1;
				return false;
			}
			if (Stage == 1)
			{
				if (!Navigation->IsNavigationReady() || !Navigation->GetSharedPath().bValid) { return false; }
				Test->TestEqual(TEXT("网格更新后一次算完整三段，次数与实体数量无关"), Navigation->PathQueryCount, InitialQueries + 6);
				Step(0.1f);
				Test->TestFalse(TEXT("重建后原路段恢复巡逻"), Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Spawner->SpawnedUnits[0].Entity).bBlocked);
				Test->TestFalse(TEXT("稳定后导航调度不持续 Tick"), Navigation->IsTickable());
				FText Failure;
				// 其他 NPC 暂停，独立检查显式命令与单点巡逻，避免旧位置被新单点路线误绑定。
				Units->StopUnits(Spawner->SpawnedUnits);
				Manager->FlushCommands();
				Test->TestFalse(TEXT("StopUnits 解除巡逻"), Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Spawner->SpawnedUnits[0].Entity).bEnabled);
				const int32 Queries = Navigation->PathQueryCount;
				Test->TestTrue(TEXT("一个点可作为共享待命路线"), Navigation->RequestSharedPatrolRoute({Waypoints[0]}, Failure));
				Test->TestTrue(TEXT("单点路线有效"), Navigation->GetSharedPath().bValid);
				Test->TestEqual(TEXT("单点不请求无意义的自身到自身寻路"), Navigation->PathQueryCount, Queries);
				const auto Handle = Spawner->SpawnedUnits[0];
				Manager->GetFragmentDataChecked<FTransformFragment>(Handle.Entity).GetMutableTransform().SetLocation(Navigation->GetSharedPath().PatrolPoints[0]);
				Units->FollowSharedPatrolRoute({Handle}, Failure);
				Manager->FlushCommands();
				Step(10.0f);
				Test->TestEqual(TEXT("单点巡逻保持零进度"), Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Handle.Entity).Distance, 0.0);
				Test->AddExpectedMessagePlain(TEXT("共享路线起点或终点附近没有可行走 NavMesh"), ELogVerbosity::Warning);
				Navigation->RequestSharedPatrolRoute({Waypoints[0], Waypoints[1], FVector(50000, 50000, 0)}, Failure);
				Test->TestFalse(TEXT("任一路段端点无效都不发布半条闭环"), Navigation->GetSharedPath().bValid);
				Test->TestTrue(TEXT("失败结果没有残留已计算采样"), Navigation->GetSharedPath().PatrolPoints.IsEmpty());
				Test->TestFalse(TEXT("相邻重合路径点明确拒绝"), Navigation->RequestSharedPatrolRoute({Waypoints[0], Waypoints[0]}, Failure));
				Test->TestFalse(TEXT("空路径点明确拒绝"), Navigation->RequestSharedPatrolRoute({}, Failure));
				Navigation->ClearSharedRoute();
				Test->TestFalse(TEXT("清除后路线立即失效"), Navigation->GetSharedPath().bValid);
				Spawner->Destroy();
				return true;
			}
			return true;
		}
	private:
		void Step(float DeltaTime)
		{
			UE::Mass::FProcessingContext Context(*Manager, DeltaTime);
			UE::Mass::Executor::Run(*Processor, Context);
		}
		FAutomationTestBase* Test;
		double StartTime = FPlatformTime::Seconds();
		int32 Stage = 0;
		int32 InitialQueries = 0;
		UWorld* World = nullptr;
		ACastleNavigationDemo* Demo = nullptr;
		UCastleNavigationSubsystem* Navigation = nullptr;
		UCastleMassUnitSubsystem* Units = nullptr;
		FMassEntityManager* Manager = nullptr;
		UMassEntityConfigAsset* Config = nullptr;
		ACastleMassSpawner* Spawner = nullptr;
		UCastleMassPatrolProcessor* Processor = nullptr;
		TArray<FVector> Waypoints;
		FVector BeforePause;
		TSharedPtr<const FCastleSharedNavigationPath, ESPMode::ThreadSafe> Path;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassSharedPatrolTest, "MovingCastle.Patrol.SharedLoop",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassSharedPatrolTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleMassPatrolTests::FSharedLoopCommand(this));
	return true;
}

#endif
