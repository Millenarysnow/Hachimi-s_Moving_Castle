// 垂直合成回归：通过正式放置、飞入、连续合成和回收入口验证三维空间占用。

#if WITH_DEV_AUTOMATION_TESTS

#include "CastleBuildingTestUtilities.h"
#include "NavigationSystem.h"
#include "CastleBackpackComponent.h"
#include "BuildingMergeComponent.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/PCG/CastleBuildingPCGSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace CastleVerticalMergeTests
{
	/** 使用独立游戏世界，保留实际飞入完成事件与 Actor 销毁流程，不接触用户关卡。 */
	UWorld* MakeWorld()
	{
		const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
			.CreatePhysicsScene(true).CreateNavigation(true).CreateAISystem(false).ShouldSimulatePhysics(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
		if (World != nullptr)
        {
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            // 合成与拆除的动画会登记导航观察者，显式创建导航输入。
            check(FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)->GetDefaultNavDataInstance(FNavigationSystem::Create));
        }
		return World;
	}

	/** 非零网格原点与棋盘变换可发现垂直合成错误地使用世界 Z、或把锚点抬到楼层中心的问题。 */
	ACastleBoard* MakeBoard(UWorld* World, UCastleBuildingDefinition* Definition)
	{
		const FTransform Transform(FRotator(12.0f, 37.0f, -8.0f), FVector(120.0f, -80.0f, 250.0f), FVector(1.25f));
		ACastleBoard* Board = World->SpawnActor<ACastleBoard>(ACastleBoard::StaticClass(), Transform);
		if (Board == nullptr) { return nullptr; }
		Board->GridOrigin = FIntVector(-2, 3, 1);
		Board->GridSize = FIntVector(4, 4, 4);
		Board->BuildingDefinitions = { Definition };
		FCastleGroundGenerationResult Ground;
		for (int32 X = 0; X < 4; ++X)
		{
			for (int32 Y = 0; Y < 4; ++Y)
			{
				FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
				Cell.Coordinate = Board->GridOrigin + FIntVector(X, Y, 0);
				Cell.Color = ECastleTileColor::Red;
				Cell.Number = 3;
			}
		}
		FText Reason;
		if (!Board->ApplyGroundGenerationResult(Ground, Reason)) { return nullptr; }
		// 此夹具的原生立方体不含 PCG 楼板，逻辑测试单独验证占格、合成与过渡。
		// 正式蓝图的梯口和导航由同套件的 DaoDunBlueprints 及 PCG 专项使用正式资产验证。
		auto* PCG = World->GetSubsystem<UCastleBuildingPCGSubsystem>();
		check(PCG);
		PCG->ObserveBoard(Board);
		Board->OnLayoutCommitted.RemoveAll(PCG);
		return Board;
	}
}

