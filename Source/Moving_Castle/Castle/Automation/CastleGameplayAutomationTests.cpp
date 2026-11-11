// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildings.h"
#include "CastleBuildingTestUtilities.h"
#include "CastleGroundGenerator.h"
#include "CastleGroundTileActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	/** 为 Actor 级棋盘格测试建立独立游戏 World，避免改动用户关卡。 */
	struct FScopedGroundActorWorld
	{
		UWorld* World = nullptr;

		FScopedGroundActorWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
			if (World != nullptr)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
		}

		~FScopedGroundActorWorld()
		{
			if (World != nullptr)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}
	};
}

/**
 * 验证随机地面生成器的确定性、道路起始格和无效请求拒绝逻辑。
 *
 * 该测试只依赖 UObject 数据对象，不创建任何美术资源，因此可以在编辑器的
 * NullRHI 模式下运行，也能保证后续替换生成策略时接口行为不会悄悄改变。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCastleRandomGroundGeneratorAutomationTest,
	"MovingCastle.Logic.RandomGroundGenerator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleRandomGroundGeneratorAutomationTest::RunTest(const FString& Parameters)
{
	UCastleRandomGroundGenerator* Generator = NewObject<UCastleRandomGroundGenerator>();
	TestNotNull(TEXT("随机地面生成器对象应该创建成功"), Generator);
	if (Generator == nullptr)
	{
		return false;
	}

	FCastleGroundGenerationRequest Request;
	Request.GridSize = FIntVector(4, 3, 4);
	Request.Origin = FIntVector(10, 20, 0);
	Request.Seed = 20261107;
	Request.bReserveRoadStartTile = true;

	const FCastleGroundGenerationResult FirstResult = Generator->GenerateGround(Request);
	const FCastleGroundGenerationResult SecondResult = Generator->GenerateGround(Request);
	TestEqual(TEXT("随机地面结果应覆盖所有平面格子"), FirstResult.Cells.Num(), 12);
	TestEqual(TEXT("相同种子应生成相同数量的格子"), SecondResult.Cells.Num(), FirstResult.Cells.Num());

	if (FirstResult.Cells.Num() == SecondResult.Cells.Num())
	{
		for (int32 Index = 0; Index < FirstResult.Cells.Num(); ++Index)
		{
			TestTrue(TEXT("相同种子生成的坐标应一致"), FirstResult.Cells[Index].Coordinate == SecondResult.Cells[Index].Coordinate);
			TestTrue(TEXT("相同种子生成的颜色应一致"), FirstResult.Cells[Index].Color == SecondResult.Cells[Index].Color);
			TestEqual(TEXT("相同种子生成的数字应一致"), FirstResult.Cells[Index].Number, SecondResult.Cells[Index].Number);
		}
	}

	FCastleGroundCell StartCell;
	TestTrue(TEXT("启用道路起始格时原点应为灰色 0 号"), FirstResult.FindCell(Request.Origin, StartCell));
	if (FirstResult.FindCell(Request.Origin, StartCell))
	{
		TestTrue(TEXT("原点地面应标记为道路格"), StartCell.IsRoadTile());
	}

	FCastleGroundGenerationRequest InvalidRequest = Request;
	InvalidRequest.GridSize.X = 0;
	// 该调用专门验证错误输入路径，预先声明期望的 Error 日志，避免测试框架把预期错误当成测试失败。
	AddExpectedError(TEXT("随机地面生成失败：请求尺寸无效"), EAutomationExpectedErrorFlags::Contains, 1);
	const FCastleGroundGenerationResult InvalidResult = Generator->GenerateGround(InvalidRequest);
	TestEqual(TEXT("无效尺寸应被拒绝且不返回部分结果"), InvalidResult.Cells.Num(), 0);

	return true;
}

/** 验证棋盘只接收完整且合法的地面生成结果，不接受重复坐标。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCastleBoardGroundResultAutomationTest,
	"MovingCastle.Logic.BoardGroundResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBoardGroundResultAutomationTest::RunTest(const FString& Parameters)
{
	ACastleBoard* Board = NewObject<ACastleBoard>();
	TestNotNull(TEXT("棋盘对象应该创建成功"), Board);
	if (Board == nullptr)
	{
		return false;
	}

	Board->GridSize = FIntVector(2, 2, 3);
	Board->GridOrigin = FIntVector::ZeroValue;

	UCastleRandomGroundGenerator* Generator = NewObject<UCastleRandomGroundGenerator>();
	FCastleGroundGenerationRequest Request;
	Request.GridSize = Board->GridSize;
	Request.Origin = Board->GridOrigin;
	Request.Seed = 17;
	const FCastleGroundGenerationResult ValidResult = Generator->GenerateGround(Request);

	FText FailureReason;
	TestTrue(TEXT("完整地面结果应被棋盘接收"), Board->ApplyGroundGenerationResult(ValidResult, FailureReason));

	FCastleGroundGenerationResult DuplicateResult = ValidResult;
	DuplicateResult.Cells[1].Coordinate = DuplicateResult.Cells[0].Coordinate;
	TestFalse(TEXT("重复坐标的地面结果应被拒绝"), Board->ApplyGroundGenerationResult(DuplicateResult, FailureReason));

	return true;
}

/** 验证管理器仅维护格子 Actor 索引，格子视觉、数据和碰撞由独立 Actor 持有。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCastleBoardGroundActorAutomationTest,
	"MovingCastle.Logic.BoardGroundActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBoardGroundActorAutomationTest::RunTest(const FString& Parameters)
{
	FScopedGroundActorWorld Scope;
	if (!TestNotNull(TEXT("创建独立测试 World"), Scope.World)) { return false; }

	const FTransform BoardTransform(FRotator(0.0f, 37.0f, 0.0f), FVector(120.0f, -80.0f, 250.0f), FVector(1.25f));
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACastleBoard* Board = Scope.World->SpawnActor<ACastleBoard>(ACastleBoard::StaticClass(), BoardTransform, SpawnParameters);
	if (!TestNotNull(TEXT("在 World 中生成棋盘管理器"), Board)) { return false; }
	Board->GridSize = FIntVector(2, 2, 1);

	FCastleGroundGenerationResult Ground;
	for (int32 Y = 0; Y < 2; ++Y)
	{
		for (int32 X = 0; X < 2; ++X)
		{
			FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
			Cell.Coordinate = FIntVector(X, Y, 0);
			Cell.Color = X == 0 && Y == 0 ? ECastleTileColor::Gray : ECastleTileColor::Red;
			Cell.Number = X == 0 && Y == 0 ? 0 : 1;
		}
	}
	FText FailureReason;
	if (!TestTrue(TEXT("应用四格生成结果"), Board->ApplyGroundGenerationResult(Ground, FailureReason)))
	{
		AddError(FailureReason.ToString());
		return false;
	}

	TArray<UStaticMeshComponent*> ManagerMeshes;
	Board->GetComponents<UStaticMeshComponent>(ManagerMeshes);
	TestEqual(TEXT("管理器不再直接拥有地面 Static Mesh 组件"), ManagerMeshes.Num(), 0);

	TArray<ACastleGroundTileActor*> TileActors;
	for (TActorIterator<ACastleGroundTileActor> It(Scope.World); It; ++It)
	{
		if (It->GetOwner() == Board)
		{
			TileActors.Add(*It);
		}
	}
	if (!TestEqual(TEXT("每个格子都生成独立 Actor"), TileActors.Num(), 4)) { return false; }

	TSet<FIntVector> SeenCoordinates;
	for (ACastleGroundTileActor* Tile : TileActors)
	{
		TestTrue(TEXT("格子 Actor 有棋盘 Owner 但没有附着到管理器"), Tile->GetOwner() == Board && Tile->GetAttachParentActor() == nullptr);
		const FCastleGroundCell Cell = Tile->GetGroundCell();
		TestFalse(TEXT("每个坐标只对应一个格子 Actor"), SeenCoordinates.Contains(Cell.Coordinate));
		SeenCoordinates.Add(Cell.Coordinate);

		const FVector LocalCenter(Cell.Coordinate.X * Board->CellSize, Cell.Coordinate.Y * Board->CellSize,
			Board->WorldBaseHeight - Board->LayerHeight * 0.45f);
		const FVector ExpectedLocation = BoardTransform.TransformPosition(LocalCenter);
		TestTrue(TEXT("独立 Actor 使用棋盘完整变换定位"), Tile->GetActorLocation().Equals(ExpectedLocation, 0.01f));
		TestEqual(TEXT("格子地块只使用查询碰撞"), Tile->GetTileMeshComponent()->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
		TestEqual(TEXT("格子地块保留 Visibility 射线阻挡"),
			Tile->GetTileMeshComponent()->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
		TestTrue(TEXT("实际格子地面参与导航"), Tile->GetTileMeshComponent()->CanEverAffectNavigation());
		TestEqual(TEXT("实际格子地面阻挡 Pawn 导航碰撞"),
			Tile->GetTileMeshComponent()->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
		TestEqual(TEXT("数字底牌没有碰撞"), Tile->GetNumberPlateComponent()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("数字底牌不参与导航"), Tile->GetNumberPlateComponent()->CanEverAffectNavigation());
	}
	return true;
}

/** 验证背包容量、建筑类等级读取和失败恢复所依赖的插入顺序。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCastleBackpackAutomationTest,
	"MovingCastle.Logic.Backpack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBackpackAutomationTest::RunTest(const FString& Parameters)
{
	UCastleBackpackComponent* Backpack = NewObject<UCastleBackpackComponent>();
	TestNotNull(TEXT("背包组件应该创建成功"), Backpack);
	if (Backpack == nullptr)
	{
		return false;
	}

	FText FailureReason;
	int32 AddedIndex = INDEX_NONE;
	TestTrue(TEXT("合法箭塔卡牌应能加入背包"), Backpack->TryAddBuilding(CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower), 1, ECastleTileColor::Red, 3, AddedIndex, FailureReason));
	TestEqual(TEXT("加入第一张卡牌后数量应为 1"), Backpack->GetItemCount(), 1);

	TestFalse(TEXT("道路不能使用非灰色 0 号属性"), Backpack->TryAddBuilding(CastleBuildingTests::MakeDefinition(ECastleBuildingType::Road), 1, ECastleTileColor::Red, 3, AddedIndex, FailureReason));

	for (int32 Index = Backpack->GetItemCount(); Index < Backpack->Capacity; ++Index)
	{
		TestTrue(TEXT("容量未满时应能继续加入合法卡牌"), Backpack->TryAddBuilding(CastleBuildingTests::MakeDefinition(ECastleBuildingType::Farm), 1, ECastleTileColor::Blue, 2, AddedIndex, FailureReason));
	}
	TestEqual(TEXT("背包应达到策划案规定的 10 格上限"), Backpack->GetItemCount(), 10);
	TestFalse(TEXT("背包满后不能继续加入卡牌"), Backpack->TryAddBuilding(CastleBuildingTests::MakeDefinition(ECastleBuildingType::CannonTower), 1, ECastleTileColor::Green, 4, AddedIndex, FailureReason));

	FCastleBuildingDescriptor RemovedDescriptor;
	TestTrue(TEXT("应能取出第一个槽位"), Backpack->TryRemoveItemAt(0, RemovedDescriptor, FailureReason));
	TestTrue(TEXT("取出后应能按原槽位插回卡牌"), Backpack->TryInsertItemAt(0, RemovedDescriptor, FailureReason));
	TestEqual(TEXT("插回后背包数量应恢复为 10"), Backpack->GetItemCount(), 10);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
