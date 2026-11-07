// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildings.h"
#include "CastleGroundGenerator.h"

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
	TestTrue(TEXT("合法箭塔卡牌应能加入背包"), Backpack->TryAddBuilding(ACastleArcherTowerLevel1::StaticClass(), ECastleTileColor::Red, 3, AddedIndex, FailureReason));
	TestEqual(TEXT("加入第一张卡牌后数量应为 1"), Backpack->GetItemCount(), 1);

	TestFalse(TEXT("道路不能使用非灰色 0 号属性"), Backpack->TryAddBuilding(ACastleRoadBuilding::StaticClass(), ECastleTileColor::Red, 3, AddedIndex, FailureReason));

	for (int32 Index = Backpack->GetItemCount(); Index < Backpack->Capacity; ++Index)
	{
		TestTrue(TEXT("容量未满时应能继续加入合法卡牌"), Backpack->TryAddBuilding(ACastleFarmLevel1::StaticClass(), ECastleTileColor::Blue, 2, AddedIndex, FailureReason));
	}
	TestEqual(TEXT("背包应达到策划案规定的 10 格上限"), Backpack->GetItemCount(), 10);
	TestFalse(TEXT("背包满后不能继续加入卡牌"), Backpack->TryAddBuilding(ACastleCannonTowerLevel1::StaticClass(), ECastleTileColor::Green, 4, AddedIndex, FailureReason));

	FCastleBuildingDescriptor RemovedDescriptor;
	TestTrue(TEXT("应能取出第一个槽位"), Backpack->TryRemoveItemAt(0, RemovedDescriptor, FailureReason));
	TestTrue(TEXT("取出后应能按原槽位插回卡牌"), Backpack->TryInsertItemAt(0, RemovedDescriptor, FailureReason));
	TestEqual(TEXT("插回后背包数量应恢复为 10"), Backpack->GetItemCount(), 10);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