/** 不同放置顺序覆盖水平/垂直二级、三个三级平面、以及垂直三级沿剩余轴合成四级。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleVerticalMergeShapesTest, "MovingCastle.Logic.VerticalMerge.ShapesAndChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleVerticalMergeShapesTest::RunTest(const FString& Parameters)
{
	UWorld* World = CastleVerticalMergeTests::MakeWorld();
	if (!TestNotNull(TEXT("创建垂直合成测试世界"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	// 模型是测试显式提供的输入，不在生产路径中为缺失资产创建替代资源。
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
	struct FShapeCase
	{
		const TCHAR* Name;
		int32 Level;
		TArray<FIntVector> Cells;
	};
	const TArray<FShapeCase> Cases = {
		{ TEXT("二级 X"), 2, { {0,0,0}, {1,0,0} } },
		{ TEXT("二级 Y"), 2, { {0,0,0}, {0,1,0} } },
		{ TEXT("二级 Z"), 2, { {0,0,0}, {0,0,1} } },
		{ TEXT("三级 XY"), 3, { {0,0,0}, {1,0,0}, {0,1,0}, {1,1,0} } },
		{ TEXT("三级 XZ 来自垂直对"), 3, { {0,0,0}, {0,0,1}, {1,0,0}, {1,0,1} } },
		{ TEXT("三级 YZ 来自垂直对"), 3, { {0,0,0}, {0,0,1}, {0,1,0}, {0,1,1} } },
		{ TEXT("三级 XZ 来自水平对"), 3, { {0,0,0}, {1,0,0}, {0,0,1}, {1,0,1} } },
		{ TEXT("三级 YZ 来自水平对"), 3, { {0,0,0}, {0,1,0}, {0,0,1}, {0,1,1} } },
		{ TEXT("四级来自 XZ 立面"), 4, { {0,0,0}, {0,0,1}, {1,0,0}, {1,0,1}, {0,1,0}, {0,1,1}, {1,1,0}, {1,1,1} } },
		{ TEXT("四级来自 YZ 立面"), 4, { {0,0,0}, {0,0,1}, {0,1,0}, {0,1,1}, {1,0,0}, {1,0,1}, {1,1,0}, {1,1,1} } }
	};
	// 盾塔复用完全相同的水平/垂直合成、占用索引及回收规则，不能在接入时遗漏。
	for (const ECastleBuildingType Type : { ECastleBuildingType::ArcherTower, ECastleBuildingType::CannonTower,
		ECastleBuildingType::Farm, ECastleBuildingType::ShieldTower })
	{
		UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(Type);
		ACastleBoard* Board = CastleVerticalMergeTests::MakeBoard(World, Definition);
		if (!TestNotNull(TEXT("配置固定测试棋盘"), Board)) { return false; }
		for (const FShapeCase& Shape : Cases)
		{
			const FString Context = FString::Printf(TEXT("类别 %d，%s"), static_cast<int32>(Type), Shape.Name);
			FText Reason;
			FCastleBuildingDescriptor Card;
			if (!Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason)) { AddError(Reason.ToString()); return false; }
			for (const FIntVector& Cell : Shape.Cells)
			{
				ACastleBuildingBase* Placed = Board->PlaceBuilding(Board->GridOrigin + Cell, Card, true, Reason);
				if (!TestNotNull(Context + TEXT("：正式放置成功"), Placed)) { AddError(Reason.ToString()); return false; }
				if (!TestTrue(Context + TEXT("：飞入完成后触发合成"), CastleBuildingTests::CompletePlacement(Placed))) { return false; }
			}
			ACastleBuildingBase* Result = CastleBuildingTests::FindPlacedLevel(Board, Shape.Level);
			if (!TestNotNull(Context + TEXT("：合成到预期等级"), Result)) { return false; }
			TestEqual(Context + TEXT("：仅剩最终 Actor"), Board->GetPlacedBuildingCount(), 1);
			TestEqual(Context + TEXT("：总占格数守恒"), Board->GetOccupiedRoomCellCount(), Shape.Cells.Num());
			const TArray<FIntVector> ActualCells = Result->GetOccupiedCoordinates();
			for (const FIntVector& Cell : Shape.Cells)
			{
				TestTrue(Context + TEXT("：保留真实三维坐标"), ActualCells.Contains(Board->GridOrigin + Cell));
				TestFalse(Context + TEXT("：每个占格都能阻止重复放置"), Board->CanPlaceBuilding(Board->GridOrigin + Cell, Card, Reason));
			}
			FIntVector Maximum = FIntVector::ZeroValue;
			for (const FIntVector& Cell : Shape.Cells) { Maximum = Maximum.ComponentMax(Cell); }
			const FVector ExpectedLocal(Maximum.X * Board->CellSize * 0.5f, Maximum.Y * Board->CellSize * 0.5f,
				Board->WorldBaseHeight - Board->LayerHeight * 0.5f);
			TestTrue(Context + TEXT("：锚点仍是最低层底面中心"),
				Result->GetActorLocation().Equals(Board->GetActorTransform().TransformPosition(ExpectedLocal), 0.01f));
			FQuat ExpectedRotation = Board->GetActorQuat();
			// 水平二级 Y 方向和三级 YZ 垂直平面都沿 +Z 旋转；XZ 垂直平面保持标准朝向。
			if ((Shape.Level == 2 && Maximum.Y == 1)
				|| (Shape.Level == 3 && Maximum.X == 0 && Maximum.Y == 1 && Maximum.Z == 1))
			{
				ExpectedRotation *= FQuat(FVector::UpVector, HALF_PI);
			}
			TestTrue(Context + TEXT("：按占格平面旋转模型，垂直类不发生侧翻"), Result->GetActorQuat().Equals(ExpectedRotation, 0.001));
			TestTrue(Context + TEXT("：仍使用同一建筑定义"), Result->GetBuildingDefinition() == Definition);
			TestTrue(Context + TEXT("：合并动画实际开始"), Result->BuildingMerge->bIsPlaying);
			Result->BuildingMerge->TickComponent(100.0f, LEVELTICK_All, nullptr);
			TestFalse(Context + TEXT("：合并动画正常结束"), Result->BuildingMerge->bIsPlaying);

			// 从底层起点拆一格，避免将底下仍有原支撑的上层复放条件混入回收语义。
			if (!TestTrue(Context + TEXT("：只回收指定格"), Board->DemolishBuildingCellToBackpack(
				Result, Board->GridOrigin, Board->PrototypeBackpack, Reason))) { AddError(Reason.ToString()); return false; }
			TestEqual(Context + TEXT("：回收后其余层仍占用"), Board->GetOccupiedRoomCellCount(), Shape.Cells.Num() - 1);
			for (const FIntVector& Cell : Shape.Cells)
			{
				if (Cell != FIntVector::ZeroValue)
				{
					TestNotNull(Context + TEXT("：未拆格仍由降级建筑占用"), Board->GetBuildingAt(Board->GridOrigin + Cell));
				}
			}
			ACastleBuildingBase* Replaced = Board->PlaceBuildingFromBackpack(Board->GridOrigin, Board->PrototypeBackpack, 0, false, Reason);
			if (!TestNotNull(Context + TEXT("：回收卡可重新放置"), Replaced)) { AddError(Reason.ToString()); return false; }
			if (!CastleBuildingTests::CompletePlacement(Replaced)) { return false; }
			TestEqual(Context + TEXT("：回收卡重新放置是一级"), Replaced->GetBuildingLevel(), 1);
			TestEqual(Context + TEXT("：重新放置仅消耗一张卡"), Board->PrototypeBackpack->GetItemCount(), 0);
			for (ACastleBuildingBase* Actor : Board->GetPlacedBuildings())
			{
				if (!Board->RemoveBuilding(Actor, Reason)) { AddError(Reason.ToString()); return false; }
			}
			AddInfo(Context + TEXT("：占格、锚点、动画与回收验证通过。"));
		}
	}
	return true;
}

/** 分开记录每种占格的精确降级结果，方便定位哪个优先形状或坐标发生偏差。 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCastleDemolitionShapeTest, "MovingCastle.Logic.Demolition.Shape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FCastleDemolitionShapeTest::GetTests(TArray<FString>& OutNames, TArray<FString>& OutCommands) const
{
	OutNames = {TEXT("Lv2"), TEXT("Lv3.Horizontal"), TEXT("Lv3.Vertical"), TEXT("Lv4")};
	OutCommands = OutNames;
}

bool FCastleDemolitionShapeTest::RunTest(const FString& Shape)
{
	UWorld* World = CastleVerticalMergeTests::MakeWorld();
	if (!TestNotNull(TEXT("创建单格拆除测试世界"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
	auto* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ShieldTower);
	auto* Board = CastleVerticalMergeTests::MakeBoard(World, Definition);
	if (!TestNotNull(TEXT("建立同色号的旋转棋盘夹具"), Board)) { return false; }

	FText Reason;
	FCastleBuildingDescriptor Card;
	const FIntVector Origin = Board->GridOrigin;
	struct FExpectedBuilding
	{
		int32 Level;
		TArray<FIntVector> Cells;
	};
	TArray<FExpectedBuilding> Expected;
	FIntVector RemovedOffset;
	ACastleBuildingBase* Original = nullptr;
	if (Shape == TEXT("Lv3.Vertical"))
	{
		// 用实际垂直飞入合成 XZ 四格，不依赖默认水平的多格直接生成入口。
		if (!Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason)) { AddError(Reason.ToString()); return false; }
		for (const FIntVector& Cell : {FIntVector(0, 0, 0), FIntVector(0, 0, 1), FIntVector(1, 0, 0), FIntVector(1, 0, 1)})
		{
			if (!CastleBuildingTests::CompletePlacement(Board->PlaceBuilding(Origin + Cell, Card, true, Reason)))
			{
				AddError(Reason.ToString());
				return false;
			}
		}
		Original = Board->GetBuildingAt(Origin);
		RemovedOffset = FIntVector(1, 0, 1);
		Expected = {{2, {{0, 0, 0}, {1, 0, 0}}}, {1, {{0, 0, 1}}}};
	}
	else
	{
		int32 Level = 0;
		if (Shape == TEXT("Lv2"))
		{
			Level = 2;
			RemovedOffset = FIntVector(1, 0, 0);
			Expected = {{1, {{0, 0, 0}}}};
		}
		else if (Shape == TEXT("Lv3.Horizontal"))
		{
			Level = 3;
			RemovedOffset = FIntVector(1, 1, 0);
			Expected = {{2, {{0, 0, 0}, {1, 0, 0}}}, {1, {{0, 1, 0}}}};
		}
		else if (Shape == TEXT("Lv4"))
		{
			Level = 4;
			RemovedOffset = FIntVector(1, 1, 1);
			// 缺少顶层一角时，应先留下水平四格，然后取顶层 X 向一对，最后保留单格。
			Expected = {{3, {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}}},
				{2, {{0, 0, 1}, {1, 0, 1}}}, {1, {{0, 1, 1}}}};
		}
		else
		{
			AddError(TEXT("未知的拆除形状验收场景。"));
			return false;
		}
		if (!Definition->CreateDescriptor(Level, ECastleTileColor::Red, 3, Card, Reason))
		{
			AddError(Reason.ToString());
			return false;
		}
		Original = Board->PlaceBuilding(Origin, Card, false, Reason);
		if (!CastleBuildingTests::CompletePlacement(Original)) { AddError(Reason.ToString()); return false; }
	}
	if (!TestNotNull(TEXT("待拆建筑已完整占用测试坐标"), Original)) { return false; }
	Original->BuildingMerge->TickComponent(100.0f, LEVELTICK_All, nullptr);
	const FIntVector Target = Origin + RemovedOffset;
	FIntVector Resolved;
	TestTrue(TEXT("变换后的指定格中心能解析回同一占格"),
		Board->TryResolveBuildingCell(*Original, Board->GetGridWorldLocation(Target), Resolved) && Resolved == Target);
	const FVector Midpoint = (Board->GetGridWorldLocation(Origin) + Board->GetGridWorldLocation(Origin + FIntVector(1, 0, 0))) * 0.5;
	TestTrue(TEXT("旋转缩放棋盘的等距格界仍选字典序首格"),
		Board->TryResolveBuildingCell(*Original, Midpoint, Resolved) && Resolved == Origin);
	const int32 OriginalCellCount = Original->GetOccupiedCoordinates().Num();
	int32 LayoutCommitCount = 0;
	const FDelegateHandle LayoutHandle = Board->OnLayoutCommitted.AddLambda(
		[&](const ACastleBoard*, const FVector&) { ++LayoutCommitCount; });
	ON_SCOPE_EXIT { Board->OnLayoutCommitted.Remove(LayoutHandle); };
	if (!Board->DemolishBuildingCellToBackpack(Original, Target, Board->PrototypeBackpack, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestEqual(TEXT("仅提交一次最终布局"), LayoutCommitCount, 1);
	TestNull(TEXT("指定格被释放"), Board->GetBuildingAt(Target));
	TestEqual(TEXT("其余占格数守恒"), Board->GetOccupiedRoomCellCount(), OriginalCellCount - 1);
	TestEqual(TEXT("生成预期的降级 Actor 数量"), Board->GetPlacedBuildingCount(), Expected.Num());
	TestEqual(TEXT("只回收一张牌"), Board->PrototypeBackpack->GetItemCount(), 1);
	FCastleBuildingDescriptor Returned;
	TestTrue(TEXT("回收的是同定义同色同号一级卡"), Board->PrototypeBackpack->TryGetItemAt(0, Returned)
		&& Returned.Level == 1 && Returned.Definition == Definition && Returned.Color == Card.Color && Returned.Number == Card.Number);
	for (const FExpectedBuilding& Building : Expected)
	{
		const ACastleBuildingBase* Result = Board->GetBuildingAt(Origin + Building.Cells[0]);
		if (!TestNotNull(TEXT("预期锚格已有降级建筑"), Result)) { return false; }
		TestTrue(TEXT("降级建筑处于预期等级且已经落定"), Result->GetBuildingLevel() == Building.Level
			&& Result->CanReceiveBuildingTrigger() && Result->GetOccupiedCoordinates().Num() == Building.Cells.Num());
		for (const FIntVector& Cell : Building.Cells)
		{
			TestTrue(TEXT("重组按轴向优先级占据精确格组"), Board->GetBuildingAt(Origin + Cell) == Result);
		}
	}
	return true;
}

/** 用真实徽章子系统证明邻接规整绕开赠牌，且回收入口的所有拒绝保持原索引与库存。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDemolitionNoBadgeRewardTest, "MovingCastle.Logic.Demolition.NoUpgradeBadgeReward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDemolitionNoBadgeRewardTest::RunTest(const FString&)
{
	UWorld* World = CastleVerticalMergeTests::MakeWorld();
	if (!TestNotNull(TEXT("创建带玩家徽章的拆除夹具"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* InstanceWorld = Instance->GetWorld();
	ON_SCOPE_EXIT
	{
		World->SetGameInstance(nullptr);
		Instance->Shutdown();
		GEngine->DestroyWorldContext(InstanceWorld);
		InstanceWorld->DestroyWorld(false);
	};
	World->SetGameInstance(Instance);
	auto* Badges = UCastleBadgeSubsystem::GetForWorld(World);
	if (!TestNotNull(TEXT("玩家持有真实的徽章子系统"), Badges)) { return false; }
	FCastleBadgeDefinition Badge;
	FText Reason;
	bool bNeedsReplacement = false;
	if (!FCastleBadgeDefinition::TryMakeStandard(ECastleBadgeType::UpgradeRoomCards, Badge)
		|| !Badges->TryAcquireBadge(Badge, bNeedsReplacement, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestEqual(TEXT("已激活两张升级赠牌"), Badges->GetUpgradeBonusRoomCards(), int64{2});
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
	auto* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ShieldTower);
	auto* Board = CastleVerticalMergeTests::MakeBoard(World, Definition);
	if (!TestNotNull(TEXT("建立合成与外接邻居测试棋盘"), Board)) { return false; }
	FCastleBuildingDescriptor Level1, Level2;
	if (!Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Level1, Reason)
		|| !Definition->CreateDescriptor(2, ECastleTileColor::Red, 3, Level2, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	const FIntVector Target = Board->GridOrigin;
	const FIntVector SurvivingCell = Target + FIntVector(1, 0, 0);
	const FIntVector NeighborCell = Target + FIntVector(2, 0, 0);
	auto* Original = Board->PlaceBuilding(Target, Level2, false, Reason);
	auto* Neighbor = Board->PlaceBuilding(NeighborCell, Level1, false, Reason);
	if (!CastleBuildingTests::CompletePlacement(Original) || !CastleBuildingTests::CompletePlacement(Neighbor))
	{
		AddError(Reason.ToString());
		return false;
	}
	auto* Backpack = Board->PrototypeBackpack.Get();
	int32 Index = INDEX_NONE;
	bool bOverflow = false;
	AddExpectedError(TEXT("建筑栏仅允许一级单格建筑卡"), EAutomationExpectedErrorFlags::Contains, 3);
	TestFalse(TEXT("背包拒绝写入多格卡"), Backpack->TryAddItem(Level2, Index, Reason));
	TestFalse(TEXT("背包拒绝插入多格卡"), Backpack->TryInsertItemAt(0, Level2, Reason));
	TestFalse(TEXT("允许溢出的入口也拒绝多格卡"), Backpack->TryAddItemAllowOverflow(Level2, bOverflow, Index, Reason));
	AddExpectedError(TEXT("拒绝无坐标回收建筑"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("无坐标回收整栋接口拒绝多格建筑"), Board->RemoveBuildingToBackpack(Original, Backpack, Reason));
	if (!Backpack->TrySetCapacity(1, Reason) || !Backpack->TryAddItem(Level1, Index, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestFalse(TEXT("满背包时拒绝单格拆除"), Board->DemolishBuildingCellToBackpack(Original, Target, Backpack, Reason));
	TestFalse(TEXT("不属于原建筑的坐标不可拆除"), Board->DemolishBuildingCellToBackpack(Original, NeighborCell, Backpack, Reason));
	TestTrue(TEXT("拒绝保持两座 Actor、原有格子与唯一背包卡"), Board->GetBuildingAt(Target) == Original
		&& Board->GetBuildingAt(SurvivingCell) == Original && Board->GetBuildingAt(NeighborCell) == Neighbor
		&& Backpack->GetItemCount() == 1 && Backpack->GetTempItemCount() == 0);
	FCastleBuildingDescriptor RemovedCard;
	if (!Backpack->TryRemoveItemAt(Index, RemovedCard, Reason)
		|| !Board->DemolishBuildingCellToBackpack(Original, Target, Backpack, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	const auto* Combined = Board->GetBuildingAt(SurvivingCell);
	TestTrue(TEXT("降级一级与已落定外部相邻一级立即合成"), Combined && Combined != Original && Combined != Neighbor
		&& Combined->GetBuildingLevel() == 2 && Board->GetBuildingAt(NeighborCell) == Combined);
	TestNull(TEXT("只有目标格清空"), Board->GetBuildingAt(Target));
	TestEqual(TEXT("建筑栏只有目标格退回的一张牌"), Backpack->GetItemCount(), 1);
	TestEqual(TEXT("临时建筑栏没有升级赠牌"), Backpack->GetTempItemCount(), 0);
	return true;
}

/** 二级允许垂直后，四格直线、L 形和斜接仍不是合法三级；跨属性或跨定义仍不得升级。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleVerticalMergeRejectionTest, "MovingCastle.Logic.VerticalMerge.RejectInvalidShapesAndCards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleVerticalMergeRejectionTest::RunTest(const FString& Parameters)
{
	UWorld* World = CastleVerticalMergeTests::MakeWorld();
	if (!TestNotNull(TEXT("创建非法形状测试世界"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
	UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::Farm);
	ACastleBoard* Board = CastleVerticalMergeTests::MakeBoard(World, Definition);
	if (!TestNotNull(TEXT("配置非法形状测试棋盘"), Board)) { return false; }
	const TArray<TArray<FIntVector>> InvalidShapes = {
		{ {0,0,0}, {0,0,1}, {0,0,2}, {0,0,3} }, // 四格垂直直线只会形成两个二级。
		{ {0,0,0}, {1,0,0}, {2,0,0}, {3,0,0} }, // 水平四格直线同样不能合成三级。
		{ {0,0,0}, {0,0,1}, {1,0,0}, {2,0,0} }, // L 形由一座垂直二级与一座水平二级组成。
		{ {0,0,0}, {0,0,1}, {1,1,0}, {1,1,1} } // 两对只斜向接触，不属于六面邻接。
	};
	FText Reason;
	FCastleBuildingDescriptor Card;
	if (!Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason)) { return false; }
	for (const TArray<FIntVector>& Shape : InvalidShapes)
	{
		for (const FIntVector& Cell : Shape)
		{
			ACastleBuildingBase* Placed = Board->PlaceBuilding(Board->GridOrigin + Cell, Card, true, Reason);
			if (!TestNotNull(TEXT("非法合成形状仍允许逐格合法放置"), Placed)) { AddError(Reason.ToString()); return false; }
			if (!CastleBuildingTests::CompletePlacement(Placed)) { return false; }
		}
		TestEqual(TEXT("非法四格形状保留两座二级"), Board->GetPlacedBuildingCount(), 2);
		TestNull(TEXT("非法形状不生成三级"), CastleBuildingTests::FindPlacedLevel(Board, 3));
		TestEqual(TEXT("未升级也不丢失占格"), Board->GetOccupiedRoomCellCount(), 4);
		for (ACastleBuildingBase* Building : Board->GetPlacedBuildings())
		{
			if (!Board->RemoveBuilding(Building, Reason)) { AddError(Reason.ToString()); return false; }
		}
	}
	// 各卡仍能通过颜色或数字匹配合法堆叠，但合成要求颜色、数字、定义和等级全部相同。
	TArray<FCastleBuildingDescriptor> DifferentCards;
	FCastleBuildingDescriptor Different = Card;
	Different.Color = ECastleTileColor::Blue;
	DifferentCards.Add(Different);
	Different = Card;
	Different.Number = 4;
	DifferentCards.Add(Different);
	Different = Card;
	Different.Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::Farm);
	DifferentCards.Add(Different);
	Different = Card;
	Different.Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::CannonTower);
	DifferentCards.Add(Different);
	for (const FCastleBuildingDescriptor& OtherCard : DifferentCards)
	{
		ACastleBuildingBase* Lower = Board->PlaceBuilding(Board->GridOrigin, Card, true, Reason);
		if (!TestNotNull(TEXT("放置属性对照的底层建筑"), Lower) || !CastleBuildingTests::CompletePlacement(Lower)) { return false; }
		ACastleBuildingBase* Upper = Board->PlaceBuilding(Board->GridOrigin + FIntVector(0, 0, 1), OtherCard, true, Reason);
		if (!TestNotNull(TEXT("颜色或数字匹配允许垂直放置"), Upper) || !CastleBuildingTests::CompletePlacement(Upper)) { return false; }
		TestEqual(TEXT("属性或定义不同的上下两格不合成"), Board->GetPlacedBuildingCount(), 2);
		if (!Board->RemoveBuilding(Lower, Reason) || !Board->RemoveBuilding(Upper, Reason)) { return false; }
	}
	return true;
}

/** 后放的上层先飞入完成时不能提前合成；下层完成事件必须重新检查垂直邻居。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleVerticalMergeDeferredTest, "MovingCastle.Logic.VerticalMerge.WaitForBothFlights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleVerticalMergeDeferredTest::RunTest(const FString& Parameters)
{
	UWorld* World = CastleVerticalMergeTests::MakeWorld();
	if (!TestNotNull(TEXT("创建乱序飞入测试世界"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
	UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::Farm);
	ACastleBoard* Board = CastleVerticalMergeTests::MakeBoard(World, Definition);
	if (!TestNotNull(TEXT("配置乱序飞入测试棋盘"), Board)) { return false; }
	FText Reason;
	FCastleBuildingDescriptor Card;
	if (!Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason)) { return false; }
	ACastleBuildingBase* Lower = Board->PlaceBuilding(Board->GridOrigin, Card, false, Reason);
	ACastleBuildingBase* Upper = Board->PlaceBuilding(Board->GridOrigin + FIntVector(0, 0, 1), Card, true, Reason);
	if (!TestNotNull(TEXT("底层飞入中"), Lower) || !TestNotNull(TEXT("上层飞入中"), Upper)) { return false; }
	if (!CastleBuildingTests::CompletePlacement(Upper)) { return false; }
	TestEqual(TEXT("仅上层完成时保留两座一级"), Board->GetPlacedBuildingCount(), 2);
	TestNull(TEXT("下层未落稳前不生成二级"), CastleBuildingTests::FindPlacedLevel(Board, 2));
	if (!CastleBuildingTests::CompletePlacement(Lower)) { return false; }
	ACastleBuildingBase* Result = CastleBuildingTests::FindPlacedLevel(Board, 2);
	if (!TestNotNull(TEXT("下层完成后重新检查并垂直合成"), Result)) { return false; }
	TestEqual(TEXT("合成后只有一个 Actor"), Board->GetPlacedBuildingCount(), 1);
	TestEqual(TEXT("上下两格均被结果占用"), Board->GetOccupiedRoomCellCount(), 2);
	TestTrue(TEXT("垂直合并动画启动"), Result->BuildingMerge->bIsPlaying);
	return true;
}

/** 正式 DA 与拆件蓝图验证 X/Y/Z、XY/XZ/YZ 六种合成，避免原生夹具掩盖垂直类漏配。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDaoDunMergeBlueprintsTest, "MovingCastle.Logic.VerticalMerge.DaoDunBlueprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDaoDunMergeBlueprintsTest::RunTest(const FString& Parameters)
{
	UWorld* World = CastleVerticalMergeTests::MakeWorld();
	if (!TestNotNull(TEXT("创建正式模型合成世界"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	const TArray<TArray<FIntVector>> Shapes = {
		{ {0,0,0}, {1,0,0} }, { {0,0,0}, {0,1,0} }, { {0,0,0}, {0,0,1} },
		{ {0,0,0}, {1,0,0}, {0,1,0}, {1,1,0} },
		{ {0,0,0}, {0,0,1}, {1,0,0}, {1,0,1} },
		{ {0,0,0}, {0,0,1}, {0,1,0}, {0,1,1} }
	};
	for (const TCHAR* Family : { TEXT("ArcherTower"), TEXT("CannonTower"), TEXT("Farm"), TEXT("ShieldTower") })
	{
		const FString Path = FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), Family, Family);
		UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr, *Path);
		if (!TestNotNull(TEXT("加载正式功能建筑定义"), Definition)) { return false; }
		FText Reason;
		if (!Definition->ValidateConfiguration(Reason)) { AddError(Reason.ToString()); return false; }
		ACastleBoard* Board = CastleVerticalMergeTests::MakeBoard(World, Definition);
		if (!TestNotNull(TEXT("创建正式定义测试棋盘"), Board)) { return false; }
		for (const TArray<FIntVector>& Cells : Shapes)
		{
			FCastleBuildingDescriptor Card;
			if (!Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason)) { AddError(Reason.ToString()); return false; }
			FIntVector Max = FIntVector::ZeroValue;
			for (const FIntVector& Cell : Cells)
			{
				Max = Max.ComponentMax(Cell);
				ACastleBuildingBase* Placed = Board->PlaceBuilding(Board->GridOrigin + Cell, Card, true, Reason);
				if (!TestNotNull(TEXT("正式一级拆件模型可放置"), Placed)) { AddError(Reason.ToString()); return false; }
				if (!TestTrue(TEXT("正式飞入完成并触发合成"), CastleBuildingTests::CompletePlacement(Placed))) { return false; }
			}
			const int32 Level = Cells.Num() == 2 ? 2 : 3;
			ACastleBuildingBase* Result = CastleBuildingTests::FindPlacedLevel(Board, Level);
			if (!TestNotNull(TEXT("合成得到正式目标等级"), Result)) { return false; }
			const FString Suffix = Max.Z == 1 ? TEXT("_vertical") : TEXT("");
			const FString ExpectedClass = FString::Printf(TEXT("/Game/MovingCastle/BP/BP_%s_lv%d%s.BP_%s_lv%d%s_C"),
				Family, Level, *Suffix, Family, Level, *Suffix);
			TestEqual(TEXT("合成精确选用对应水平或垂直蓝图"), Result->GetClass()->GetPathName(), ExpectedClass);
			FQuat Rotation = Board->GetActorQuat();
			if (Max.X == 0 && Max.Y == 1) { Rotation *= FQuat(FVector::UpVector, HALF_PI); }
			TestTrue(TEXT("Y/YZ 整体旋转 90 度，其余保持棋盘朝向"), Result->GetActorQuat().Equals(Rotation, 0.001));
			TestTrue(TEXT("正式蓝图通过视觉非空检查"), Result->ConfigureBuildingVisual(Board->CellSize, Board->LayerHeight, Reason));
			Result->BuildingMerge->TickComponent(100.0f, LEVELTICK_All, nullptr);
			if (!Board->RemoveBuilding(Result, Reason)) { AddError(Reason.ToString()); return false; }
		}
		Board->Destroy();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
