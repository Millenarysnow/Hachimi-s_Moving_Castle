// 棋盘坐标生成契约：尺寸、原点和遍历顺序不能改变同一 XY 格的内容。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CastleGroundGenerator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleGroundCoordinateStableTest, "MovingCastle.Board.CoordinateStableGround",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleGroundCoordinateStableTest::RunTest(const FString& Parameters)
{
 UCastleRandomGroundGenerator* Generator = NewObject<UCastleRandomGroundGenerator>();
 FCastleGroundGenerationRequest Small;
 Small.GridSize = FIntVector(5,5,8);
 Small.Origin = FIntVector(-4,7,2);
 Small.Seed = -718;
 const auto Original = Generator->GenerateGround(Small);
 FCastleGroundGenerationRequest Large = Small;
 Large.GridSize = FIntVector(7,7,8);
 Large.Origin -= FIntVector(1,1,0);
 const auto Expanded = Generator->GenerateGround(Large);
 TestEqual(TEXT("起始生成 25 格"), Original.Cells.Num(), 25);
 TestEqual(TEXT("扩大生成 49 格"), Expanded.Cells.Num(), 49);
 for (const auto& Cell : Original.Cells)
 {
  FCastleGroundCell Found;
  if (!TestTrue(TEXT("扩大后保留坐标"), Expanded.FindCell(Cell.Coordinate, Found))) { return false; }
  TestTrue(TEXT("扩大后颜色不变"), Found.Color == Cell.Color);
  TestEqual(TEXT("扩大后数字不变"), Found.Number, Cell.Number);
  // 单格请求改变尺寸和 Z；随机内容的契约仅包含种子与绝对 XY 坐标。
  FCastleGroundGenerationRequest Single = Small;
  Single.GridSize = FIntVector(1,1,1);
  Single.Origin = Cell.Coordinate + FIntVector(0,0,3);
  const auto Independent = Generator->GenerateGround(Single);
  if (!TestEqual(TEXT("独立单格生成完整"), Independent.Cells.Num(), 1)) { return false; }
  TestTrue(TEXT("单格颜色一致"), Independent.Cells[0].Color == Cell.Color);
  TestEqual(TEXT("单格数字一致"), Independent.Cells[0].Number, Cell.Number);
 }
 return true;
}
#endif
