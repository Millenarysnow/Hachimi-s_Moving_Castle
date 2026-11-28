// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/PCG/CastlePCGSemanticTestRules.h"
#include "Castle/Components/CastleMainTowerBalloonComponent.h"
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
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "PhysicsEngine/BodySetup.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMainTowerKitAssetTest, "MovingCastle.MainTower.KitAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMainTowerKitAssetTest::RunTest(const FString& Parameters)
{
	// 来自正式交付 assembly_manifest.json 的 UE 坐标。净塔身与带气球整塔的包围盒不同。
	struct FExpectedMesh
	{
		const TCHAR* Name;
		FVector Min;
		FVector Max;
	};
	const FExpectedMesh Towers[] = {
		{TEXT("SM_MainTower_L01"), {-309.042, -309.042, 0}, {309.042, 309.042, 2908.8999}},
		{TEXT("SM_MainTower_L02"), {-309.042, -309.042, 0}, {309.042, 309.042, 4028.9005}},
		{TEXT("SM_MainTower_L03"), {-359.0, -309.042, 0}, {309.042, 354.0, 5148.9002}},
		{TEXT("SM_MainTower_L04"), {-359.0, -354.0, 0}, {359.0, 354.0, 6268.9007}},
		{TEXT("SM_MainTower_L05"), {-359.0, -354.0, 0}, {359.0, 354.0, 7388.9}}
	};
	const FExpectedMesh LiftKit[] = {
		{TEXT("SM_LiftAnchor"), {-47, -42, 0}, {47, 42, 65.4}},
		{TEXT("SM_LiftTether"), {-7.2, -7.2, 0}, {7.2, 7.2, 100}},
		{TEXT("SM_LiftBalloon_Scout"), {-364.5919, -353.6411, -22.6}, {364.5919, 353.6413, 788.7}},
		{TEXT("SM_LiftBalloon_Spindle"), {-375.7576, -366.1951, -22.6}, {375.7576, 366.1952, 994.7}},
		{TEXT("SM_LiftBalloon_Lantern"), {-453.8532, -415.6883, -22.6}, {453.8532, 415.6882, 866.7}},
		{TEXT("SM_LiftBalloon_Crown"), {-532.8232, -425.1458, -22.6}, {532.8231, 425.1458, 993.5788}},
		{TEXT("SM_LiftBalloon_Pod_Rose"), {-193.7877, -177.0059, -14.69}, {193.7877, 177.0059, 471.7}},
		{TEXT("SM_LiftBalloon_Pod_Teal"), {-193.7877, -177.0059, -14.69}, {193.7877, 177.0059, 471.7}}
	};
	const auto CheckMesh = [&](const FExpectedMesh& Expected, const TCHAR* Folder, const bool bTower)
	{
		const FString Path = FString::Printf(TEXT("/Game/MovingCastle/Model/MainTower/%s/%s.%s"),
			Folder, Expected.Name, Expected.Name);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		if (!TestNotNull(Path, Mesh)) { return; }
		const FBox Bounds = Mesh->GetBoundingBox();
		TestTrue(FString::Printf(TEXT("%s 包围盒与作者清单的误差小于 1.5 毫米"), Expected.Name),
			Bounds.Min.Equals(Expected.Min, 0.15) && Bounds.Max.Equals(Expected.Max, 0.15));
		TestFalse(FString::Printf(TEXT("%s 含作者材质槽"), Expected.Name), Mesh->GetStaticMaterials().IsEmpty());
		for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
		{
			TestEqual(FString::Printf(TEXT("%s 的材质槽 %s 指向同名材质"), Expected.Name, *Slot.MaterialSlotName.ToString()),
				GetNameSafe(Slot.MaterialInterface.Get()), Slot.MaterialSlotName.ToString());
		}
		if (bTower && TestNotNull(TEXT("整塔包含可查询的碰撞定义"), Mesh->GetBodySetup()))
		{
			TestTrue(TEXT("整塔射线使用三角网格"), Mesh->GetBodySetup()->CollisionTraceFlag == CTF_UseComplexAsSimple);
		}
	};
	for (const FExpectedMesh& Mesh : Towers) { CheckMesh(Mesh, TEXT("Tower"), true); }
	for (const FExpectedMesh& Mesh : LiftKit) { CheckMesh(Mesh, TEXT("LiftKit"), false); }
	// 独立核对五个导入底座共用塔根原点、厘米尺寸及同名作者材质，含正式上限之外的 L05。
	const FExpectedMesh FlightBases[] = {
		{TEXT("SM_MainTower_FlightBase_L01"), {-272.5, -272.5, -191.654}, {272.5, 272.5, 4.7}},
		{TEXT("SM_MainTower_FlightBase_L02"), {-277.0, -277.0, -231.417}, {277.0, 277.0, 4.7}},
		{TEXT("SM_MainTower_FlightBase_L03"), {-281.5, -281.5, -279.385}, {281.5, 281.5, 4.7}},
		{TEXT("SM_MainTower_FlightBase_L04"), {-286.0, -286.0, -329.714}, {286.0, 286.0, 4.7}},
		{TEXT("SM_MainTower_FlightBase_L05"), {-290.0, -290.0, -385.922}, {290.0, 290.0, 4.7}}
	};
	for (const FExpectedMesh& Mesh : FlightBases) { CheckMesh(Mesh, TEXT("FlightBase"), false); }
	CheckMesh({TEXT("SM_MainTower_DoorSigns_560"), {-323.3, -141.6, 369.152}, {323.3, 141.6, 464.852}}, TEXT("DoorSigns"), false);
	auto* Exhaust = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/RocketThrusterExhaustFX/FX/NS_RocketExhaust_Realistic.NS_RocketExhaust_Realistic"));
	if (TestNotNull(TEXT("正式喷口 Niagara 系统"), Exhaust))
	{
		// 编辑器冷启动首次加载 Niagara 系统时脚本处于按需编译态，校验前须完成加载与脚本编译。
		Exhaust->EnsureFullyLoaded();
#if WITH_EDITORONLY_DATA
		Exhaust->WaitForCompilationComplete(false, false);
#endif
		TestTrue(TEXT("火焰可用且已包含发射器"), Exhaust->IsValid() && !Exhaust->GetEmitterHandles().IsEmpty());
	}
	const TCHAR* Palette[] = {
		TEXT("Brass"), TEXT("Bronze"), TEXT("CanvasMint"), TEXT("CanvasPink"), TEXT("CanvasRose"), TEXT("CanvasRoseDeep"),
		TEXT("CanvasRoseLight"), TEXT("CanvasTeal"), TEXT("CanvasTealDeep"), TEXT("Cloud"), TEXT("Dark"), TEXT("EmberTile"),
		TEXT("GlassDeep"), TEXT("GlassLight"), TEXT("Iron"), TEXT("Lamp"), TEXT("Linen"), TEXT("LinenShade"), TEXT("PeachWood"),
		TEXT("PlasterShade"), TEXT("Quoin"), TEXT("Rope"), TEXT("SteelTeal"), TEXT("StoneLight"), TEXT("StoneShade"),
		TEXT("TileDeep"), TEXT("TileLight"), TEXT("TileShade"), TEXT("WoodLight"), TEXT("WoodShade")
	};
	for (const TCHAR* Suffix : Palette)
	{
		const FString Path = FString::Printf(TEXT("/Game/MovingCastle/Model/MainTower/Materials/M_Aero_%s.M_Aero_%s"), Suffix, Suffix);
		UMaterial* Material = LoadObject<UMaterial>(nullptr, *Path);
		if (TestNotNull(Path, Material) && FCString::Strcmp(Suffix, TEXT("Lamp")) == 0)
		{
#if WITH_EDITORONLY_DATA
			TestNotNull(TEXT("灯具材质保留自发光输入"), Material->GetEditorOnlyData()->EmissiveColor.Expression);
#endif
		}
	}
	return true;
}

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

	// 教学场景将一格 HUD 血条作为最低生命，重复超额伤害也只能停在该下限。
	TestTrue(TEXT("教程锁血测试重新初始化主塔"), Tower->InitializeHealth());
	TestTrue(TEXT("教程启用一心锁血"), Tower->EnableTutorialHealthLock(Reason));
	const float TutorialHealthFloor = Tower->MaxHealth / 20.0f;
	const float HealthAboveOneHeart = TutorialHealthFloor + 10.0f;
	Tower->MaxBalloons = 20;
	Tower->HealthPerBalloon = 11.0f;
	TestTrue(TEXT("教程测试主塔受击后仍高于一心"), Tower->ApplyDamage(Tower->MaxHealth - HealthAboveOneHeart));
	AddExpectedError(TEXT("至少保留一颗心"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("教程不能回收导致生命低于一心的气球"), Tower->TryConsumeBalloons(1, Reason));
	TestTrue(TEXT("拒绝一心以下的气球回收后保留原生命"), FMath::IsNearlyEqual(Tower->CurrentHealth, HealthAboveOneHeart));
	TestTrue(TEXT("超额攻击在教程锁血下仍成功结算"), Tower->ApplyDamage(Tower->MaxHealth * 2.0f));
	TestTrue(TEXT("超额攻击将主塔保留在一颗心"), FMath::IsNearlyEqual(Tower->CurrentHealth, TutorialHealthFloor));
	TestTrue(TEXT("锁血主塔仍然存活"), Tower->IsAlive());
	TestTrue(TEXT("锁血后续攻击继续结算"), Tower->ApplyDamage(Tower->MaxHealth));
	TestTrue(TEXT("后续攻击不能降低一心下限"), FMath::IsNearlyEqual(Tower->CurrentHealth, TutorialHealthFloor));
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
	if (!TestTrue(TEXT("生成开局三层"), Grid->Initialize(FIntVector::ZeroValue, 3, 500.0f, 515.0f, 560.0f, 290.0f, 17, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestEqual(TEXT("开局四面各三格"), Grid->GetCells().Num(), 12);
	const FCastleTowerSideCell& FirstSideCell = Grid->GetCells()[0];
	TestTrue(TEXT("塔侧格使用没有窄边的 Plane 网格"), IsValid(FirstSideCell.Panel)
		&& FirstSideCell.Panel->GetStaticMesh() && FirstSideCell.Panel->GetStaticMesh()->GetFName() == TEXT("Plane"));
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
	if (!CastlePCGTests::ConfigurePlacementOnlyPCG(*Board, *this)) { return false; }
	Board->GridSize = FIntVector(5, 5, 9);
	Board->PlacementSurfaceMode = ECastlePlacementSurfaceMode::MainTowerSides;
	// 与 MainMap 的原始关卡配置一致，直接覆盖正式模型和面板前沿的深度关系。
	Board->TowerSideSurfaceOffset = 287.0f;
	Board->bEnableProgressiveTowerLayers = true;
	Board->bSpawnMainTowerOnBeginPlay = true;
	Board->bSpawnInitialEnemiesOnBeginPlay = false;
	// 使用正式地图同一蓝图与套件，不用占位网格替代主塔。
	Board->MainTowerClass = LoadClass<AActor>(nullptr, TEXT("/Game/MovingCastle/BP/BP_MainTower.BP_MainTower_C"));
	if (!TestNotNull(TEXT("正式主塔蓝图可加载"), Board->MainTowerClass.Get())) { return false; }
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	if (!TestNotNull(TEXT("开局成功生成塔身"), Board->MainTowerActor.Get())) { return false; }
	const FIntVector Center = Board->GetBoardCenterCoordinate();
	auto* Health = Board->MainTowerActor->FindComponentByClass<UCastleMainTowerComponent>();
	if (!TestNotNull(TEXT("主塔生命组件已登记"), Health)) { return false; }
	TestEqual(TEXT("三层主塔战斗几何使用 1680 cm 实际塔高"), Health->TowerShaftHeight, 3.0f * Board->LayerHeight);
	auto* LiftPresentation = Board->MainTowerActor->FindComponentByClass<UCastleMainTowerBalloonComponent>();
	if (!TestNotNull(TEXT("主塔吊挂组件已登记"), LiftPresentation)) { return false; }
	USceneComponent* Crown = nullptr;
	USceneComponent* Balloons = nullptr;
	for (UActorComponent* Part : Board->MainTowerActor->GetComponents())
	{
		if (Part->GetFName() == TEXT("TowerCrownAnchor")) { Crown = Cast<USceneComponent>(Part); }
		if (Part->GetFName() == TEXT("TowerBalloonAnchor")) { Balloons = Cast<USceneComponent>(Part); }
	}
	if (!TestNotNull(TEXT("塔尖锚点存在"), Crown) || !TestNotNull(TEXT("气球锚点存在"), Balloons)) { return false; }
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
	TMap<FIntVector, ACastleBuildingBase*> PlacedRooms;
	PlacedRooms.Add(OldRoomCell, OldRoom);
	const auto FindExhaust = [&]() { return Board->MainTowerActor->FindComponentByClass<UNiagaraComponent>(); };
	const auto CheckFlight = [&](const UNiagaraComponent* Exhaust, const int32 Tier, const bool bVisible)
	{
		if (!TestNotNull(TEXT("有已登记的中央喷口 Niagara"), Exhaust)) { return false; }
		const auto* Base = Cast<UStaticMeshComponent>(Exhaust->GetAttachParent());
		if (!TestNotNull(TEXT("喷口挂在该等级底座下"), Base)) { return false; }
		TestEqual(TEXT("底座与当前塔身等级一致"), GetNameSafe(Base->GetStaticMesh()),
			FString::Printf(TEXT("SM_MainTower_FlightBase_L%02d"), Tier + 1));
		TestTrue(TEXT("底座挂在共同塔根且保持作者原点"), Base->GetAttachParent() == Board->MainTowerActor->GetRootComponent()
			&& Base->GetRelativeTransform().Equals(FTransform::Identity));
		const double ExhaustZ[] = {-183.0, -221.0, -266.0, -315.0, -371.0};
		TestTrue(TEXT("发射面紧贴中央喷口下沿"), Exhaust->GetRelativeLocation().Equals(FVector(0, 0, ExhaustZ[Tier]), 0.01));
		TestTrue(TEXT("作者 +X 喷射轴对齐底座的 -Z"), Exhaust->GetRelativeRotation().Vector().Equals(FVector::DownVector, 0.001));
		TestEqual(TEXT("喷口资产是 Realistic 作者版本"), GetNameSafe(Exhaust->GetAsset()), FString(TEXT("NS_RocketExhaust_Realistic")));
		TestTrue(TEXT("底座与喷口一起登记并按事务控制可见性"), Base->IsRegistered() && Exhaust->IsRegistered()
			&& Base->IsVisible() == bVisible && Exhaust->IsVisible() == bVisible);
		TestTrue(TEXT("底座和喷口都没有碰撞及导航资格"), Base->GetCollisionEnabled() == ECollisionEnabled::NoCollision
			&& Exhaust->GetCollisionEnabled() == ECollisionEnabled::NoCollision
			&& !Base->CanEverAffectNavigation() && !Exhaust->CanEverAffectNavigation());
		return true;
	};
	for (const int32 Layers : {3, 5, 7, 9})
	{
		if (Layers == 5)
		{
			// 用合法扩层请求触发侧格失败，验证待发布的 L02 吊挂撤回且开局塔身保留。
			const auto* ExistingSide = Board->FindTowerSideCell(Center, ECastleGridFace::NegativeY);
			if (!TestNotNull(TEXT("用于模拟追加失败的原侧格"), ExistingSide)) { return false; }
			const auto* Body = Cast<UStaticMeshComponent>(FindObjectFast<UObject>(Board->MainTowerActor, TEXT("TowerBodyMesh")));
			if (!TestNotNull(TEXT("扩层失败前的整塔组件"), Body)) { return false; }
			const UStaticMesh* MeshBeforeFailure = Body->GetStaticMesh();
			const UNiagaraComponent* ExhaustBeforeFailure = FindExhaust();
			const int32 PartCountBeforeFailure = TInlineComponentArray<UStaticMeshComponent*>(Board->MainTowerActor.Get()).Num();
			ExistingSide->Panel->UnregisterComponent();
			AddExpectedError(TEXT("的面板或数字未注册，或纹理材质属性无效"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("侧格失效时拒绝合法的五层扩层"), Board->ExpandMainTowerLayers(5, Reason));
			ExistingSide->Panel->RegisterComponent();
			TestEqual(TEXT("扩层失败保留三层"), Board->GetUnlockedLayerCount(), 3);
			TestTrue(TEXT("扩层失败保留 L01 塔身"), Body->GetStaticMesh() == MeshBeforeFailure);
			TestEqual(TEXT("扩层失败清理待发布吊挂"),
				TInlineComponentArray<UStaticMeshComponent*>(Board->MainTowerActor.Get()).Num(), PartCountBeforeFailure);
			TestTrue(TEXT("扩层失败保留原喷口并清除未提交火焰"), FindExhaust() == ExhaustBeforeFailure
				&& TInlineComponentArray<UNiagaraComponent*>(Board->MainTowerActor.Get()).Num() == 1);
		}
		if (Layers != 3 && !TestTrue(TEXT("扩层提交成功"), Board->ExpandMainTowerLayers(Layers, Reason))) { return false; }
		TestEqual(TEXT("棋盘发布实际层数"), Board->GetUnlockedLayerCount(), Layers);
		TestEqual(TEXT("扩层同步更新主塔战斗几何高度"), Health->TowerShaftHeight, static_cast<float>(Layers) * Board->LayerHeight);
		FCastleTowerPrism TowerPrism;
		FString TowerGeometryFailure;
		if (!TestTrue(TEXT("扩层后可读取当前真实塔身几何"), Health->GetTowerShaftPrism(TowerPrism, TowerGeometryFailure)))
		{
			AddError(TowerGeometryFailure);
			return false;
		}
		TestTrue(TEXT("扩层几何高度与开放层数相符"),
			FMath::IsNearlyEqual(TowerPrism.HalfSize.Z, Layers * Board->LayerHeight * FMath::Abs(Health->GetOwner()->GetActorScale3D().Z)));
		for (const auto& Room : PlacedRooms)
		{
			TestTrue(TEXT("旧层房间保持原实例与占用"), Board->GetBuildingAt(Room.Key) == Room.Value && IsValid(Room.Value));
		}
		const FIntVector Highest = Center + FIntVector(1, 0, Layers - 1);
		TestTrue(TEXT("最高开放层在范围内"), Board->IsCoordinateWithinBoard(Highest));
		TestFalse(TEXT("再上一层超出高度"), Board->IsCoordinateWithinBoard(Highest + FIntVector(0, 0, 1)));
		TestFalse(TEXT("第一层以下超出高度"), Board->IsCoordinateWithinBoard(Center + FIntVector(1, 0, -1)));
		const int32 Tier = (Layers - 3) / 2;
		TestEqual(TEXT("每次扩层后只保留一个喷口组件"), TInlineComponentArray<UNiagaraComponent*>(Board->MainTowerActor.Get()).Num(), 1);
		if (!CheckFlight(FindExhaust(), Tier, true)) { return false; }
		const auto* Body = Cast<UStaticMeshComponent>(FindObjectFast<UObject>(Board->MainTowerActor, TEXT("TowerBodyMesh")));
		if (!TestNotNull(TEXT("蓝图的整塔网格组件存在"), Body)) { return false; }
		TestEqual(TEXT("当前整塔使用对应的 560 cm 网格"),
			GetNameSafe(Body->GetStaticMesh()), FString::Printf(TEXT("SM_MainTower_L%02d"), Tier + 1));
		TestEqual(TEXT("整塔仅查询碰撞"), Body->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
		TestEqual(TEXT("整塔只挡建造射线"), Body->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
		TestEqual(TEXT("整塔不阻挡 NPC"), Body->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
		TestFalse(TEXT("整塔外墙不参与导航"), Body->CanEverAffectNavigation());
		const FTransform& TowerTransform = Board->MainTowerActor->GetActorTransform();
		TestTrue(TEXT("塔冠锚点对齐当前等级的檐口"), TowerTransform.InverseTransformPosition(Crown->GetComponentLocation())
			.Equals(FVector(0, 0, Layers * 560.0), 0.01));
		TestTrue(TEXT("气球锚点对齐清单中的中央吊耳"), FMath::IsNearlyEqual(
			TowerTransform.InverseTransformPosition(Balloons->GetComponentLocation()).Z, 2888.4 + (Layers - 3) * 560.0, 0.01));

		// 真实 Visibility 射线覆盖所有已开放层，不把之前放置的房间误判成塔身遮挡。
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CastleMainTowerSide), false);
		for (const auto& Room : PlacedRooms) { TraceParams.AddIgnoredActor(Room.Value); }
		const ECastleGridFace Faces[] = {
			ECastleGridFace::PositiveX, ECastleGridFace::NegativeX, ECastleGridFace::PositiveY, ECastleGridFace::NegativeY};
		const auto* Grid = Board->MainTowerActor->FindComponentByClass<UCastleTowerSideGridComponent>();
		if (!TestNotNull(TEXT("正式主塔存在侧格查询组件"), Grid)) { return false; }
		TArray<FCastleGridCursorTarget> HighestTargets;
		for (int32 Layer = 0; Layer < Layers; ++Layer)
		{
			for (const ECastleGridFace Face : Faces)
			{
				const auto* Side = Board->FindTowerSideCell(Center + FIntVector(0, 0, Layer), Face);
				if (!TestNotNull(TEXT("四面每层均有侧格"), Side)) { return false; }
				TestTrue(TEXT("塔侧格使用无厚度侧面的 Plane 网格"), Side->Panel->GetStaticMesh()
					&& Side->Panel->GetStaticMesh()->GetFName() == TEXT("Plane"));
				const FTransform& Panel = Side->Panel->GetComponentTransform();
				const FVector Outward = Panel.GetUnitAxis(EAxis::Z);
				// 基础 Plane 的 UV 从本地 -X/-Y 角起算。站在塔外看，U 应向右、V 应向下。
				const FVector TowerUp = TowerTransform.GetUnitAxis(EAxis::Z);
				const FVector ViewerRight = FVector::CrossProduct(Outward, TowerUp);
				TestTrue(TEXT("塔外正视时数字向右且正立"), Panel.GetUnitAxis(EAxis::X).Equals(ViewerRight)
					&& Panel.GetUnitAxis(EAxis::Y).Equals(-TowerUp));
				for (const double U : {-49.5, 0.0, 49.5})
				{
					const FVector Point = Panel.TransformPosition(FVector(U, 0, 0));
					FHitResult Hit;
					const bool bReachedPanel = World->LineTraceSingleByChannel(
						Hit, Point + Outward * 200.0, Point - Outward * 20.0, ECC_Visibility, TraceParams)
						&& Hit.GetComponent() == Side->Panel;
					TestTrue(FString::Printf(TEXT("%d 层塔面板射线：层 %d 面 %d 横向 %.1f，命中 %s"),
						Layers, Layer, static_cast<int32>(Face), U, *GetNameSafe(Hit.GetComponent())), bReachedPanel);
					if (Layer == Layers - 1 && U == 0.0)
					{
						FCastleGridCursorTarget& Target = HighestTargets.AddDefaulted_GetRef();
						if (!TestTrue(TEXT("最高开放层由实际命中解析建造面"), Grid->TryResolveHit(Hit, Target))) { return false; }
						const FVector ExpectedCenter = Board->GetGridWorldLocation(Target.AdjacentCell);
						const FVector ExpectedSize(Board->CellSize * 0.98f, Board->CellSize * 0.98f,
							Board->LayerHeight * 0.98f);
						const FVector ActualSize = Target.IndicatorTransform.GetScale3D() * 100.0f;
						TestTrue(TEXT("塔侧邻格中心与棋盘三维格心重合"),
							Target.IndicatorTransform.GetLocation().Equals(ExpectedCenter, 1.0f));
						TestTrue(TEXT("塔侧全息盒为 98% 棋盘格宽深与层高"),
							ActualSize.Equals(ExpectedSize * Board->GetActorScale3D(), 1.0f));
					}
				}
			}
		}

		// 逻辑容量和作者的大气球数量分别断言，半血时各按自己的总数向上取整。
		const int32 LogicalMax[] = {5, 10, 20, 40};
		const int32 VisualMax[] = {1, 1, 2, 3};
		if (!TestTrue(TEXT("当前等级按原规则设置逻辑容量并补满生命"),
			Health->SetTowerBalloonTier(LogicalMax[Tier], LogicalMax[Tier] * 200.0f, true, Reason))) { return false; }
		const auto CheckVisualAndHealth = [&](const TCHAR* Phase, int32 ExpectedVisual, int32 ExpectedLogical)
		{
			int32 LogicalLive, LogicalCapacity;
			Health->GetBalloonCounts(LogicalLive, LogicalCapacity);
			TestEqual(FString::Printf(TEXT("%s逻辑容量"), Phase), LogicalCapacity, LogicalMax[Tier]);
			TestEqual(FString::Printf(TEXT("%s逻辑存活"), Phase), LogicalLive, ExpectedLogical);
			int32 VisibleBalloons = 0;
			int32 VisibleTethers = 0;
			TInlineComponentArray<UStaticMeshComponent*> Installed(Board->MainTowerActor.Get());
			TArray<FVector> BalloonEyes;
			TArray<FVector> TetherEnds;
			for (const UStaticMeshComponent* Mesh : Installed)
			{
				const FString Name = GetNameSafe(Mesh->GetStaticMesh());
				// 网格已烘焙固定挂环，运行时再装配同位置的挂环会造成共面闪烁。
				TestFalse(TEXT("塔身没有重复实例化固定挂环"), Name == TEXT("SM_LiftAnchor"));
				if (Name.StartsWith(TEXT("SM_LiftBalloon_")))
				{
					VisibleBalloons += Mesh->IsVisible() ? 1 : 0;
					BalloonEyes.Add(Mesh->GetComponentLocation());
				}
				else if (Name == TEXT("SM_LiftTether"))
				{
					VisibleTethers += Mesh->IsVisible() ? 1 : 0;
					TetherEnds.Add(Mesh->GetComponentTransform().TransformPosition(FVector(0, 0, 100)));
				}
				if (Name.StartsWith(TEXT("SM_Lift")))
				{
					TestEqual(TEXT("吊挂关闭碰撞"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
					TestFalse(TEXT("吊挂不写入导航"), Mesh->CanEverAffectNavigation());
				}
			}
			TestEqual(FString::Printf(TEXT("%s可见大气球"), Phase), VisibleBalloons, ExpectedVisual);
			TestEqual(FString::Printf(TEXT("%s可见拉索"), Phase), VisibleTethers, ExpectedVisual);
			TestEqual(TEXT("完整等级气球组数量"), BalloonEyes.Num(), VisualMax[Tier]);
			TestEqual(TEXT("完整等级拉索组数量"), TetherEnds.Num(), VisualMax[Tier]);
			for (const FVector& End : TetherEnds)
			{
				TestTrue(TEXT("拉索局部 +Z 的 100 cm 端点抵达气球底眼"),
					BalloonEyes.ContainsByPredicate([&](const FVector& Eye) { return End.Equals(Eye, 0.01); }));
			}
		};
		CheckVisualAndHealth(TEXT("满血"), VisualMax[Tier], LogicalMax[Tier]);
		TestTrue(TEXT("扣至半血"), Health->ApplyDamage(Health->MaxHealth * 0.5f));
		CheckVisualAndHealth(TEXT("半血"), (VisualMax[Tier] + 1) / 2, (LogicalMax[Tier] + 1) / 2);
		TestTrue(TEXT("扣至 1 点生命"), Health->ApplyDamage(Health->CurrentHealth - 1.0f));
		CheckVisualAndHealth(TEXT("残血"), 1, 1);
		TestTrue(TEXT("扣至归零"), Health->ApplyDamage(1.0f));
		CheckVisualAndHealth(TEXT("归零"), 0, 0);
		if (Layers == 9)
		{
			// 已归零的吊挂保持隐藏，验证越界生命报错且拒绝刷新。
			AddExpectedError(TEXT("的吊挂生命无效"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("吊挂拒绝超出上限的生命"), LiftPresentation->UpdateHealth(Health->MaxHealth + 1.0f, Health->MaxHealth));
			CheckVisualAndHealth(TEXT("拒绝越界生命后"), 0, 0);
		}
		for (const FCastleGridCursorTarget& Target : HighestTargets)
		{
			const auto* Cell = Board->FindTowerSideCell(Target.Cell, Target.Face);
			if (!TestNotNull(TEXT("射线保留了选中的来源面"), Cell)) { return false; }
			FCastleBuildingDescriptor Descriptor;
			if (!Board->BuildingDefinitions[0]->CreateDescriptor(1, Cell->Color, Cell->Number, Descriptor, Reason)) { return false; }
			TestFalse(TEXT("真实放置检查拒绝更高一层"),
				Board->CanPlaceBuilding(Target.AdjacentCell + FIntVector(0, 0, 1), Descriptor, Reason));
			TestTrue(TEXT("放置失败来自层高"), Reason.ToString().Contains(TEXT("层")));
			ACastleBuildingBase* Room = Board->PlaceBuildingAtCursor(Target, Descriptor, false, Reason);
			if (!TestNotNull(TEXT("最高开放层可通过实际侧格命中放置房间"), Room)
				|| !TestTrue(TEXT("最高开放层房间完成放置"), CastleBuildingTests::CompletePlacement(Room)))
			{
				AddError(Reason.ToString());
				return false;
			}
			PlacedRooms.Add(Target.AdjacentCell, Room);
		}
	}
	AddExpectedError(TEXT("目标层数必须严格大于"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("主塔棋盘只开放 3、5、7、9 层"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("棋盘拒绝缩层"), Board->ExpandMainTowerLayers(7, Reason));
	TestFalse(TEXT("棋盘拒绝第十层"), Board->ExpandMainTowerLayers(10, Reason));
	// 即使关卡配置被改为 11，也不能把预留资产开放为可玩层数。
	Board->MaxTowerLayers = 11;
	Board->GridSize.Z = 11;
	TestFalse(TEXT("棋盘拒绝预留的第十一层"), Board->ExpandMainTowerLayers(11, Reason));
	TestEqual(TEXT("拒绝十一层后保留九层"), Board->GetUnlockedLayerCount(), 9);
	Board->MaxTowerLayers = 9;
	Board->GridSize.Z = 9;
	// 正式棋盘保留九层上限；单独准备作者的 11 层表现，验证 L05 可用且丢弃后仍保留 L04。
	if (!TestTrue(TEXT("准备 11 层底座套件"), LiftPresentation->PrepareTier(11, Reason))) { return false; }
	const TInlineComponentArray<UNiagaraComponent*> WithPending(Board->MainTowerActor.Get());
	const auto* Pending = WithPending.FindByPredicate([](const UNiagaraComponent* Exhaust) { return !Exhaust->IsVisible(); });
	const bool bCheckedPending = TestNotNull(TEXT("11 层隐藏喷口已准备"), Pending) && CheckFlight(*Pending, 4, false);
	LiftPresentation->DiscardPreparedTier();
	if (!bCheckedPending || !CheckFlight(FindExhaust(), 3, true)) { return false; }
	TestEqual(TEXT("丢弃 L05 后没有残留喷口"), TInlineComponentArray<UNiagaraComponent*>(Board->MainTowerActor.Get()).Num(), 1);
	// 使用正式建造入口配置门外房间，验证正负 X 语义与真实棋盘格心一致。
	UCastleTowerDispatchDoors* Doors = NewObject<UCastleTowerDispatchDoors>(Board->MainTowerActor);
	Board->MainTowerActor->AddInstanceComponent(Doors);
	Doors->RegisterComponent();
	if (!TestTrue(TEXT("注册塔底双门"), Doors->Initialize(*Board, Reason))) { return false; }
	const auto* DoorSigns = FindObjectFast<UStaticMeshComponent>(Board->MainTowerActor.Get(), TEXT("MainTowerDoorSigns"));
	if (!TestNotNull(TEXT("塔根只有一件屋门双面标识"), DoorSigns)) { return false; }
	TestEqual(TEXT("双门使用正式网格"), GetNameSafe(DoorSigns->GetStaticMesh()), FString(TEXT("SM_MainTower_DoorSigns_560")));
	TestTrue(TEXT("双门保持作者基准且不影响碰撞导航"), DoorSigns->GetRelativeTransform().Equals(FTransform::Identity)
		&& DoorSigns->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !DoorSigns->CanEverAffectNavigation());
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
	// 使用准星解析后的同一入口，监听者在通知中读取已经提交的草稿和上限。
	TArray<FIntPoint> DraftCounts;
	Route->OnWaypointDraftChanged.AddLambda([Route, &DraftCounts]
	{
		DraftCounts.Emplace(Route->GetUsedWaypointCount(), Route->GetRemainingWaypointCount());
	});
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
	const TArray<FIntPoint> ExpectedCounts = {FIntPoint(1, 1), FIntPoint(2, 0), FIntPoint(2, 1), FIntPoint(3, 0), FIntPoint(3, 96)};
	TestTrue(TEXT("成功变更才通知 HUD，且通知内可以读取最新点数"), DraftCounts == ExpectedCounts);
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
