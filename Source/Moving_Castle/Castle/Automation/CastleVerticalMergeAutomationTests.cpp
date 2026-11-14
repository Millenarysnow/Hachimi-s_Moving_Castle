// 垂直合成回归：通过正式放置、飞入、连续合成和回收入口验证三维空间占用。

#if WITH_DEV_AUTOMATION_TESTS

#include "CastleBuildingTestUtilities.h"
#include "CastleBackpackComponent.h"
#include "BuildingMergeComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace CastleVerticalMergeTests
{
	/** 使用独立游戏世界，保留实际飞入完成事件与 Actor 销毁流程，不接触用户关卡。 */
	UWorld* MakeWorld()
	{
		const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
			.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
		if (World != nullptr) { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
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
	for (const ECastleBuildingType Type : { ECastleBuildingType::ArcherTower, ECastleBuildingType::CannonTower, ECastleBuildingType::Farm })
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
			if (Shape.Level == 2 && Maximum.Y == 1) { ExpectedRotation *= FQuat(FVector::UpVector, HALF_PI); }
			TestTrue(Context + TEXT("：仅水平 Y 双格旋转模型，垂直不侧翻"), Result->GetActorQuat().Equals(ExpectedRotation, 0.001));
			TestTrue(Context + TEXT("：仍使用同一建筑定义"), Result->GetBuildingDefinition() == Definition);
			TestTrue(Context + TEXT("：合并动画实际开始"), Result->BuildingMerge->bIsPlaying);
			Result->BuildingMerge->TickComponent(100.0f, LEVELTICK_All, nullptr);
			TestFalse(Context + TEXT("：合并动画正常结束"), Result->BuildingMerge->bIsPlaying);

			// 垂直建筑回收必须释放各层索引；背包保留等级身份，再次放置仍采用既有标准朝向。
			if (!TestTrue(Context + TEXT("：回收成功"), Board->RemoveBuildingToBackpack(Result, Board->PrototypeBackpack, Reason))) { return false; }
			TestEqual(Context + TEXT("：回收清空全部占格"), Board->GetOccupiedRoomCellCount(), 0);
			ACastleBuildingBase* Replaced = Board->PlaceBuildingFromBackpack(Board->GridOrigin, Board->PrototypeBackpack, 0, false, Reason);
			if (!TestNotNull(Context + TEXT("：回收卡可重新放置"), Replaced)) { AddError(Reason.ToString()); return false; }
			if (!CastleBuildingTests::CompletePlacement(Replaced)) { return false; }
			TestEqual(Context + TEXT("：回收后等级不变"), Replaced->GetBuildingLevel(), Shape.Level);
			TestEqual(Context + TEXT("：重新放置仅消耗一张卡"), Board->PrototypeBackpack->GetItemCount(), 0);
			if (!Board->RemoveBuilding(Replaced, Reason)) { AddError(Reason.ToString()); return false; }
			AddInfo(Context + TEXT("：占格、锚点、动画与回收验证通过。"));
		}
	}
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

#endif // WITH_DEV_AUTOMATION_TESTS
