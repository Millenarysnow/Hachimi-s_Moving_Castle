// 建造锚点回归测试：通过真实放置与合成验证底面定位，不依赖用户正在编辑的美术蓝图。

#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/PCG/CastlePCGSemanticTestRules.h"
#include "Misc/AutomationTest.h"

#include "CastleBuildingTestUtilities.h"
#include "CastleBackpackComponent.h"
#include "Castle/PCG/CastleBuildingPCGRule.h"
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
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);

	// 非零原点、倾斜旋转和非均匀缩放能发现沿世界 Z 直接减高度造成的错误。
	const FTransform BoardTransform(FRotator(18.0f, 37.0f, -11.0f), FVector(120.0f, -80.0f, 250.0f), FVector(1.25f, 0.8f, 1.5f));
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>(ACastleBoard::StaticClass(), BoardTransform, SpawnParameters);
	if (!TestNotNull(TEXT("生成变换后的测试棋盘"), Board)) { return false; }
	Board->GridOrigin = FIntVector(4, -3, 2);
	Board->GridSize = FIntVector(2, 2, 3);
	Board->CellSize = 600.0f;
	// 本用例故意使用非默认格距验证变换；实体格面与间隙也须显式组成同一个中心距。
	Board->GroundTileSize = 580.0f;
	Board->GroundTileGap = 20.0f;
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
		UStaticMeshComponent* Mesh = FindObject<UStaticMeshComponent>(Building, TEXT("TestBuildingModel"));
		if (!TestNotNull(FString::Printf(TEXT("%s：显式测试模型存在"), Phase), Mesh)
			|| !TestTrue(FString::Printf(TEXT("%s：测试模型已配置网格"), Phase), Mesh->GetStaticMesh() != nullptr)) { return false; }
		const FBox BoundsInBoard = Mesh->GetStaticMesh()->GetBoundingBox().TransformBy(
			Mesh->GetComponentTransform().ToMatrixWithScale() * BoardTransform.ToInverseMatrixWithScale());
		bValid &= TestTrue(FString::Printf(TEXT("%s：作者配置的模型底面与建筑锚点同高，实际 %.3f，预期 %.3f"),
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
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	if (!TestNotNull(TEXT("创建测试棋盘"), Board)) { return false; }
	Board->GridSize = FIntVector(3, 3, 3);
	CastleBuildingTests::ConfigureBoard(Board);
	if (!CastlePCGTests::ConfigurePlacementOnlyPCG(*Board, *this)) { return false; }
	UCastleBuildingDefinition* Definition = Board->BuildingDefinitions[1];
	UCastleBuildingDefinition* NormalRoomDefinition = Board->BuildingDefinitions[0];
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
		}
	}
	if (!TestTrue(TEXT("配置包含匹配邻地块的异色棋盘"), Board->ApplyGroundGenerationResult(Ground, Reason))) { return false; }
	FCastleBuildingDescriptor RedCard, LeftCard, RightCard, NormalRoomCard;
	if (!Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, RedCard, Reason)
		|| !Definition->CreateDescriptor(1, ECastleTileColor::Red, 1, LeftCard, Reason)
		|| !Definition->CreateDescriptor(1, ECastleTileColor::Yellow, 7, RightCard, Reason)
		|| !NormalRoomDefinition->CreateDescriptor(1, ECastleTileColor::Red, 3, NormalRoomCard, Reason)) { return false; }
	ACastleBuildingBase* Left = Board->PlaceBuilding(FIntVector(0, 1, 0), LeftCard, false, Reason);
	ACastleBuildingBase* Right = Board->PlaceBuilding(FIntVector(2, 1, 0), RightCard, false, Reason);
	if (!TestNotNull(TEXT("放置左侧匹配建筑"), Left) || !TestNotNull(TEXT("放置右侧不匹配建筑"), Right)) { return false; }
	if (!CastleBuildingTests::CompletePlacement(Left) || !CastleBuildingTests::CompletePlacement(Right)) { return false; }

	// 使用真实射线解析地面和左右两个建筑侧面，覆盖玩家入口而非仅手工传入落点。
	const FIntVector Destination(1, 1, 0);
	const FVector Center = Board->GetGridWorldLocation(Destination);
	FCastleGridCursorTarget FloorTarget, LeftTarget, RightTarget;
	// 侧面射线对准实际美术模型，不能再假定存在一个填满整个格子高度的自动占位方块。
	const auto ResolveSide = [Board, Center](const ACastleBuildingBase* Building, FCastleGridCursorTarget& Target)
	{
		const FVector ModelCenter = Building->GetVisualBounds().GetCenter();
		return Board->TryResolveGridCursorTarget(FVector(Center.X, Center.Y, ModelCenter.Z), ModelCenter, Target);
	};
	if (!TestTrue(TEXT("射线命中中央地面"), Board->TryResolveGridCursorTarget(Center + FVector(0, 0, Board->LayerHeight),
		Center - FVector(0, 0, Board->LayerHeight), FloorTarget))
		|| !TestTrue(TEXT("射线命中左侧建筑面"), ResolveSide(Left, LeftTarget))
		|| !TestTrue(TEXT("射线命中右侧建筑面"), ResolveSide(Right, RightTarget))) { return false; }
	TestEqual(TEXT("地面来源被保留"), FloorTarget.Surface, ECastleGridCursorSurface::Ground);
	TestEqual(TEXT("建筑来源不会误认成下方地面"), LeftTarget.Surface, ECastleGridCursorSurface::Building);
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
	TestTrue(TEXT("普通房间可依靠同色功能建筑面放置"), Board->CanPlaceBuildingAtCursor(LeftTarget, NormalRoomCard, Reason));
	FCastleBuildingDescriptor NumberMatchedRoom;
	NormalRoomDefinition->CreateDescriptor(1, ECastleTileColor::Green, 1, NumberMatchedRoom, Reason);
	TestTrue(TEXT("普通房间也可只依靠功能建筑的同数字面放置"), Board->CanPlaceBuildingAtCursor(LeftTarget, NumberMatchedRoom, Reason));
	TestFalse(TEXT("普通房间不能借未高亮的匹配邻居放置"), Board->CanPlaceBuildingAtCursor(RightTarget, NormalRoomCard, Reason));

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

	// 普通房间从同一真实背包入口依靠功能建筑侧面放置，证明新规则不限于预览高亮。
	if (!Board->PrototypeBackpack->TryAddItem(NormalRoomCard, CardIndex, Reason)) { return false; }
	ACastleBuildingBase* RoomOnTowerFace = Board->PlaceBackpackBuildingAtCursor(LeftTarget, CardIndex, Reason);
	if (!TestNotNull(TEXT("普通房间实际依靠功能建筑面建成"), RoomOnTowerFace)) { AddError(Reason.ToString()); return false; }
	if (!CastleBuildingTests::CompletePlacement(RoomOnTowerFace) || !Board->RemoveBuilding(RoomOnTowerFace, Reason)) { return false; }

	// 同一个匹配面可以放置部分悬空的多格建筑；坐标入口也不再要求其余底面有支撑。
	FCastleGridCursorTarget TopTarget = LeftTarget;
	TopTarget.Face = ECastleGridFace::Up;
	TopTarget.AdjacentCell = TopTarget.Cell + FIntVector(0, 0, 1);
	TestTrue(TEXT("匹配的建筑顶面可放置上层单格"), Board->CanPlaceBuildingAtCursor(TopTarget, RedCard, Reason));
	FCastleBuildingDescriptor WideCard;
	if (!Definition->CreateDescriptor(2, ECastleTileColor::Red, 3, WideCard, Reason)) { return false; }
	TestTrue(TEXT("有匹配面允许双格建筑部分悬空"), Board->CanPlaceBuildingAtCursor(TopTarget, WideCard, Reason));
	TestTrue(TEXT("坐标入口只需锚点下方匹配，不要求完整底面支撑"), Board->CanPlaceBuilding(TopTarget.AdjacentCell, WideCard, Reason));
	// 多格描述仍可按光标高亮面直接生成。本用例验证该几何行为，背包只接收一级。
	ACastleBuildingBase* FloatingWide = Board->PlaceBuildingAtCursor(TopTarget, WideCard, false, Reason);
	if (!TestNotNull(TEXT("直接生成部分悬空的双格建筑"), FloatingWide)) { AddError(Reason.ToString()); return false; }
	if (!CastleBuildingTests::CompletePlacement(FloatingWide)) { return false; }
	// 从公开建筑列表检查实际三维占格，不为测试开放棋盘内部的受保护索引查询。
	const auto HasPlacedCell = [Board](const FIntVector& Cell)
	{
		return Board->GetPlacedBuildings().ContainsByPredicate([&Cell](const ACastleBuildingBase* Building)
		{
			return Building->GetOccupiedCoordinates().Contains(Cell);
		});
	};
	TestTrue(TEXT("悬空部分保留实际占格"), FloatingWide->GetOccupiedCoordinates().Contains(FIntVector(1, 1, 1)));
	TestEqual(TEXT("双格建筑正确登记两格占用"), Board->GetOccupiedRoomCellCount(), 4);
	TestFalse(TEXT("双格建筑第二格正下方确实为空"), HasPlacedCell(Destination));
	TestEqual(TEXT("直接生成不变更背包库存"), Board->PrototypeBackpack->GetItemCount(), 0);

	// 从已完成的上层建筑真实侧面解析光标，再走背包入口生成下方完全没有建筑的普通房间。
	FCastleGridCursorTarget FloatingSide;
	const FVector WideCenter = FloatingWide->GetVisualBounds().GetCenter();
	if (!TestTrue(TEXT("真实射线选中上层建筑侧面"), Board->TryResolveGridCursorTarget(
		WideCenter + FVector(0, Board->CellSize, 0), WideCenter, FloatingSide))) { return false; }
	TestEqual(TEXT("射线选中朝 +Y 的建筑面"), FloatingSide.Face, ECastleGridFace::PositiveY);
	TestEqual(TEXT("侧面落点仍在第二层"), FloatingSide.AdjacentCell.Z, 1);
	TestFalse(TEXT("侧面落点正下方没有建筑"), HasPlacedCell(FloatingSide.AdjacentCell - FIntVector(0, 0, 1)));
	TestTrue(TEXT("同色普通房间允许侧向悬空"), Board->CanPlaceBuildingAtCursor(FloatingSide, NormalRoomCard, Reason));
	TestFalse(TEXT("悬空放置仍拒绝颜色数字均不匹配的卡牌"), Board->CanPlaceBuildingAtCursor(FloatingSide, ColorOnly, Reason));
	if (!Board->PrototypeBackpack->TryAddItem(NormalRoomCard, CardIndex, Reason)) { return false; }
	ACastleBuildingBase* FloatingRoom = Board->PlaceBackpackBuildingAtCursor(FloatingSide, CardIndex, Reason);
	if (!TestNotNull(TEXT("普通房间实际侧向悬空建成"), FloatingRoom)) { AddError(Reason.ToString()); return false; }
	if (!CastleBuildingTests::CompletePlacement(FloatingRoom)) { return false; }
	TestTrue(TEXT("悬空房间占用光标指定落点"), FloatingRoom->GetOccupiedCoordinates().Contains(FloatingSide.AdjacentCell));
	TestEqual(TEXT("悬空房间正确登记一格占用"), Board->GetOccupiedRoomCellCount(), 5);
	TestFalse(TEXT("悬空落点占用后仍拒绝重复放置"), Board->CanPlaceBuildingAtCursor(FloatingSide, RedCard, Reason));
	if (!Board->RemoveBuilding(FloatingRoom, Reason) || !Board->RemoveBuilding(FloatingWide, Reason)) { return false; }

	// 撤销下方支撑规则不改变匹配来源的有效性要求，失效的旧高亮面仍须明确拒绝。
	if (!Board->RemoveBuilding(Left, Reason)) { return false; }
	TestFalse(TEXT("来源建筑消失后拒绝旧高亮面"), Board->CanPlaceBuildingAtCursor(LeftTarget, RedCard, Reason));

	// 功能建筑与普通房间双向使用同色或同数字匹配，不再存在类别隔离。
	ACastleBuildingBase* NormalRoom = Board->PlaceBuilding(FIntVector(0, 0, 0), NormalRoomCard, false, Reason);
	if (!TestNotNull(TEXT("在五色地面放置普通房间"), NormalRoom)) { return false; }
	if (!CastleBuildingTests::CompletePlacement(NormalRoom)) { return false; }
	FCastleGridCursorTarget NormalRoomTarget;
	NormalRoomTarget.bValid = true;
	NormalRoomTarget.Cell = FIntVector(0, 0, 0);
	NormalRoomTarget.Face = ECastleGridFace::PositiveY;
	NormalRoomTarget.AdjacentCell = FIntVector(0, 1, 0);
	TestTrue(TEXT("功能建筑可依靠普通房间同色面匹配"), Board->CanPlaceBuildingAtCursor(NormalRoomTarget, RedCard, Reason));
	FCastleBuildingDescriptor NumberMatchedTower;
	Definition->CreateDescriptor(1, ECastleTileColor::Green, 3, NumberMatchedTower, Reason);
	TestTrue(TEXT("功能建筑可只依靠普通房间同数字面匹配"), Board->CanPlaceBuildingAtCursor(NormalRoomTarget, NumberMatchedTower, Reason));
	if (!Board->PrototypeBackpack->TryAddItem(NumberMatchedTower, CardIndex, Reason)) { return false; }
	ACastleBuildingBase* TowerOnRoomFace = Board->PlaceBackpackBuildingAtCursor(NormalRoomTarget, CardIndex, Reason);
	if (!TestNotNull(TEXT("功能建筑实际依靠普通房间侧面建成"), TowerOnRoomFace)) { AddError(Reason.ToString()); return false; }
	if (!CastleBuildingTests::CompletePlacement(TowerOnRoomFace) || !Board->RemoveBuilding(TowerOnRoomFace, Reason)) { return false; }

	// 使用开启自动合成的正式放置路径；相同定义、颜色和数字也不能升级普通房间。
	ACastleBuildingBase* SecondRoom = Board->PlaceBuilding(FIntVector(0, 1, 0), NormalRoomCard, true, Reason);
	if (!TestNotNull(TEXT("同属性普通房间可紧贴放置"), SecondRoom)) { return false; }
	if (!CastleBuildingTests::CompletePlacement(SecondRoom)) { return false; }
	TestTrue(TEXT("两座普通房间保持独立 Lv1 且不能合成"), IsValid(NormalRoom) && IsValid(SecondRoom)
		&& NormalRoom != SecondRoom && NormalRoom->GetBuildingLevel() == 1 && SecondRoom->GetBuildingLevel() == 1
		&& !NormalRoom->CanMergeWith(SecondRoom) && !NormalRoom->CanMergeToNextLevel());
	TestEqual(TEXT("棋盘保留两座普通房间及原先右侧功能塔"), Board->GetPlacedBuildingCount(), 3);
	return true;
}

#endif
