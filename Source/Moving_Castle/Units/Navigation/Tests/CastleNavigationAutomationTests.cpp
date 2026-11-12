#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleNavigationSubsystem.h"
#include "Demo/CastleNavigationDemo.h"
#include "AssemblyFlightComponent.h"
#include "BuildingMergeComponent.h"
#include "CastleBuildingTestUtilities.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleNavigationMissingDataTest, "MovingCastle.Navigation.InputValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleNavigationMissingDataTest::RunTest(const FString& Parameters)
{
	// 没有导航的纯逻辑世界仍可运行原建筑系统；明确请求寻路时报告配置问题。
	const UWorld::InitializationValues Settings = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	UCastleNavigationSubsystem* Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
	FText Failure;
	TestFalse(TEXT("缺少 NavMesh 时拒绝共享寻路"), Navigation->RequestSharedPath(FVector::ZeroVector, FVector(100.0, 0.0, 0.0), Failure));
	TestFalse(TEXT("失败原因可读取"), Failure.IsEmpty());
	TestFalse(TEXT("缺少 NavMesh 时拒绝刷新"), Navigation->RequestNavigationRefresh(Failure));
	TestFalse(TEXT("没有生成虚假可达路线"), Navigation->GetSharedPath().bValid);
	TestEqual(TEXT("失败请求不执行任何寻路"), Navigation->PathQueryCount, 0);
	TestFalse(TEXT("稳定且未配置导航的世界不持续 Tick"), Navigation->IsTickable());
	World->DestroyWorld(false);
	return true;
}

