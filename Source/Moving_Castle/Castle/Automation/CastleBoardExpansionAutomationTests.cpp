// 棋盘坐标生成契约：尺寸、原点和遍历顺序不能改变同一 XY 格的内容。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CastleGroundGenerator.h"
#include "CastleBoard.h"
#include "Castle/PCG/CastlePCGSemanticTestRules.h"
#include "CastleBuildingTestUtilities.h"
#include "CastleBackpackComponent.h"
#include "Castle/PCG/CastleBuildingPCGRule.h"
#include "CastleGroundTileActor.h"
#include "CastleBoardExpansionTestGenerator.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Moving_Castle.h"

namespace CastleBoardValidation
{
 /** 验收专用原生命令：完整 RebuildAll 后同步等待，不把异步派发耗时冒充构建耗时。 */
 FAutoConsoleCommandWithWorldAndArgs RebuildTiming(TEXT("CastleBoardTestRebuildNavigation"), TEXT("自动化验收专用：测量完整导航重建。"),
 FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
 {
  UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
  ANavigationData* Data = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
  if (!Nav || !Data)
  {
   UE_LOG(LogMoving_Castle, Error, TEXT("W2_NAV_REBUILD_FAILED：世界没有导航系统或已有导航数据。"));
   return;
  }
  const double Start=FPlatformTime::Seconds();
  // 该地图要求已有 Recast；直接重建已有导航数据，不触发缺失数据的自动创建。
  Data->RebuildAll();
  Data->EnsureBuildCompletion();
  UE_LOG(LogMoving_Castle, Display, TEXT("W2_NAV_REBUILD_SECONDS=%.6f"), FPlatformTime::Seconds()-Start);
 }));
}


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
 // 固定样本锁定稳定哈希契约；每次独立进程执行均须得到相同的紫色 4 号格。
 FCastleGroundCell Golden;
 if (!TestTrue(TEXT("固定样本存在"), Original.FindCell(Small.Origin, Golden))) { return false; }
 TestTrue(TEXT("跨进程固定颜色"), Golden.Color==ECastleTileColor::Purple);
 TestEqual(TEXT("跨进程固定数字"), Golden.Number, 4);
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

