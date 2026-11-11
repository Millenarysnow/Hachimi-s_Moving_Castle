// 建造锚点回归测试：通过真实放置与合成验证底面定位，不依赖用户正在编辑的美术蓝图。

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CastleBuildingTestUtilities.h"
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

#endif
