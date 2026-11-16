// 棋盘坐标生成契约：尺寸、原点和遍历顺序不能改变同一 XY 格的内容。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CastleGroundGenerator.h"
#include "CastleBoard.h"
#include "CastleGroundTileActor.h"
#include "CastleBoardExpansionTestGenerator.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"

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

/** 真实 World 下验证原对象身份、固定坐标、事件次数和扩张失败原子性。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBoardExpansionTest, "MovingCastle.Board.ExpansionTransaction",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleBoardExpansionTest::RunTest(const FString& Parameters)
{
 const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
 if (!TestNotNull(TEXT("测试世界"), World)) { return false; }
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
 ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
 Board->GridSize = FIntVector(5,5,8);
 Board->GridOrigin = FIntVector(-3,7,2);
 Board->SetActorTransform(FTransform(FRotator(0,37,0), FVector(100,-200,80), FVector(1.2)));
 FText Reason;
 if (!TestTrue(TEXT("生成初始地面"), Board->GenerateGround(Reason))) { AddError(Reason.ToString()); return false; }
 TMap<FIntVector, ACastleGroundTileActor*> OldTiles;
 for (TActorIterator<ACastleGroundTileActor> It(World); It; ++It) { OldTiles.Add(It->GetGroundCell().Coordinate, *It); }
 const FVector Before = Board->GetGridWorldLocation(Board->GridOrigin);
 const FIntVector Center = Board->GetBoardCenterCoordinate();
 int32 Events = 0;
 Board->OnBoardExpanded.AddLambda([this, &Events](ACastleBoard* Expanded)
 {
  ++Events;
  FIntPoint Min,Max;
  Expanded->GetBoardExtent(Min,Max);
  TestEqual(TEXT("事件中范围已提交"), Max.X-Min.X+1, 5+Events*2);
 });
 TestTrue(TEXT("扩张一圈"), Board->ExpandBoard(Reason));
 TestEqual(TEXT("扩张只广播一次"), Events, 1);
 TestTrue(TEXT("世界映射固定"), Before.Equals(Board->GetGridWorldLocation(Board->GridOrigin)));
 TestTrue(TEXT("中心格固定"), Center == Board->GetBoardCenterCoordinate());
 int32 TileCount = 0;
 for (TActorIterator<ACastleGroundTileActor> It(World); It; ++It)
 {
  ++TileCount;
  if (ACastleGroundTileActor** Old = OldTiles.Find(It->GetGroundCell().Coordinate))
  { TestTrue(TEXT("原 Actor 身份保留"), *Old == *It); }
 }
 TestEqual(TEXT("只有 49 个地块"), TileCount, 49);
 for (int32 Index=0; Index<4; ++Index) { TestTrue(TEXT("逐圈扩张到上限"), Board->ExpandBoard(Reason)); }
 FIntPoint Min,Max;
 Board->GetBoardExtent(Min,Max);
 TestEqual(TEXT("上限尺寸"), Max.X-Min.X+1, 15);
 TestFalse(TEXT("超过上限被拒"), Board->ExpandBoard(Reason));
 FIntPoint AfterMin,AfterMax;
 Board->GetBoardExtent(AfterMin,AfterMax);
 TestTrue(TEXT("上限拒绝不改变范围"), Min==AfterMin && Max==AfterMax);
 TestEqual(TEXT("失败不广播"), Events, 5);
 return true;
}

/** 故意依赖尺寸的生成策略必须被拒绝；非法配置不允许自动修正。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBoardExpansionRejectionTest, "MovingCastle.Board.ExpansionRejections",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleBoardExpansionRejectionTest::RunTest(const FString& Parameters)
{
 ACastleBoard* Board = NewObject<ACastleBoard>();
 Board->GridSize = FIntVector(5,5,8);
 Board->GroundGeneratorClass = UCastleSizeDependentTestGroundGenerator::StaticClass();
 FText Reason;
 TestTrue(TEXT("测试策略初始结果合法"), Board->GenerateGround(Reason));
 AddExpectedError(TEXT("改变已有地块"), EAutomationExpectedErrorFlags::Contains, 1);
 TestFalse(TEXT("尺寸相关策略拒绝扩张"), Board->ExpandBoard(Reason));
 FIntPoint Min,Max;
 Board->GetBoardExtent(Min,Max);
 TestTrue(TEXT("拒绝后仍是初始范围"), Min==FIntPoint(0,0) && Max==FIntPoint(4,4));
 const TArray<FIntPoint> Invalid = { FIntPoint(4,2), FIntPoint(65,2), FIntPoint(15,-1), FIntPoint(15,9) };
 for (const FIntPoint Config : Invalid)
 {
  Board->MaxBoardSize = Config.X;
  Board->MaxOverhangCells = Config.Y;
  AddExpectedError(TEXT("非法棋盘配置"), EAutomationExpectedErrorFlags::Contains, 1);
  TestFalse(TEXT("非法配置拒绝生成"), Board->GenerateGround(Reason));
 }
 return true;
}
#endif