/** 从真实建筑射线继续向外建造，并验证混合占格、高度和扩张覆盖。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBoardOverhangTest, "MovingCastle.Board.OverhangPlacement",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleBoardOverhangTest::RunTest(const FString& Parameters)
{
 const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
 if (!TestNotNull(TEXT("越界测试世界"), World)) { return false; }
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
 CastleBuildingTests::FScopedNativeBuildingModels Models(World);
 ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
 Board->GridSize = FIntVector(5,5,8);
 CastleBuildingTests::ConfigureBoard(Board);
 if (!CastlePCGTests::ConfigurePlacementOnlyPCG(*Board, *this)) { return false; }
 FText Reason;
 if (!TestTrue(TEXT("生成地面"), Board->GenerateGround(Reason))) { return false; }
 FCastleGroundCell Ground;
 Board->TryGetGroundCell(FIntVector(4,2,0), Ground);
 FCastleBuildingDescriptor Card;
 if (!Board->BuildingDefinitions[0]->CreateDescriptor(1, Ground.Color, Ground.Number, Card, Reason)) { return false; }
 ACastleBuildingBase* Source = Board->PlaceBuilding(FIntVector(4,2,0), Card, false, Reason);
 if (!TestNotNull(TEXT("放置边缘来源建筑"), Source) || !CastleBuildingTests::CompletePlacement(Source)) { return false; }
 const auto ResolveOutward = [Board](ACastleBuildingBase* Building, FCastleGridCursorTarget& Target)
 {
  const FVector Center = Building->GetVisualBounds().GetCenter();
  return Board->TryResolveGridCursorTarget(Center+FVector(Board->CellSize,0,0), Center, Target);
 };
 ACastleBuildingBase* Outside[2] = {};
 for (int32 Step=0; Step<2; ++Step)
 {
  FCastleGridCursorTarget Target;
  if (!TestTrue(TEXT("真实射线接受边缘或越界来源"), ResolveOutward(Source, Target))) { return false; }
  TestTrue(TEXT("允许外围第 1/2 格"), Board->CanPlaceBuildingAtCursor(Target, Card, Reason));
  // 使用正式背包入口，证明最终生成和扣牌仍保留建筑面来源。
  int32 Slot;
  if (!Board->PrototypeBackpack->TryAddItem(Card, Slot, Reason)) { return false; }
  Source = Board->PlaceBackpackBuildingAtCursor(Target, Slot, Reason);
  if (!TestNotNull(TEXT("实际生成越界建筑"), Source) || !CastleBuildingTests::CompletePlacement(Source)) { return false; }
  Outside[Step] = Source;
 }
 FCastleGridCursorTarget Third;
 TestTrue(TEXT("第三格仍能解析高亮候选"), ResolveOutward(Source, Third));
 TestFalse(TEXT("第三格拒绝"), Board->CanPlaceBuildingAtCursor(Third, Card, Reason));
 TestTrue(TEXT("第三格给出距离原因"), Reason.ToString().Contains(TEXT("最多伸出棋盘边界 2 格")));
 TestFalse(TEXT("外围首层坐标入口没有地面不能放"), Board->CanPlaceBuilding(FIntVector(-1,2,0), Card, Reason));
 TestFalse(TEXT("无下方匹配来源的坐标入口仍拒绝"), Board->CanPlaceBuilding(FIntVector(3,3,1), Card, Reason));
 TestTrue(TEXT("坐标入口明确报告来源建筑不存在"), Reason.ToString().Contains(TEXT("指向的建筑已不存在")));
 FCastleGridCursorTarget Top;
 Top.bValid=true; Top.Cell=FIntVector(6,2,0); Top.Face=ECastleGridFace::Up; Top.AdjacentCell=FIntVector(6,2,1);
 TestTrue(TEXT("外围建筑顶面允许上叠"), Board->CanPlaceBuildingAtCursor(Top, Card, Reason));
 FCastleBuildingDescriptor Multi;
 if (!Board->BuildingDefinitions[1]->CreateDescriptor(4, Card.Color, Card.Number, Multi, Reason)) { return false; }
 Top.Cell=FIntVector(4,2,0); Top.AdjacentCell=FIntVector(4,2,1);
 TestTrue(TEXT("跨界四级卡的内部格也允许下方无支撑"), Board->CanPlaceBuildingAtCursor(Top, Multi, Reason));
 // 多格卡横跨边界仍须使用建筑面来源；是否悬空不改变地面来源的边界约束。
 FCastleBuildingDescriptor Level2;
 if (!Board->BuildingDefinitions[1]->CreateDescriptor(2, Card.Color, Card.Number, Level2, Reason)) { return false; }
 TestFalse(TEXT("首层跨界多格卡不能从地面坐标入口伸出"), Board->CanPlaceBuilding(FIntVector(4,4,0), Level2, Reason));
 FCastleGridCursorTarget Cross;
 Cross.bValid=true; Cross.Cell=FIntVector(4,2,0); Cross.Face=ECastleGridFace::PositiveY; Cross.AdjacentCell=FIntVector(4,3,0);
 TestTrue(TEXT("建筑面来源的首层跨界多格卡合法"), Board->CanPlaceBuildingAtCursor(Cross, Level2, Reason));
 ACastleBuildingBase* Upper = Outside[1];
 for (int32 Z=1; Z<8; ++Z)
 {
  FCastleGridCursorTarget Up;
  Up.bValid=true; Up.Cell=FIntVector(6,2,Z-1); Up.Face=ECastleGridFace::Up; Up.AdjacentCell=FIntVector(6,2,Z);
  int32 Slot;
  if (!Board->PrototypeBackpack->TryAddItem(Card, Slot, Reason)) { return false; }
  Upper=Board->PlaceBackpackBuildingAtCursor(Up,Slot,Reason);
  if (!TestNotNull(TEXT("外围逐层叠到第八层"), Upper) || !CastleBuildingTests::CompletePlacement(Upper)) { return false; }
 }
 FCastleGridCursorTarget TooHigh;
 TooHigh.bValid=true; TooHigh.Cell=FIntVector(6,2,7); TooHigh.Face=ECastleGridFace::Up; TooHigh.AdjacentCell=FIntVector(6,2,8);
 TestFalse(TEXT("第九层拒绝"), Board->CanPlaceBuildingAtCursor(TooHigh,Card,Reason));
 const FVector OutsideLocation = Outside[0]->GetActorLocation();
 TestTrue(TEXT("扩张覆盖越界建筑列"), Board->ExpandBoard(Reason));
 TestTrue(TEXT("越界建筑下面新增地块"), Board->TryGetGroundCell(FIntVector(5,2,0), Ground));
 TestTrue(TEXT("扩张不移动建筑"), OutsideLocation.Equals(Outside[0]->GetActorLocation()));
 return true;
}

/** 实际射线验证隐藏，同时校验隐藏扩张的新地块与显示恢复。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBoardPresentationTest, "MovingCastle.Board.GroundPresentation",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleBoardPresentationTest::RunTest(const FString& Parameters)
{
 const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
 if (!TestNotNull(TEXT("显隐测试世界"), World)) { return false; }
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
 ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
 Board->GridSize = FIntVector(5,5,8);
 FText Reason;
 if (!Board->GenerateGround(Reason)) { AddError(Reason.ToString()); return false; }
 FCastleGridCursorTarget Target;
 const FVector Center = Board->GetGridWorldLocation(FIntVector(0,0,0));
 TestTrue(TEXT("显示状态真实射线命中"), Board->TryResolveGridCursorTarget(Center+FVector(0,0,600),Center-FVector(0,0,600),Target));
 Board->SetGroundPresentationHidden(true);
 Board->SetGroundPresentationHidden(true);
 TestTrue(TEXT("隐藏状态可查询"), Board->IsGroundPresentationHidden());
 TestFalse(TEXT("隐藏后真实射线不命中"), Board->TryResolveGridCursorTarget(Center+FVector(0,0,600),Center-FVector(0,0,600),Target));
 TestTrue(TEXT("隐藏时允许扩张"), Board->ExpandBoard(Reason));
 int32 Count=0;
 for (TActorIterator<ACastleGroundTileActor> It(World); It; ++It)
 {
  ++Count;
  TestFalse(TEXT("新旧格面都隐藏"), It->GetTileMeshComponent()->IsVisible());
  TestFalse(TEXT("新旧数字牌都隐藏"), It->GetNumberPlateComponent()->IsVisible());
  TestFalse(TEXT("新旧文字都隐藏"), It->GetNumberLabelComponent()->IsVisible());
  TestTrue(TEXT("Visibility 响应忽略"), It->GetTileMeshComponent()->GetCollisionResponseToChannel(ECC_Visibility)==ECR_Ignore);
  TestFalse(TEXT("导航资格不变"), It->GetTileMeshComponent()->CanEverAffectNavigation());
 }
 TestEqual(TEXT("隐藏不销毁任何地块"), Count, 49);
 Board->SetGroundPresentationHidden(false);
 for (TActorIterator<ACastleGroundTileActor> It(World); It; ++It)
 {
  TestTrue(TEXT("新旧格面恢复"), It->GetTileMeshComponent()->IsVisible());
  TestTrue(TEXT("新旧数字牌恢复"), It->GetNumberPlateComponent()->IsVisible());
  TestTrue(TEXT("新旧文字恢复"), It->GetNumberLabelComponent()->IsVisible());
 }
 TestTrue(TEXT("显示后真实射线恢复"), Board->TryResolveGridCursorTarget(Center+FVector(0,0,600),Center-FVector(0,0,600),Target));
 return true;
}
#endif
