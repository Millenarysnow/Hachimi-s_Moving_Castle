// 建造锚点回归测试：通过真实放置与合成验证底面定位，不依赖用户正在编辑的美术蓝图。

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CastleBuildingTestUtilities.h"
#include "CastleBackpackComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/** 验证单层、多层、上层放置和垂直合成始终使用占用范围底面中心，并支持棋盘完整变换。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingBottomCenterPlacementTest, "MovingCastle.Logic.BuildingBottomCenterPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingBottomCenterPlacementTest::RunTest(const FString& Parameters)
{
	// 创建独立游戏世界和销毁上下文，所有测试建筑均走正式的棋盘放置、飞入和合成流程。
	const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建底部锚点测试世界"), World)) { return false; }
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

	// 非零原点、倾斜旋转和非均匀缩放能发现沿世界 Z 直接减高度造成的错误。
	const FTransform BoardTransform(FRotator(18.0f, 37.0f, -11.0f), FVector(120.0f, -80.0f, 250.0f), FVector(1.25f, 0.8f, 1.5f));
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>(ACastleBoard::StaticClass(), BoardTransform, SpawnParameters);
	if (!TestNotNull(TEXT("生成变换后的测试棋盘"), Board)) { return false; }
	Board->GridOrigin = FIntVector(4, -3, 2);
	Board->GridSize = FIntVector(2, 2, 3);
	Board->CellSize = 600.0f;
	Board->LayerHeight = 400.0f;
	Board->WorldBaseHeight = 300.0f;
	UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	Board->BuildingDefinitions = { Definition };
	FCastleGroundGenerationResult Ground;
	for (int32 X = 0; X < 2; ++X)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
			Cell.Coordinate = Board->GridOrigin + FIntVector(X, Y, 0);
			Cell.Color = ECastleTileColor::Red;
			Cell.Number = 3;
		}
	}
	FText Reason;
	if (!TestTrue(TEXT("配置四格支撑地面"), Board->ApplyGroundGenerationResult(Ground, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}

	// 检查实际生成位置和真实立方体几何底面，避免只校验一个与实现重复的坐标辅助函数。
	const auto CheckBottomAnchor = [this, &BoardTransform](ACastleBuildingBase* Building, const FVector& ExpectedLocal, const TCHAR* Phase)
	{
		if (!TestNotNull(FString::Printf(TEXT("%s：建筑存在"), Phase), Building)) { return false; }
		const FVector ExpectedWorld = BoardTransform.TransformPosition(ExpectedLocal);
		bool bValid = TestTrue(FString::Printf(TEXT("%s：Actor 原点位于预期底面中心，实际 %s，预期 %s"),
			Phase, *Building->GetActorLocation().ToString(), *ExpectedWorld.ToString()),
			Building->GetActorLocation().Equals(ExpectedWorld, 0.01f));
		UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Building->GetDefaultSubobjectByName(TEXT("PrototypeMesh")));
		if (!TestNotNull(FString::Printf(TEXT("%s：原生占位组件存在"), Phase), Mesh)
			|| !TestTrue(FString::Printf(TEXT("%s：占位组件已配置网格"), Phase), Mesh->GetStaticMesh() != nullptr)) { return false; }
		const FBox BoundsInBoard = Mesh->GetStaticMesh()->GetBoundingBox().TransformBy(
			Mesh->GetComponentTransform().ToMatrixWithScale() * BoardTransform.ToInverseMatrixWithScale());
		bValid &= TestTrue(FString::Printf(TEXT("%s：占位立方体底面与建筑锚点同高，实际 %.3f，预期 %.3f"),
			Phase, BoundsInBoard.Min.Z, ExpectedLocal.Z), FMath::IsNearlyEqual(BoundsInBoard.Min.Z, ExpectedLocal.Z, 0.01));
		return bValid;
	};

	// 固定测试棋盘的首层底面为局部 Z=100，二/三级范围中心为 300；四级占两层也不能抬高锚点。
	const FVector ExpectedFirstFloor[] = {
		FVector(0.0f, 0.0f, 100.0f), FVector(300.0f, 0.0f, 100.0f),
		FVector(300.0f, 300.0f, 100.0f), FVector(300.0f, 300.0f, 100.0f), FVector(0.0f, 0.0f, 100.0f)
	};
	for (int32 Level = 1; Level <= 5; ++Level)
	{
		FCastleBuildingDescriptor Card;
		if (!Definition->CreateDescriptor(Level, ECastleTileColor::Red, 3, Card, Reason)) { AddError(Reason.ToString()); return false; }
		ACastleBuildingBase* Building = Board->PlaceBuilding(Board->GridOrigin, Card, false, Reason);
		if (!TestNotNull(TEXT("按等级直接放置成功"), Building)) { AddError(Reason.ToString()); return false; }
		if (!TestTrue(TEXT("放置飞入正常结束"), CastleBuildingTests::CompletePlacement(Building))) { return false; }
		if (!CheckBottomAnchor(Building, ExpectedFirstFloor[Level - 1], *FString::Printf(TEXT("直接放置 %d 级"), Level))) { return false; }
		if (!Board->RemoveBuilding(Building, Reason)) { AddError(Reason.ToString()); return false; }
	}

	// 两座平面四格三级塔上下堆叠后合成四级，最终锚点应回到联合占用范围的最低底面。
	FCastleBuildingDescriptor Level3Card;
	if (!Definition->CreateDescriptor(3, ECastleTileColor::Red, 3, Level3Card, Reason)) { AddError(Reason.ToString()); return false; }
	ACastleBuildingBase* Lower = Board->PlaceBuilding(Board->GridOrigin, Level3Card, false, Reason);
	if (!TestNotNull(TEXT("放置下层三级塔"), Lower)) { AddError(Reason.ToString()); return false; }
	if (!TestTrue(TEXT("下层放置完成"), CastleBuildingTests::CompletePlacement(Lower))) { return false; }
	ACastleBuildingBase* Upper = Board->PlaceBuilding(Board->GridOrigin + FIntVector(0, 0, 1), Level3Card, true, Reason);
	if (!TestNotNull(TEXT("上层三级塔得到完整支撑"), Upper)) { AddError(Reason.ToString()); return false; }
	if (!CheckBottomAnchor(Upper, FVector(300.0f, 300.0f, 500.0f), TEXT("上层放置"))) { return false; }
	if (!TestTrue(TEXT("上层放置完成并触发合成"), CastleBuildingTests::CompletePlacement(Upper))) { return false; }
	ACastleBuildingBase* Merged = CastleBuildingTests::FindPlacedLevel(Board, 4);
	if (!CheckBottomAnchor(Merged, FVector(300.0f, 300.0f, 100.0f), TEXT("垂直合成四级"))) { return false; }
	TestEqual(TEXT("合成后只剩一座建筑"), Board->GetPlacedBuildingCount(), 1);
	TestEqual(TEXT("合成保留两层八格占用"), Board->GetOccupiedRoomCellCount(), 8);
	return true;
}

