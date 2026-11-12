#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleBuildingBase.h"
#include "CastleMassSpawner.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleNavigationSubsystem.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
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
#include "Units/BuildingInteraction/CastleBuildingTriggerSubsystem.h"
#include "Units/BuildingInteraction/Demo/CastleBuildingTriggerDemo.h"
#include "Units/BuildingInteraction/Mass/CastleMassBuildingTriggerProcessor.h"

namespace CastleBuildingTriggerIntegration
{
	/** 真实演示地图、异步 NavMesh、正式位移和正式接口，不用注入虚构的进入消息。 */
	class FRunCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRunCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - StartTime > 90) { Test->AddError(TEXT("建筑触发集成验证超时。")); return true; }
			if (!Demo)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType != EWorldType::Game) { continue; }
					for (TActorIterator<ACastleBuildingTriggerDemo> It(Context.World()); It; ++It) { Demo = *It; break; }
				}
				if (!Test->TestNotNull(TEXT("在建筑触发演示地图运行"), Demo)) { return true; }
				World = Demo->GetWorld();
				Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
				Triggers = World->GetSubsystem<UCastleBuildingTriggerSubsystem>();
				Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
				Manager = &World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
			}
			if (Demo->Phase < 0) { Test->AddError(TEXT("演示初始化失败，详见日志。")); return true; }
			if (Demo->Phase != 1) { return false; }
			if (Stage == 0)
			{
				Path = Navigation->AcquireSharedPath();
				const auto TriggerPath = Triggers->AcquireTriggerPath(Path);
				if (!Test->TestEqual(TEXT("实际放置双格楼和单格楼各有一个进入点"), TriggerPath->Entries.Num(), 2)) { return true; }
				Units->StopUnits(Demo->Spawner->SpawnedUnits);
				Manager->FlushCommands();
				// 显示批次证明可见性，额外纯模拟批次检验跨 Chunk 千人触发，避免测试受 GPU 预算影响。
				UMassEntityConfigAsset* Config = NewObject<UMassEntityConfigAsset>(World);
				auto* Trait = NewObject<UCastleMassNPCTrait>(Config);
				Trait->Movement.MaxSpeed = 400;
				Trait->Movement.Acceleration = 0;
				Config->GetMutableConfig().AddTrait(*Trait);
				Spawner = World->SpawnActorDeferred<ACastleMassSpawner>(ACastleMassSpawner::StaticClass(), FTransform::Identity);
				Spawner->Configuration = Config;
				Spawner->Count = 1024;
				Spawner->bFollowSharedPatrolRoute = true;
				Spawner->FinishSpawning(FTransform::Identity);
				if (!Test->TestEqual(TEXT("正式生成器创建 1024 名 NPC"), Spawner->SpawnedUnits.Num(), 1024)) { return true; }
				Manager->FlushCommands();
				BeforeA = Demo->Buildings[0]->BuildingEffect->TriggerCount;
				BeforeB = Demo->Buildings[1]->BuildingEffect->TriggerCount;
				Step<UCastleMassPatrolProcessor>(0);
				Step<UCastleMassBuildingTriggerProcessor>(0);
				CheckCounts(TEXT("沿路线出生不补发进入"), BeforeA, BeforeB);
				const int32 CompileCount = Triggers->CompilationCount;
				const int32 QueryCount = Navigation->PathQueryCount;
				Step<UCastleMassPatrolProcessor>(static_cast<float>(Path->Length * 2.01 / 400));
				Step<UCastleMassBuildingTriggerProcessor>(0);
				const int64 AfterA = Demo->Buildings[0]->BuildingEffect->TriggerCount;
				const int64 AfterB = Demo->Buildings[1]->BuildingEffect->TriggerCount;
				Test->TestTrue(TEXT("单帧跨两圈保留全部双格楼进入"), AfterA >= BeforeA + 2048);
				Test->TestTrue(TEXT("单帧跨两圈保留全部单格楼进入"), AfterB >= BeforeB + 2048);
				Test->TestTrue(TEXT("双格内部跨格没有额外倍增"), AfterA < BeforeA + 2070);
				Test->TestTrue(TEXT("相邻另一栋楼独立计数"), AfterB < BeforeB + 2070);
				Test->TestEqual(TEXT("大量 NPC 触发不重新编译区域"), Triggers->CompilationCount, CompileCount);
				Test->TestEqual(TEXT("进入建筑不额外寻路"), Navigation->PathQueryCount, QueryCount);
				Step<UCastleMassBuildingTriggerProcessor>(0);
				CheckCounts(TEXT("重复执行检测不重放旧移动"), AfterA, AfterB);
				Step<UCastleMassPatrolProcessor>(0);
				Step<UCastleMassBuildingTriggerProcessor>(0);
				CheckCounts(TEXT("原地停留不重复进入"), AfterA, AfterB);
				Units->StopUnits(Spawner->SpawnedUnits);
				Manager->FlushCommands();
				FText Failure;
				// 唯一保留的单位放在 A 内，用真实重绑定验证新旧路线坐标不能产生虚假穿楼。
				const auto Unit = Spawner->SpawnedUnits[0];
				Manager->GetFragmentDataChecked<FTransformFragment>(Unit.Entity).GetMutableTransform().SetLocation(FVector(300, 0, 5));
				Units->FollowSharedPatrolRoute({Unit}, Failure);
				Manager->FlushCommands();
				Navigation->BeginGeometryChange(Demo);
				Step<UCastleMassPatrolProcessor>(1);
				Step<UCastleMassBuildingTriggerProcessor>(0);
				CheckCounts(TEXT("建造失效暂停没有进入"), AfterA, AfterB);
				Navigation->EndGeometryChange(Demo);
				BeforeA = AfterA;
				BeforeB = AfterB;
				Stage = 1;
				return false;
			}
			if (!Navigation->IsNavigationReady() || !Navigation->GetSharedPath().bValid) { return false; }
			if (Stage == 1)
			{
				Step<UCastleMassPatrolProcessor>(0);
				Step<UCastleMassBuildingTriggerProcessor>(0);
				CheckCounts(TEXT("NavMesh 重建重绑定不会补发进入"), BeforeA, BeforeB);
				Test->TestTrue(TEXT("持有的旧路线保持不可变"), Path->bValid);
				FText Failure;
				Test->TestTrue(TEXT("改成整条位于 A 内的真实巡逻路线"), Navigation->RequestSharedPatrolRoute({FVector(0, 0, 5), FVector(700, 0, 5)}, Failure));
				Step<UCastleMassPatrolProcessor>(0);
				Step<UCastleMassBuildingTriggerProcessor>(0);
				const int64 ReboundA = Demo->Buildings[0]->BuildingEffect->TriggerCount;
				const int64 ReboundB = Demo->Buildings[1]->BuildingEffect->TriggerCount;
				Step<UCastleMassPatrolProcessor>(30);
				Step<UCastleMassBuildingTriggerProcessor>(0);
				CheckCounts(TEXT("整圈楼内运动和跨格不触发"), ReboundA, ReboundB);
				Test->TestTrue(TEXT("全在楼内的快照没有进入点"), Triggers->AcquireTriggerPath(Navigation->AcquireSharedPath())->Entries.IsEmpty());
				Spawner->Destroy();
				return true;
			}
			return true;
		}

	private:
		template<typename T>
		void Step(float DeltaTime)
		{
			T* Processor = NewObject<T>(World);
			Processor->CallInitialize(World, Manager->AsShared());
			UE::Mass::FProcessingContext Context(*Manager, DeltaTime);
			UE::Mass::Executor::Run(*Processor, Context);
		}
		void CheckCounts(const TCHAR* Message, int64 A, int64 B)
		{
			Test->TestEqual(FString(Message) + TEXT("：A"), Demo->Buildings[0]->BuildingEffect->TriggerCount, A);
			Test->TestEqual(FString(Message) + TEXT("：B"), Demo->Buildings[1]->BuildingEffect->TriggerCount, B);
		}
		FAutomationTestBase* Test;
		double StartTime = FPlatformTime::Seconds();
		int32 Stage = 0;
		int64 BeforeA = 0, BeforeB = 0;
		UWorld* World = nullptr;
		ACastleBuildingTriggerDemo* Demo = nullptr;
		ACastleMassSpawner* Spawner = nullptr;
		UCastleNavigationSubsystem* Navigation = nullptr;
		UCastleBuildingTriggerSubsystem* Triggers = nullptr;
		UCastleMassUnitSubsystem* Units = nullptr;
		FMassEntityManager* Manager = nullptr;
		TSharedPtr<const FCastleSharedNavigationPath, ESPMode::ThreadSafe> Path;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingTriggerIntegrationTest, "MovingCastle.BuildingTriggers.SharedPatrolIntegration",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingTriggerIntegrationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleBuildingTriggerIntegration::FRunCommand(this));
	return true;
}

#endif
