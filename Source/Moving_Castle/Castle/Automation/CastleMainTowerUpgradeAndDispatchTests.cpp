// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/Components/CastleMainTowerComponent.h"
#include "Castle/Actors/CastleBoard.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Castle/PCG/CastleBuildingPCGRule.h"
#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "CastleNavigationSubsystem.h"
#include "CastleMassProcessors.h"
#include "CastleMassTraits.h"
#include "CastleTowerDispatchConfigDataAsset.h"
#include "CastleTowerDispatchSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Interaction/CastleTowerDispatchDoors.h"
#include "MassCommonFragments.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Mass/CastleMassPatrolProcessor.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMainTowerBalloonHealthTest, "MovingCastle.MainTower.BalloonHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMainTowerBalloonHealthTest::RunTest(const FString& Parameters)
{
	UCastleMainTowerComponent* Tower = NewObject<UCastleMainTowerComponent>();
	const auto CheckBalloons = [&](const TCHAR* Phase, const int32 ExpectedLive, const int32 ExpectedMax)
	{
		int32 Live, Max;
		Tower->GetBalloonCounts(Live, Max);
		TestEqual(FString::Printf(TEXT("%s时存活气球"), Phase), Live, ExpectedLive);
		TestEqual(FString::Printf(TEXT("%s时气球上限"), Phase), Max, ExpectedMax);
	};
	TestTrue(TEXT("初始化默认生命"), Tower->InitializeHealth());
	CheckBalloons(TEXT("初始满血"), 5, 5);
	TestTrue(TEXT("受击仍按浮点生命扣减"), Tower->ApplyDamage(400.0f));
	TestEqual(TEXT("受击生命为 600"), Tower->CurrentHealth, 600.0f);
	CheckBalloons(TEXT("受击"), 3, 5);
	TestTrue(TEXT("恢复 100 点生命"), Tower->ApplyHealing(100.0f));
	CheckBalloons(TEXT("回血"), 4, 5);
	TestTrue(TEXT("主塔保留微量生命"), Tower->ApplyDamage(699.75f));
	CheckBalloons(TEXT("微量残血"), 1, 5);
	TestTrue(TEXT("归零"), Tower->ApplyDamage(0.25f));
	CheckBalloons(TEXT("生命归零"), 0, 5);

	// 用新一局模拟升级与回收，避免把升级误当成死亡后的恢复入口。
	TestTrue(TEXT("开启独立升级场景"), Tower->InitializeHealth());
	TestTrue(TEXT("升级前受伤"), Tower->ApplyDamage(200.0f));
	FText Reason;
	TestTrue(TEXT("升级至十球时补满"), Tower->SetTowerBalloonTier(10, 2000.0f, true, Reason));
	TestEqual(TEXT("升级得到完整新生命"), Tower->CurrentHealth, 2000.0f);
	CheckBalloons(TEXT("升级补满"), 10, 10);
	TestTrue(TEXT("可以消耗九个气球"), Tower->TryConsumeBalloons(9, Reason));
	CheckBalloons(TEXT("回收九个"), 1, 10);
	AddExpectedError(TEXT("需要保留至少一个存活气球"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("不能消耗最后一个气球"), Tower->TryConsumeBalloons(1, Reason));
	TestEqual(TEXT("拒绝回收不扣血"), Tower->CurrentHealth, 200.0f);
	TestTrue(TEXT("徽章倍率可使用默认等比生命缩放"), Tower->SetMaxHealthMultiplier(1.2f));
	TestEqual(TEXT("徽章最大生命"), Tower->MaxHealth, 2400.0f);
	CheckBalloons(TEXT("等比应用徽章"), 1, 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMainTowerSideGrowthTest, "MovingCastle.MainTower.SideGridGrowth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMainTowerSideGrowthTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("建立侧格扩层测试 World"), World)) { return false; }
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	AActor* Tower = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("创建测试塔身"), Tower)) { return false; }
	USceneComponent* Root = NewObject<USceneComponent>(Tower);
	Tower->SetRootComponent(Root);
	Root->RegisterComponent();
	UCastleTowerSideGridComponent* Grid = NewObject<UCastleTowerSideGridComponent>(Tower);
	Grid->RegisterComponent();
	FText Reason;
	if (!TestTrue(TEXT("生成开局三层"), Grid->Initialize(FIntVector::ZeroValue, 3, 500.0f, 560.0f, 290.0f, 17, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestEqual(TEXT("开局四面各三格"), Grid->GetCells().Num(), 12);
	for (const int32 Target : {5, 7, 9})
	{
		const TArray<FCastleTowerSideCell> Old = Grid->GetCells();
		if (!TestTrue(TEXT("成功追加两层"), Grid->ExpandToLayerCount(Target, Reason))) { return false; }
		TestEqual(TEXT("四侧都有新层"), Grid->GetCells().Num(), Target * 4);
		for (const FCastleTowerSideCell& Cell : Old)
		{
			const FCastleTowerSideCell* Current = Grid->FindCell(Cell.TowerCell, Cell.Face);
			if (!TestNotNull(TEXT("旧层格仍存在"), Current)) { return false; }
			TestTrue(TEXT("旧颜色保留"), Current->Color == Cell.Color);
			TestEqual(TEXT("旧数字保留"), Current->Number, Cell.Number);
			TestTrue(TEXT("旧面板实例保留"), Current->Panel == Cell.Panel);
		}
	}
	AddExpectedError(TEXT("目标层数"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("不允许缩层"), Grid->ExpandToLayerCount(7, Reason));
	TestFalse(TEXT("侧格拒绝超出自身有效范围"), Grid->ExpandToLayerCount(65, Reason));
	TestEqual(TEXT("失败后原九层仍完整"), Grid->GetCells().Num(), 36);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMainTowerBoardGrowthTest, "MovingCastle.MainTower.BoardGrowth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMainTowerBoardGrowthTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("建立棋盘扩层测试 World"), World)) { return false; }
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	if (!TestNotNull(TEXT("建立扩层棋盘"), Board)) { return false; }
	CastleBuildingTests::ConfigureBoard(Board);
	CastleBuildingTests::FScopedNativeBuildingModels BuildingModels(World);
	if (!TestNotNull(TEXT("复制实际 PCG 规则资产"), Board->PCGRuleSet.Get())) { return false; }
	Board->PCGRuleSet = DuplicateObject<UCastlePCGRuleSetDataAsset>(Board->PCGRuleSet, Board);
	Board->PCGRuleSet->Rules = {NewObject<UCastlePCGPlanarRule>(Board->PCGRuleSet)};
	Board->GridSize = FIntVector(5, 5, 9);
	Board->PlacementSurfaceMode = ECastlePlacementSurfaceMode::MainTowerSides;
	Board->TowerSideSurfaceOffset = 290.0f;
	Board->bEnableProgressiveTowerLayers = true;
	Board->bSpawnMainTowerOnBeginPlay = true;
	Board->bSpawnInitialEnemiesOnBeginPlay = false;
	// 验证原始主塔蓝图的分段和挂接，测试资产与正式地图一致。
	Board->MainTowerClass = LoadClass<AActor>(nullptr, TEXT("/Game/MovingCastle/BP/BP_MainTower.BP_MainTower_C"));
	if (!TestNotNull(TEXT("原始主塔蓝图可加载"), Board->MainTowerClass.Get())) { return false; }
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	if (!TestNotNull(TEXT("开局成功生成塔身"), Board->MainTowerActor.Get())) { return false; }
	const FIntVector Center = Board->GetBoardCenterCoordinate();
	const FVector Up = Board->GetActorUpVector();
	USceneComponent* Crown = nullptr;
	USceneComponent* Balloons = nullptr;
	for (UActorComponent* Part : Board->MainTowerActor->GetComponents())
	{
		if (Part->GetFName() == TEXT("TowerCrownAnchor")) { Crown = Cast<USceneComponent>(Part); }
		if (Part->GetFName() == TEXT("TowerBalloonAnchor")) { Balloons = Cast<USceneComponent>(Part); }
	}
	if (!TestNotNull(TEXT("塔尖锚点存在"), Crown) || !TestNotNull(TEXT("气球锚点存在"), Balloons)) { return false; }
	const FVector InitialCrown = Crown->GetComponentLocation();
	const FVector InitialBalloons = Balloons->GetComponentLocation();
	FText Reason;
	const FCastleTowerSideCell* OriginalSide = Board->FindTowerSideCell(Center, ECastleGridFace::NegativeX);
	if (!TestNotNull(TEXT("已有旧层的建造面"), OriginalSide)) { return false; }
	FCastleBuildingDescriptor OriginalCard;
	if (!Board->BuildingDefinitions[0]->CreateDescriptor(1, OriginalSide->Color, OriginalSide->Number, OriginalCard, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	const FIntVector OldRoomCell = Center + FIntVector(-1, 0, 0);
	FCastleGridCursorTarget OriginalTarget;
	OriginalTarget.bValid = true;
	OriginalTarget.Cell = Center;
	OriginalTarget.AdjacentCell = OldRoomCell;
	OriginalTarget.Face = ECastleGridFace::NegativeX;
	OriginalTarget.Surface = ECastleGridCursorSurface::MainTowerSide;
	ACastleBuildingBase* OldRoom = Board->PlaceBuildingAtCursor(OriginalTarget, OriginalCard, false, Reason);
	if (!TestNotNull(TEXT("通过正式入口放置旧层房间"), OldRoom)
		|| !TestTrue(TEXT("旧层房间完成建造"), CastleBuildingTests::CompletePlacement(OldRoom)))
	{
		AddError(Reason.ToString());
		return false;
	}
	for (const int32 Layers : {3, 5, 7, 9})
	{
		if (Layers != 3 && !TestTrue(TEXT("扩层提交成功"), Board->ExpandMainTowerLayers(Layers, Reason))) { return false; }
		TestEqual(TEXT("棋盘发布实际层数"), Board->GetUnlockedLayerCount(), Layers);
		TestTrue(TEXT("旧层房间保持原实例与占用"), Board->GetBuildingAt(OldRoomCell) == OldRoom && IsValid(OldRoom));
		const FIntVector Highest = Center + FIntVector(1, 0, Layers - 1);
		TestTrue(TEXT("最高开放层在范围内"), Board->IsCoordinateWithinBoard(Highest));
		TestFalse(TEXT("再上一层超出高度"), Board->IsCoordinateWithinBoard(Highest + FIntVector(0, 0, 1)));
		TestFalse(TEXT("第一层以下超出高度"), Board->IsCoordinateWithinBoard(Center + FIntVector(1, 0, -1)));
		const FVector LocalCrown = Board->MainTowerActor->GetActorTransform().InverseTransformPosition(Crown->GetComponentLocation());
		// 正式塔身原件每段 500 cm，塔冠底面与当前最高可见段的顶面对齐。
		constexpr double SourceSectionHeight = 500.0;
		TestTrue(TEXT("塔冠落在当前最高塔身段顶面"), FMath::IsNearlyEqual(LocalCrown.Z + 5000.0, Layers * SourceSectionHeight, 0.01));
		int32 VisibleBodyParts = 0;
		int32 HiddenBodyParts = 0;
		int32 CrownParts = 0;
		for (const UActorComponent* Component : Board->MainTowerActor->GetComponents())
		{
			const auto* Mesh = Cast<UStaticMeshComponent>(Component);
			if (!Mesh || !Mesh->GetAttachParent()) { continue; }
			if (Mesh->GetAttachParent()->GetFName() == TEXT("TowerBodyAnchor"))
			{
				if (Mesh->IsVisible()) { ++VisibleBodyParts; }
				else
				{
					++HiddenBodyParts;
					TestEqual(TEXT("隐藏塔身关闭碰撞"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
				}
			}
			else if (Mesh->GetAttachParent() == Crown) { ++CrownParts; }
		}
		TestEqual(TEXT("每层显示原蓝图的 42 件塔身"), VisibleBodyParts, 42 * Layers);
		TestEqual(TEXT("未开放层及第十段仍隐藏"), HiddenBodyParts, 42 * (10 - Layers));
		TestEqual(TEXT("塔冠完整保留 54 件"), CrownParts, 54);
		const double HeightDelta = (Layers - 3) * SourceSectionHeight;
		TestTrue(TEXT("塔尖上移与新增塔身段高度相等"), Crown->GetComponentLocation().Equals(InitialCrown + Up * HeightDelta, 0.1));
		TestTrue(TEXT("气球跟随塔尖"), Balloons->GetComponentLocation().Equals(InitialBalloons + Up * HeightDelta, 0.1));
		const auto* Cell = Board->FindTowerSideCell(Highest - FIntVector(1, 0, 0), ECastleGridFace::PositiveX);
		if (!TestNotNull(TEXT("最高开放层存在侧格"), Cell)) { return false; }
		FCastleBuildingDescriptor Descriptor;
		if (!Board->BuildingDefinitions[0]->CreateDescriptor(1, Cell->Color, Cell->Number, Descriptor, Reason)) { return false; }
		FCastleGridCursorTarget Target;
		Target.bValid = true;
		Target.Cell = Cell->TowerCell;
		Target.AdjacentCell = Highest;
		Target.Face = ECastleGridFace::PositiveX;
		Target.Surface = ECastleGridCursorSurface::MainTowerSide;
		TestTrue(TEXT("最高开放层可用侧格建造"), Board->CanPlaceBuildingAtCursor(Target, Descriptor, Reason));
		TestFalse(TEXT("真实放置检查拒绝更高一层"), Board->CanPlaceBuilding(Highest + FIntVector(0, 0, 1), Descriptor, Reason));
		TestTrue(TEXT("放置失败来自层高"), Reason.ToString().Contains(TEXT("层")));
	}
	AddExpectedError(TEXT("目标层数必须严格大于"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("棋盘拒绝缩层"), Board->ExpandMainTowerLayers(7, Reason));
	TestFalse(TEXT("棋盘拒绝第十层"), Board->ExpandMainTowerLayers(10, Reason));
	// 使用正式建造入口配置门外房间，验证正负 X 语义与真实棋盘格心一致。
	UCastleTowerDispatchDoors* Doors = NewObject<UCastleTowerDispatchDoors>(Board->MainTowerActor);
	Board->MainTowerActor->AddInstanceComponent(Doors);
	Doors->RegisterComponent();
	if (!TestTrue(TEXT("注册塔底双门"), Doors->Initialize(*Board, Reason))) { return false; }
	FVector DoorA, DoorB;
	AddExpectedMessagePlain(TEXT("需要已完成放置的房间"), ELogVerbosity::Warning);
	TestFalse(TEXT("缺门 A 房间时没有有效出口"), Doors->TryGetDoorWaypoints(DoorA, DoorB, Reason));
	TestTrue(TEXT("缺门的提示指向门 A"), Reason.ToString().Contains(TEXT("门 A")));
	const auto* ExitSide = Board->FindTowerSideCell(Center, ECastleGridFace::PositiveX);
	if (!TestNotNull(TEXT("门 A 所在侧格"), ExitSide)) { return false; }
	FCastleBuildingDescriptor ExitCard;
	if (!Board->BuildingDefinitions[0]->CreateDescriptor(1, ExitSide->Color, ExitSide->Number, ExitCard, Reason)) { return false; }
	FCastleGridCursorTarget ExitTarget = OriginalTarget;
	ExitTarget.Face = ECastleGridFace::PositiveX;
	ExitTarget.AdjacentCell = Center + FIntVector(1, 0, 0);
	ACastleBuildingBase* ExitRoom = Board->PlaceBuildingAtCursor(ExitTarget, ExitCard, false, Reason);
	if (!TestTrue(TEXT("门 A 房间完成放置"), CastleBuildingTests::CompletePlacement(ExitRoom))) { return false; }
	// 原生模型夹具采用明确的作者站立面，正式关卡由房间布局资产提供同一个坐标。
	for (ACastleBuildingBase* Room : {ExitRoom, OldRoom})
	{
		Room->HachimisLayout = NewObject<UCastleHachimisV4Layout>(Room);
		Room->HachimisLayout->StandingHeight = 25.0;
	}
	if (!TestTrue(TEXT("双门均取得真实房间的落点"), Doors->TryGetDoorWaypoints(DoorA, DoorB, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	const FVector TowerCenter = Board->GetGridWorldLocation(Center);
	const double ExpectedDoorOffsetX = (Board->CellSize * 0.5 + 30.0) * 0.5;
	TestTrue(TEXT("门 A 锚点位于主塔一楼内部 +X 侧"), FVector::DistSquared2D(DoorA, TowerCenter + FVector(ExpectedDoorOffsetX, 0, 0)) < 0.01);
	TestTrue(TEXT("门 B 锚点位于主塔一楼内部 −X 侧"), FVector::DistSquared2D(DoorB, TowerCenter - FVector(ExpectedDoorOffsetX, 0, 0)) < 0.01);
	const double FloorZ = Board->GetGridWorldLocation(OldRoomCell).Z - Board->LayerHeight * 0.5 + 25.0;
	TestTrue(TEXT("双门均采用房间站立面标高"), FMath::IsNearlyEqual(DoorA.Z, FloorZ, 0.01)
		&& FMath::IsNearlyEqual(DoorB.Z, FloorZ, 0.01));
	FHitResult FloorHit;
	for (const FVector& DoorPoint : {DoorA, DoorB})
	{
		TestTrue(TEXT("主塔一楼内部门点下方具备不可见导航地板碰撞"),
			World->LineTraceSingleByChannel(FloorHit, DoorPoint + FVector(0, 0, 10), DoorPoint - FVector(0, 0, 10), ECC_Pawn)
			&& FloorHit.GetActor() == Board->MainTowerActor
			&& FMath::IsNearlyEqual(FloorHit.ImpactPoint.Z, FloorZ, 0.1));
	}
	TestFalse(TEXT("主塔一楼中央留有隔断空隙防止穿塔抄近道"),
		World->LineTraceSingleByChannel(FloorHit, FVector(TowerCenter.X, TowerCenter.Y, FloorZ + 10.0),
			FVector(TowerCenter.X, TowerCenter.Y, FloorZ - 10.0), ECC_Pawn));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMainTowerWaypointLimitTest, "MovingCastle.MainTower.WaypointLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMainTowerWaypointLimitTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("建立路点测试 World"), World)) { return false; }
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	ACastlePatrolRoute* Route = World->SpawnActor<ACastlePatrolRoute>();
	if (!TestNotNull(TEXT("生成路线编辑入口"), Route)) { return false; }
	// 直接使用准星射线成功解析落点后的相同入口，避免测试依赖鼠标焦点。
	FText Reason;
	TestEqual(TEXT("初始限额为两点"), Route->GetMaxPlayerWaypoints(), 2);
	TestTrue(TEXT("第 1 点被接收"), Route->TryAppendDraftWaypoint(FVector(0, 0, 0), Reason));
	TestTrue(TEXT("第 2 点被接收"), Route->TryAppendDraftWaypoint(FVector(100, 0, 0), Reason));
	TestFalse(TEXT("第 3 点超出限额"), Route->TryAppendDraftWaypoint(FVector(200, 0, 0), Reason));
	TestTrue(TEXT("超限有可见提示"), Reason.ToString().Contains(TEXT("已达到当前路径点上限")));
	TestEqual(TEXT("拒绝后草稿仍有两点"), Route->DraftWaypoints.Num(), 2);
	TestTrue(TEXT("升级增加一个点"), Route->AddMaxPlayerWaypoints(1));
	TestTrue(TEXT("提升限额后第 3 点被接收"), Route->TryAppendDraftWaypoint(FVector(200, 0, 0), Reason));
	TestEqual(TEXT("草稿最终有三个点"), Route->DraftWaypoints.Num(), 3);
	TestTrue(TEXT("编号允许的最后一个值为 99"), Route->SetMaxPlayerWaypoints(99));
	AddExpectedError(TEXT("结果必须在 1～"), EAutomationExpectedErrorFlags::Contains, 3);
	TestFalse(TEXT("配置不能超过编号资产的 99 上限"), Route->SetMaxPlayerWaypoints(100));
	TestFalse(TEXT("增量不能超过 99 上限"), Route->AddMaxPlayerWaypoints(1));
	TestFalse(TEXT("配置不能低于 1"), Route->SetMaxPlayerWaypoints(0));
	TestEqual(TEXT("无效配置保留原上限"), Route->GetMaxPlayerWaypoints(), 99);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMainTowerDispatchTest, "MovingCastle.MainTower.DispatchPool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMainTowerDispatchTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("建立出兵测试 World"), World)) { return false; }
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	AActor* Tower = World->SpawnActor<AActor>();
	UCastleMainTowerComponent* Health = NewObject<UCastleMainTowerComponent>(Tower);
	Health->InitializeHealth();
	auto* Towers = World->GetSubsystem<UCastleMainTowerSubsystem>();
	auto* Dispatch = World->GetSubsystem<UCastleTowerDispatchSubsystem>();
	auto* Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
	auto* Mass = World->GetSubsystem<UMassEntitySubsystem>();
	if (!Towers || !Dispatch || !Navigation || !Mass || !Towers->RegisterMainTower(Health))
	{
		AddError(TEXT("无法初始化出兵测试的世界子系统。"));
		return false;
	}
	auto& Manager = Mass->GetMutableEntityManager();
	UMassEntityConfigAsset* NPC = NewObject<UMassEntityConfigAsset>(World);
	auto* Trait = NewObject<UCastleMassNPCTrait>(NPC);
	Trait->Movement.MaxSpeed = 400.0f;
	Trait->Movement.Acceleration = 0.0f;
	NPC->GetMutableConfig().AddTrait(*Trait);
	auto* Config = NewObject<UCastleTowerDispatchConfigDataAsset>(World);
	Config->NPCConfiguration = NPC;
	FText Reason;
	if (!TestTrue(TEXT("登记出兵配置"), Dispatch->Configure(Config, Reason))
		|| !TestTrue(TEXT("八人池开启"), Dispatch->StartBattleDispatchSession(NPC, 8, Reason))) { return false; }
	// 在本用例直接提供一个已解出的 A→路点→B 快照，专门验证派兵与 Mass 进度。
	FCastleSharedNavigationPath Path;
	Path.bValid = Path.bTowerDispatch = true;
	Path.Waypoints = Path.PatrolPoints = {FVector(0, 0, 0), FVector(100, 0, 0), FVector(200, 0, 0)};
	Path.WaypointDistances = Path.PatrolDistances = {0.0, 100.0, 200.0};
	Path.Length = 200.0;
	Navigation->PublishPath(MoveTemp(Path));
	auto* Processor = NewObject<UCastleMassPatrolProcessor>(World);
	Processor->CallInitialize(World, Manager.AsShared());
	auto* Lifetime = NewObject<UCastleMassLifetimeProcessor>(World);
	Lifetime->CallInitialize(World, Manager.AsShared());
	const auto Move = [&](float DeltaSeconds)
	{
		UE::Mass::FProcessingContext Processing(Manager, DeltaSeconds);
		UE::Mass::Executor::Run(*Processor, Processing);
	};
	const auto CheckCounts = [&](const int32 ExpectedReserve, const int32 ExpectedActive)
	{
		int32 Reserve, Active;
		Dispatch->GetDispatchCounts(Reserve, Active);
		TestEqual(TEXT("待命数"), Reserve, ExpectedReserve);
		TestEqual(TEXT("活跃数"), Active, ExpectedActive);
		TestEqual(TEXT("总人数守恒"), Reserve + Active, 8);
	};
	const auto FinishMassDestruction = [&]
	{
		Manager.FlushCommands();
		UE::Mass::FProcessingContext Processing(Manager, 0.0f);
		UE::Mass::Executor::Run(*Lifetime, Processing);
		Manager.FlushCommands();
	};
	CheckCounts(8, 0);
	TestTrue(TEXT("按下立即派出"), Dispatch->BeginManualDispatch(Reason));
	CheckCounts(7, 1);
	Dispatch->Tick(0.10f);
	CheckCounts(7, 1);
	Dispatch->Tick(0.06f);
	Dispatch->StopManualDispatch();
	CheckCounts(6, 2);
	Move(1.0f);
	for (const auto& Active : Dispatch->ActiveNPCs)
	{
		const auto& Patrol = Manager.GetFragmentDataChecked<FCastleMassPatrolFragment>(Active.Handle.Entity);
		TestTrue(TEXT("手动模式抵达门 B"), Patrol.bReachedDispatchEntrance);
	}
	Dispatch->Tick(0.0f);
	CheckCounts(6, 2);
	// 处理实际延迟请求和生命周期回收，确认计数只在实体退出后改变。
	FinishMassDestruction();
	Dispatch->Tick(0.0f);
	CheckCounts(8, 0);

	TestTrue(TEXT("自动派兵可开启"), Dispatch->SetAutoDispatchEnabled(true));
	Dispatch->Tick(0.25f);
	CheckCounts(8, 0);
	Dispatch->Tick(0.25f);
	CheckCounts(7, 1);
	for (int32 Index = 1; Index < 8; ++Index) { Dispatch->Tick(0.5f); }
	CheckCounts(0, 8);
	TestFalse(TEXT("池空时派兵明确被拒绝"), Dispatch->TryDispatchSingleNPC(Reason));
	Move(0.75f);
	for (const auto& Active : Dispatch->ActiveNPCs)
	{
		const auto& Patrol = Manager.GetFragmentDataChecked<FCastleMassPatrolFragment>(Active.Handle.Entity);
		TestEqual(TEXT("自动单位完成一圈"), Patrol.CompletedLaps, static_cast<int64>(1));
		TestFalse(TEXT("自动到达门 B 不回池"), Patrol.bReachedDispatchEntrance);
		const auto& Transform = Manager.GetFragmentDataChecked<FTransformFragment>(Active.Handle.Entity).GetTransform();
		TestTrue(TEXT("在门 A 重开后走完本帧余下的 100 cm"), Transform.GetLocation().Equals(FVector(100, 0, 0), 0.01));
	}
	CheckCounts(0, 8);
	TestTrue(TEXT("切回手动"), Dispatch->SetAutoDispatchEnabled(false));
	Move(1.0f);
	for (const auto& Active : Dispatch->ActiveNPCs)
	{
		TestTrue(TEXT("切换后再次抵达门 B 会回收"),
			Manager.GetFragmentDataChecked<FCastleMassPatrolFragment>(Active.Handle.Entity).bReachedDispatchEntrance);
	}
	Dispatch->Tick(0.0f);
	FinishMassDestruction();
	Dispatch->Tick(0.0f);
	CheckCounts(8, 0);
	TestTrue(TEXT("空场可结束会话"), Dispatch->EndBattleDispatchSession(Reason));
	TestFalse(TEXT("结束后会话失效"), Dispatch->IsSessionActive());
	return true;
}

#endif