/** 同一个空格可从多个方向命中；只允许被高亮的来源面提供属性匹配，拒绝时不得消耗卡牌。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectedPlacementTest, "MovingCastle.Logic.DirectedSurfacePlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDirectedPlacementTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建定向放置测试世界"), World)) { return false; }
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	if (!TestNotNull(TEXT("创建测试棋盘"), Board)) { return false; }
	Board->GridSize = FIntVector(3, 3, 3);
	CastleBuildingTests::ConfigureBoard(Board);
	UCastleBuildingDefinition* Definition = Board->BuildingDefinitions[1];
	UCastleBuildingDefinition* RoadDefinition = Board->BuildingDefinitions[0];
	FText Reason;

	// 中央蓝 5 与红 3 卡牌不匹配，左侧红 1 建筑可匹配，右侧黄 7 建筑不可匹配。
	FCastleGroundGenerationResult Ground;
	for (int32 X = 0; X < 3; ++X)
	{
		for (int32 Y = 0; Y < 3; ++Y)
		{
			FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
			Cell.Coordinate = FIntVector(X, Y, 0);
			Cell.Color = X == 0 ? ECastleTileColor::Red : X == 1 ? ECastleTileColor::Blue : ECastleTileColor::Yellow;
			Cell.Number = X == 0 ? 1 : X == 1 ? 5 : 7;
			if (X == 1 && Y == 2) { Cell.Color = ECastleTileColor::Red; Cell.Number = 3; }
			if (X == 0 && Y == 0) { Cell.Color = ECastleTileColor::Gray; Cell.Number = 0; }
		}
	}
	if (!TestTrue(TEXT("配置包含匹配邻地块的异色棋盘"), Board->ApplyGroundGenerationResult(Ground, Reason))) { return false; }
	FCastleBuildingDescriptor RedCard, LeftCard, RightCard, RoadCard;
	if (!Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, RedCard, Reason)
		|| !Definition->CreateDescriptor(1, ECastleTileColor::Red, 1, LeftCard, Reason)
		|| !Definition->CreateDescriptor(1, ECastleTileColor::Yellow, 7, RightCard, Reason)
		|| !RoadDefinition->CreateDescriptor(1, ECastleTileColor::Gray, 0, RoadCard, Reason)) { return false; }
	ACastleBuildingBase* Left = Board->PlaceBuilding(FIntVector(0, 1, 0), LeftCard, false, Reason);
	ACastleBuildingBase* Right = Board->PlaceBuilding(FIntVector(2, 1, 0), RightCard, false, Reason);
	if (!TestNotNull(TEXT("放置左侧匹配建筑"), Left) || !TestNotNull(TEXT("放置右侧不匹配建筑"), Right)) { return false; }
	if (!CastleBuildingTests::CompletePlacement(Left) || !CastleBuildingTests::CompletePlacement(Right)) { return false; }

	// 使用真实射线解析地面和左右两个建筑侧面，覆盖玩家入口而非仅手工传入落点。
	const FIntVector Destination(1, 1, 0);
	const FVector Center = Board->GetGridWorldLocation(Destination);
	FCastleGridCursorTarget FloorTarget, LeftTarget, RightTarget;
	if (!TestTrue(TEXT("射线命中中央地面"), Board->TryResolveGridCursorTarget(Center + FVector(0, 0, Board->LayerHeight),
		Center - FVector(0, 0, Board->LayerHeight), FloorTarget))
		|| !TestTrue(TEXT("射线命中左侧建筑面"), Board->TryResolveGridCursorTarget(Center, Board->GetGridWorldLocation(FIntVector(0, 1, 0)), LeftTarget))
		|| !TestTrue(TEXT("射线命中右侧建筑面"), Board->TryResolveGridCursorTarget(Center, Board->GetGridWorldLocation(FIntVector(2, 1, 0)), RightTarget))) { return false; }
	TestTrue(TEXT("地面来源被保留"), FloorTarget.bGroundSurface);
	TestFalse(TEXT("建筑来源不会误认成下方地面"), LeftTarget.bGroundSurface);
	TestEqual(TEXT("左侧高亮面向 +X"), LeftTarget.Face, ECastleGridFace::PositiveX);
	TestEqual(TEXT("右侧高亮面向 -X"), RightTarget.Face, ECastleGridFace::NegativeX);
	TestEqual(TEXT("两侧指向同一个落点"), LeftTarget.AdjacentCell, RightTarget.AdjacentCell);
	TestFalse(TEXT("高亮蓝 5 地面不能借用红 3 邻地块或红色邻建筑匹配"), Board->CanPlaceBuildingAtCursor(FloorTarget, RedCard, Reason));
	TestFalse(TEXT("坐标接口也不能扫描周围邻格放宽匹配"), Board->CanPlaceBuilding(Destination, RedCard, Reason));
	TestFalse(TEXT("高亮黄 7 建筑不能借用另一侧红色建筑匹配"), Board->CanPlaceBuildingAtCursor(RightTarget, RedCard, Reason));
	TestTrue(TEXT("同一落点指向红色建筑面则可放置"), Board->CanPlaceBuildingAtCursor(LeftTarget, RedCard, Reason));

	FCastleBuildingDescriptor ColorOnly, NumberOnly;
	Definition->CreateDescriptor(1, ECastleTileColor::Blue, 9, ColorOnly, Reason);
	Definition->CreateDescriptor(1, ECastleTileColor::Green, 5, NumberOnly, Reason);
	TestTrue(TEXT("高亮面仅颜色相同即可"), Board->CanPlaceBuildingAtCursor(FloorTarget, ColorOnly, Reason));
	TestTrue(TEXT("高亮面仅数字相同即可"), Board->CanPlaceBuildingAtCursor(FloorTarget, NumberOnly, Reason));
	TestFalse(TEXT("道路不能依靠功能建筑面放置"), Board->CanPlaceBuildingAtCursor(LeftTarget, RoadCard, Reason));

	// 验证真实背包点击路径：失败保持槽位、数量和占用，成功才消耗卡牌并占用同一落点。
	int32 CardIndex = INDEX_NONE;
	if (!Board->PrototypeBackpack->TryAddItem(RedCard, CardIndex, Reason)) { return false; }
	TestNull(TEXT("点击不匹配的地面实际不建成"), Board->PlaceBackpackBuildingAtCursor(FloorTarget, CardIndex, Reason));
	TestFalse(TEXT("拒绝建造有明确反馈原因"), Reason.IsEmpty());
	TestEqual(TEXT("拒绝建造保留背包卡牌"), Board->PrototypeBackpack->GetItemCount(), 1);
	TestEqual(TEXT("拒绝建造不增加占用格"), Board->GetOccupiedRoomCellCount(), 2);
	TestEqual(TEXT("拒绝建造不增加房间数"), Board->GetPlacedBuildingCount(), 2);
	ACastleBuildingBase* Placed = Board->PlaceBackpackBuildingAtCursor(LeftTarget, CardIndex, Reason);
	if (!TestNotNull(TEXT("点击匹配的建筑面实际建成"), Placed)) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("成功后扣除背包卡牌"), Board->PrototypeBackpack->GetItemCount(), 0);
	TestTrue(TEXT("实际占用的是原先同一落点"), Placed->GetOccupiedCoordinates().Contains(Destination));
	if (!CastleBuildingTests::CompletePlacement(Placed) || !Board->RemoveBuilding(Placed, Reason)) { return false; }

	// 上层仍要求整个底面有支撑；来源建筑消失时也必须拒绝，不能改用其他邻格。
	FCastleGridCursorTarget TopTarget = LeftTarget;
	TopTarget.Face = ECastleGridFace::Up;
	TopTarget.AdjacentCell = TopTarget.Cell + FIntVector(0, 0, 1);
	TestTrue(TEXT("匹配的建筑顶面可支撑上层单格"), Board->CanPlaceBuildingAtCursor(TopTarget, RedCard, Reason));
	FCastleBuildingDescriptor WideCard;
	Definition->CreateDescriptor(2, ECastleTileColor::Red, 3, WideCard, Reason);
	TestFalse(TEXT("有匹配面仍不能让双格建筑部分悬空"), Board->CanPlaceBuildingAtCursor(TopTarget, WideCard, Reason));
	if (!Board->RemoveBuilding(Left, Reason)) { return false; }
	TestFalse(TEXT("来源建筑消失后拒绝旧高亮面"), Board->CanPlaceBuildingAtCursor(LeftTarget, RedCard, Reason));

	// 道路面只给道路提供匹配；即使旁边还有匹配地面也不能让功能卡借用它。
	ACastleBuildingBase* Road = Board->PlaceBuilding(FIntVector(0, 0, 0), RoadCard, false, Reason);
	if (!TestNotNull(TEXT("在道路地面放置道路"), Road)) { return false; }
	if (!CastleBuildingTests::CompletePlacement(Road)) { return false; }
	FCastleGridCursorTarget RoadTarget;
	RoadTarget.bValid = true;
	RoadTarget.Cell = FIntVector(0, 0, 0);
	RoadTarget.Face = ECastleGridFace::PositiveY;
	RoadTarget.AdjacentCell = FIntVector(0, 1, 0);
	TestFalse(TEXT("功能建筑不能借道路面匹配"), Board->CanPlaceBuildingAtCursor(RoadTarget, RedCard, Reason));
	TestTrue(TEXT("道路仍可依靠道路面延伸"), Board->CanPlaceBuildingAtCursor(RoadTarget, RoadCard, Reason));
	return true;
}

#endif