namespace CastleNavigationTests
{
	/** 每次 Update 由引擎推进真正的导航、动画和地图 Tick，不手工伪造构建完成事件。 */
	class FSharedNavigationCommand : public IAutomationLatentCommand
	{
	public:
		explicit FSharedNavigationCommand(FAutomationTestBase* InTest) : Test(InTest), Deadline(FPlatformTime::Seconds() + 40.0) {}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("导航演示测试超时，NavMesh 或动画未完成。")); return true; }
			if (!Demo)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType != EWorldType::Game) { continue; }
					for (TActorIterator<ACastleNavigationDemo> It(Context.World()); It; ++It) { Demo = *It; break; }
				}
				if (!Test->TestNotNull(TEXT("已打开原生楼梯/合并导航演示"), Demo)) { return true; }
				Navigation = Demo->GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
			}
			if (Demo->Phase < 0) { Test->AddError(TEXT("演示提前失败，请检查导航配置及日志。")); return true; }
			if (Stage == 0)
			{
				if (Demo->Phase != 1) { return false; }
				Test->TestEqual(TEXT("两段飞入完成只提交一轮刷新"), Navigation->RefreshCount, 1);
				Test->TestEqual(TEXT("共享路线只寻路一次"), Navigation->PathQueryCount, 1);
				const auto First = Navigation->AcquireSharedPath();
				if (!Test->TestTrue(TEXT("发布完整有效的共享路线"), First && First->bValid)) { return true; }
				const auto Second = Navigation->AcquireSharedPath();
				Test->TestTrue(TEXT("多个消费者取得同一份只读数据"), First.Get() == Second.Get());
				Test->TestTrue(TEXT("路线沿楼梯抵达上层"), First->Points.Last().Z - First->Points[0].Z > 550.0);
				Test->TestTrue(TEXT("路线绕墙，有实际转折"), First->Points.Num() > 2);
				FText Failure;
				const FTransform Transform = Demo->GetActorTransform();
				Start = Transform.TransformPosition(FVector(-1200.0, 0.0, 5.0));
				Goal = Transform.TransformPosition(FVector(2000.0, 0.0, 605.0));
				for (int32 Index = 0; Index < 1000; ++Index) { Navigation->RequestSharedPath(Start, Goal, Failure); }
				Test->TestEqual(TEXT("1000 个相同请求复用已有结果"), Navigation->PathQueryCount, 1);
				OldPath = First;
				IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("Navigation")), true);
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Navigation/InitialPath.png"), true, false);
				NextCheck = FPlatformTime::Seconds() + 0.25;
				Stage = 10;
				return false;
			}
			if (Stage == 10)
			{
				if (FPlatformTime::Seconds() < NextCheck) { return false; }
				if (!Test->TestTrue(TEXT("开始真实墙体合并"), Demo->StartMerge())) { return true; }
				Test->TestTrue(TEXT("合并开始使当前路线失效"), !Navigation->GetSharedPath().bValid);
				Test->TestTrue(TEXT("旧快照未被原地修改"), OldPath->bValid);
				Stage = 1;
				return false;
			}
			if (Stage == 1)
			{
				if (Demo->Phase == 2)
				{
					if (Navigation->ActiveChangeCount > 0)
					{
						Test->TestEqual(TEXT("合并动画中不提交刷新"), Navigation->RefreshCount, 1);
						Test->TestTrue(TEXT("合并期间生成确实锁定"), FNavigationSystem::GetCurrent<UNavigationSystemV1>(Demo->GetWorld())->IsNavigationBuildingLocked());
					}
					return false;
				}
				if (Demo->Phase != 3) { return false; }
				Test->TestEqual(TEXT("合并结束只增加一轮刷新"), Navigation->RefreshCount, 2);
				Test->TestEqual(TEXT("导航改变只统一重算一次路线"), Navigation->PathQueryCount, 2);
				const auto Path = Navigation->AcquireSharedPath();
				Test->TestTrue(TEXT("新路线与旧快照使用不同版本"), Path->NavigationRevision > OldPath->NavigationRevision && Path.Get() != OldPath.Get());
				const auto MinY = [](const TArray<FVector>& Points)
				{
					double Result = 0.0;
					for (const FVector& Point : Points) { Result = FMath::Min(Result, Point.Y); }
					return Result;
				};
				const auto MaxY = [](const TArray<FVector>& Points)
				{
					double Result = 0.0;
					for (const FVector& Point : Points) { Result = FMath::Max(Result, Point.Y); }
					return Result;
				};
				Test->TestTrue(TEXT("初始路线从墙的负 Y 侧绕行"), MinY(OldPath->Points) < -200.0);
				Test->TestTrue(TEXT("合并后从新开口的正 Y 侧绕行"), MaxY(Path->Points) > 200.0);
				IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("Navigation")), true);
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Navigation/SharedPath.png"), true, false);
				NextCheck = FPlatformTime::Seconds() + 0.5;
				Stage = 2;
				return false;
			}
			if (Stage == 2)
			{
				if (FPlatformTime::Seconds() < NextCheck) { return false; }
				Test->TestEqual(TEXT("静止后不持续刷新"), Navigation->RefreshCount, 2);
				Test->TestFalse(TEXT("静止后导航调度停止 Tick"), Navigation->IsTickable());
				// 使用同一个来源嵌套，再取消真实的飞入和合并；所有结束必须正确配对。
				Navigation->BeginGeometryChange(Demo);
				Navigation->BeginGeometryChange(Demo);
				Navigation->EndGeometryChange(Demo);
				Test->TestEqual(TEXT("嵌套结束一次仍保持暂停"), Navigation->ActiveChangeCount, 1);
				AActor* Source = SpawnPart(TEXT("CancelSource"));
				AActor* Target = SpawnPart(TEXT("CancelTarget"));
				Target->SetActorLocation(Target->GetActorLocation() + FVector(300.0, 0.0, 0.0));
				UAssemblyFlightComponent* Flight = NewObject<UAssemblyFlightComponent>(Source);
				Flight->bAutoPlay = Flight->bShowStatusOnScreen = false;
				Source->AddInstanceComponent(Flight);
				Flight->RegisterComponent();
				Test->TestTrue(TEXT("取消用飞入可启动"), Flight->StartAssembly());
				Flight->ResetAssembly();
				UBuildingMergeComponent* Merge = NewObject<UBuildingMergeComponent>(Target);
				Merge->bShowStatusOnScreen = false;
				Target->AddInstanceComponent(Merge);
				Merge->RegisterComponent();
				Test->TestTrue(TEXT("取消用合并可启动"), Merge->StartChange({Source}));
				Test->TestTrue(TEXT("取消前合并确实正在播放"), Merge->bIsPlaying);
				Merge->ResetMerge();
				Source->Destroy();
				Target->Destroy();
				Navigation->EndGeometryChange(Demo);
				Test->TestEqual(TEXT("取消和销毁后没有残留暂停来源"), Navigation->ActiveChangeCount, 0);
				Stage = 3;
				return false;
			}
			if (Stage == 3)
			{
				if (!Navigation->IsNavigationReady() || !Navigation->GetSharedPath().bValid) { return false; }
				Test->TestEqual(TEXT("所有取消操作合并成一次刷新"), Navigation->RefreshCount, 3);
				Test->TestEqual(TEXT("取消后的路线只重算一次"), Navigation->PathQueryCount, 3);
				Test->AddExpectedMessagePlain(TEXT("共享路线起点或终点附近没有可行走 NavMesh"), ELogVerbosity::Warning);
				FText Failure;
				Navigation->RequestSharedPath(Start, FVector(50000.0, 50000.0, 0.0), Failure);
				Test->TestFalse(TEXT("不可投影的终点不发布部分或旧路线"), Navigation->GetSharedPath().bValid);
				Test->TestFalse(TEXT("不可达原因可读取"), Navigation->LastFailure.IsEmpty());
				Navigation->RequestSharedPath(Start, Goal, Failure);
				Test->TestTrue(TEXT("修改为有效起终点后可重新发布"), Navigation->GetSharedPath().bValid);
				// 再走正式棋盘放置：两座建筑飞入完成后同步触发合并，中间不能解锁并重建。
				if (!BeginBoardPlacement()) { return true; }
				Stage = 4;
				return false;
			}
			if (Stage == 4)
			{
				if (Navigation->ActiveChangeCount > 0)
				{
					Test->TestEqual(TEXT("棋盘飞入及后续合并期间不额外刷新"), Navigation->RefreshCount, 3);
					return false;
				}
				if (!Navigation->IsNavigationReady()) { return false; }
				Test->TestEqual(TEXT("正式放置到合并整轮只刷新一次"), Navigation->RefreshCount, 4);
				ACastleBuildingBase* Building = CastleBuildingTests::FindPlacedLevel(Board, 2);
				if (!Test->TestNotNull(TEXT("正式棋盘已完成二级合并"), Building)) { return true; }
				FText Failure;
				Test->TestTrue(TEXT("正式移除入口可删除合并结果"), Board->RemoveBuilding(Building, Failure));
				Stage = 5;
				return false;
			}
			if (Stage == 5)
			{
				if (!Navigation->IsNavigationReady()) { return false; }
				Test->TestEqual(TEXT("移除建筑只再刷新一次"), Navigation->RefreshCount, 5);
				Test->TestEqual(TEXT("正式放置/合并/移除均各重算共享路线一次"), Navigation->PathQueryCount, 6);
				Test->TestTrue(TEXT("其他建筑变化后楼梯共享路线仍有效"), Navigation->GetSharedPath().bValid);
				Test->TestFalse(TEXT("所有变化完成后再次停止调度"), Navigation->IsTickable());
				Board->Destroy();
				return true;
			}
			return true;
		}
	private:
		/** 在导航演示范围外创建小棋盘，测试业务信号串接，不覆盖演示道路。 */
		bool BeginBoardPlacement()
		{
			UWorld* World = Demo->GetWorld();
			// 此集成测试只检查业务变化调度，显式提供模型，避免重新依赖生产占位行为。
			TestModels = MakeUnique<CastleBuildingTests::FScopedNativeBuildingModels>(World);
			const FTransform Transform(FVector(6000.0, 6000.0, 0.0));
			Board = World->SpawnActorDeferred<ACastleBoard>(ACastleBoard::StaticClass(), Transform);
			if (!Test->TestNotNull(TEXT("创建业务集成用棋盘"), Board)) { return false; }
			Board->bGenerateGroundOnBeginPlay = false;
			Board->GridSize = FIntVector(2, 1, 1);
			UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
			Board->BuildingDefinitions = {Definition};
			Board->FinishSpawning(Transform);
			FCastleGroundGenerationResult Ground;
			for (int32 X = 0; X < 2; ++X)
			{
				FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
				Cell.Coordinate = FIntVector(X, 0, 0);
				Cell.Color = ECastleTileColor::Red;
				Cell.Number = 3;
			}
			FText Failure;
			if (!Test->TestTrue(TEXT("正式配置棋盘地面"), Board->ApplyGroundGenerationResult(Ground, Failure))) { return false; }
			FCastleBuildingDescriptor Card;
			Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Failure);
			if (!Test->TestNotNull(TEXT("正式放置第一座建筑"), Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Failure))) { return false; }
			return Test->TestNotNull(TEXT("正式放置第二座并等待自动合并"), Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Failure));
		}

		/** 测试用网格在导航范围外，不改变已验证的楼梯和绕墙路径。 */
		AActor* SpawnPart(const TCHAR* Name)
		{
			AActor* Actor = Demo->GetWorld()->SpawnActor<AActor>();
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor, FName(Name));
			Actor->SetRootComponent(Mesh);
			Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetCollisionProfileName(TEXT("BlockAll"));
			Actor->AddInstanceComponent(Mesh);
			Mesh->RegisterComponent();
			Actor->SetActorLocation(FVector(5000.0, 5000.0, 0.0));
			return Actor;
		}
		FAutomationTestBase* Test;
		ACastleNavigationDemo* Demo = nullptr;
		UCastleNavigationSubsystem* Navigation = nullptr;
		ACastleBoard* Board = nullptr;
		TUniquePtr<CastleBuildingTests::FScopedNativeBuildingModels> TestModels;
		TSharedPtr<const FCastleSharedNavigationPath, ESPMode::ThreadSafe> OldPath;
		FVector Start;
		FVector Goal;
		int32 Stage = 0;
		double Deadline;
		double NextCheck = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleNavigationRuntimeTest, "MovingCastle.NavigationDemo.SharedPath",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleNavigationRuntimeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleNavigationTests::FSharedNavigationCommand(this));
	return true;
}

#endif
