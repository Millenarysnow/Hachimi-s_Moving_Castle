// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "CastlePlayerController.h"
#include "CastleBoard.h"
#include "CastleBackpackComponent.h"
#include "CastleGroundTileActor.h"
#include "CastleBuildingTestUtilities.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "CastleShopComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceNull.h"
#include "Tests/AutomationCommon.h"


namespace
{
 /** 在编辑器完成地图加载并启动PIE后检查正式开局，不保存地图。 */
 class FCheckDirectorMainMap final : public IAutomationLatentCommand
 {
 public:
  explicit FCheckDirectorMainMap(FAutomationTestBase* InTest) : Test(InTest) {}
  virtual bool Update() override
  {
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE && Context.World()) { World = Context.World(); break; }
	}
	if (!Test->TestNotNull(TEXT("独立游戏世界存在"), World)) { return true; }
	Test->TestTrue(TEXT("当前确实加载MainMap"), UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()).EndsWith(TEXT("/MainMap")));
	UCastleDirectorSubsystem* Director = World->GetSubsystem<UCastleDirectorSubsystem>();
	if (!Test->TestTrue(TEXT("真实MainMap导演已自动启动"), Director && Director->IsRunning())) { return true; }
	Test->TestEqual(TEXT("真实开局为编辑阶段"), Director->GetPhase(), ECastleDirectorPhase::Editing);
	Test->TestEqual(TEXT("开局等级为1"), Director->GetLevel(), 1);
	Test->TestEqual(TEXT("开局经验为0"), Director->GetExperience(), 0);
	int32 NodeCount = 0;
	for (TActorIterator<ACastleDirectorNodeActor> It(World); It; ++It) { ++NodeCount; }
	Test->TestTrue(TEXT("真实世界中生成2至3个节点"), NodeCount >= 2 && NodeCount <= 3);
	FText Reason;
	Test->TestFalse(TEXT("开局没有商店权限"), Director->IsShopAccessAllowed(Reason));
	ACastlePlayerController* Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
	if (!Test->TestNotNull(TEXT("本地控制器类型正确"), Controller)) { return true; }
	Controller->ToggleShop();
	Test->TestFalse(TEXT("真实B键入口拒绝开店"), Controller->GetShop()->IsShopOpen());
	Test->TestTrue(TEXT("真实HUD房间计数是10/15"), Director->GetHUDSummary().Contains(TEXT("房间 10/15")));
	FOutputDeviceNull Output;
	Test->TestTrue(TEXT("真实伤害命令可执行"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleDamageMainTower 99999"), Output, World));
	Test->TestEqual(TEXT("主塔死亡触发正式游戏结束"), Director->GetPhase(), ECastleDirectorPhase::GameOver);
	Test->TestTrue(TEXT("世界已暂停"), World->IsPaused());
	Test->TestTrue(TEXT("游戏结束鼠标可见"), Controller->bShowMouseCursor);
	Test->TestTrue(TEXT("游戏结束已锁定移动"), Controller->IsMoveInputIgnored());
	Test->TestTrue(TEXT("游戏结束已锁定视角"), Controller->IsLookInputIgnored());
	Test->AddInfo(TEXT("MainMap启动、节点、HUD、商店门控、正式主塔死亡与暂停路径已验证；字体中文显示另行验收。"));
	return true;
  }
 private:
  FAutomationTestBase* Test;
 };
 /** 在实际塔模型和正式地图上验证四侧每一格；不是通过构造光标结构绕过射线。 */
 class FCheckMainTowerSidePlacement final : public IAutomationLatentCommand
 {
 public:
  explicit FCheckMainTowerSidePlacement(FAutomationTestBase* InTest) : Test(InTest) {}
  bool Update() override
  {
   UWorld* World = AutomationCommon::GetAnyGameWorld();
   if (!World || !World->HasBegunPlay())
   {
    if (FPlatformTime::Seconds() <= Deadline) { return false; }
    Test->AddError(TEXT("等待 MainMap PIE 塔侧放置启动超时。"));
    return true;
   }
   TActorIterator<ACastleBoard> It(World);
   if (!Test->TestTrue(TEXT("正式地图存在棋盘管理器"), static_cast<bool>(It))) { return true; }
   ACastleBoard* Board = *It;
   FText Reason;
   if (!Test->TestTrue(TEXT("MainMap 显式启用塔侧模式"),
       Board->PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides && Board->ValidatePlacementSurface(Reason)))
   {
    Test->AddError(Reason.ToString());
    return true;
   }
   FCastleGroundCell Ground;
   Test->TestFalse(TEXT("没有生成地面数据"), Board->TryGetGroundCell(Board->GridOrigin, Ground));
   Test->TestFalse(TEXT("没有生成任何地面格 Actor"), static_cast<bool>(TActorIterator<ACastleGroundTileActor>(World)));
   Test->TestEqual(TEXT("没有把主塔或侧格计作已放房间"), Board->GetPlacedBuildingCount(), 0);

   // 从外向内投射每层四面的中心；同一塔身坐标的四个方向必须得到不同落点。
   const FIntVector Base = Board->GetBoardCenterCoordinate();
   const FIntVector Offsets[] = {FIntVector(-1, 0, 0), FIntVector(0, -1, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0)};
   const ECastleGridFace Faces[] = {ECastleGridFace::NegativeX, ECastleGridFace::NegativeY, ECastleGridFace::PositiveX, ECastleGridFace::PositiveY};
   auto Resolve = [Board](const FIntVector& Cell, const FIntVector& Offset, FCastleGridCursorTarget& OutTarget)
   {
    const FVector Center = Board->GetGridWorldLocation(Cell);
    const FVector Displacement = Board->GetActorTransform().TransformVector(FVector(Offset) * Board->CellSize * 2.0);
    return Board->TryResolveGridCursorTarget(Center + Displacement, Center, OutTarget);
   };
   FCastleBuildingDescriptor Probe;
   Probe.Definition = Board->FindBuildingDefinition(ECastleBuildingType::NormalRoom);
   Probe.Level = 1;
   for (int32 Layer = 0; Layer < Board->GridSize.Z; ++Layer)
   {
    const FIntVector Cell = Base + FIntVector(0, 0, Layer);
    for (int32 Side = 0; Side < UE_ARRAY_COUNT(Faces); ++Side)
    {
     FCastleGridCursorTarget Target;
     if (!Test->TestTrue(FString::Printf(TEXT("层 %d 面 %d 命中真实侧格且落在正确相邻格"), Layer, Side),
         Resolve(Cell, Offsets[Side], Target) && Target.Surface == ECastleGridCursorSurface::MainTowerSide
         && Target.Face == Faces[Side] && Target.Cell == Cell && Target.AdjacentCell == Cell + Offsets[Side])) { return true; }
     const FCastleTowerSideCell* Data = Board->FindTowerSideCell(Cell, Target.Face);
     if (!Test->TestNotNull(TEXT("按塔身坐标及方向取得格子数据"), Data)) { return true; }
     Test->TestTrue(TEXT("合法五色色号与 1～9 数字"), Data->Color >= ECastleTileColor::Red
         && Data->Color <= ECastleTileColor::Purple && Data->Number >= 1 && Data->Number <= 9);
     // 高层同样能直接起建，不要求在塔脚先造一根支撑柱。
     Probe.Color = Data->Color;
     Probe.Number = Data->Number;
     Test->TestTrue(TEXT("实际命中的每个侧格都可摆放匹配房间"), Board->CanPlaceBuildingAtCursor(Target, Probe, Reason));
     Test->TestTrue(TEXT("面板及数字可见，不占实体碰撞或导航"), Data->Panel->IsVisible() && Data->Label->IsVisible()
         && Data->Panel->GetCollisionEnabled() == ECollisionEnabled::QueryOnly && !Data->Panel->CanEverAffectNavigation()
         && !Data->Label->CanEverAffectNavigation() && Data->Label->Text.ToString() == FString::FromInt(Data->Number));
    }
   }

   FCastleGridCursorTarget BottomTarget, Other;
   if (!Test->TestTrue(TEXT("左键样例牌入口可解析首层 -X 格"), Resolve(Base, Offsets[0], BottomTarget))) { return true; }
   const FVector TowerBottom = Board->MainTowerActor->GetActorLocation();
   Test->TestFalse(TEXT("塔顶不提供放置格"), Board->TryResolveGridCursorTarget(
       TowerBottom + FVector(0, 0, 7000), TowerBottom + FVector(0, 0, 5000), Other));
   FCastleBuildingDescriptor Starter;
   if (!Test->TestTrue(TEXT("起始背包槽位 1 有可放卡"), Board->PrototypeBackpack->TryGetItemAt(0, Starter))) { return true; }
   Test->TestTrue(TEXT("实际开局卡匹配首层 -X"), Board->CanPlaceBuildingAtCursor(BottomTarget, Starter, Reason));
   const FCastleTowerSideCell* BottomCell = Board->FindTowerSideCell(Base, Faces[0]);
   FCastleBuildingDescriptor OffColor = Starter;
   OffColor.Color = BottomCell->Color == ECastleTileColor::Red ? ECastleTileColor::Blue : ECastleTileColor::Red;
   Test->TestTrue(TEXT("不同色同数字可放"), Board->CanPlaceBuildingAtCursor(BottomTarget, OffColor, Reason));
   OffColor.Number = BottomCell->Number % 9 + 1;
   Test->TestFalse(TEXT("颜色和数字都不同拒绝"), Board->CanPlaceBuildingAtCursor(BottomTarget, OffColor, Reason));
   OffColor.Color = BottomCell->Color;
   Test->TestTrue(TEXT("同色不同数字可放"), Board->CanPlaceBuildingAtCursor(BottomTarget, OffColor, Reason));
   Test->TestFalse(TEXT("无来源的坐标入口不能从空中或隐藏地面开始"), Board->CanPlaceBuilding(BottomTarget.AdjacentCell, Starter, Reason));
   FCastleBuildingDescriptor IntoTower = Starter;
   IntoTower.Definition = Board->FindBuildingDefinition(ECastleBuildingType::Farm);
   IntoTower.Level = 2;
   Test->TestTrue(TEXT("多格牌不可侵入塔身"), !Board->CanPlaceBuildingAtCursor(BottomTarget, IntoTower, Reason)
       && Reason.ToString().Contains(TEXT("主塔塔身")));

   // 显隐前后沿用同一面板和色号，隐藏期间旧光标也不能提交到该侧格。
   if (!Test->TestTrue(TEXT("隐藏当前建造面"), Board->SetPlacementSurfacePresentationHidden(true, Reason))) { return true; }
   Test->TestFalse(TEXT("隐藏后侧面不能命中"), Resolve(Base, Offsets[0], Other));
   Test->TestFalse(TEXT("隐藏前缓存的目标不能放置"), Board->CanPlaceBuildingAtCursor(BottomTarget, Starter, Reason));
   Test->TestFalse(TEXT("主塔本体仍可见"), Board->MainTowerActor->IsHidden());
   if (!Test->TestTrue(TEXT("恢复建造面"), Board->SetPlacementSurfacePresentationHidden(false, Reason))) { return true; }
   Test->TestTrue(TEXT("原始侧格实例与属性保留"), Resolve(Base, Offsets[0], Other)
       && Board->FindTowerSideCell(Base, Faces[0]) == BottomCell && Board->CanPlaceBuildingAtCursor(Other, Starter, Reason));

   const int32 Before = Board->PrototypeBackpack->GetItemCount();
   ACastleBuildingBase* Room = Board->PlaceBackpackBuildingAtCursor(BottomTarget, 0, Reason);
   if (!Test->TestTrue(TEXT("沿正式背包入口放置并完成飞入"), CastleBuildingTests::CompletePlacement(Room)))
   {
    Test->AddError(Reason.ToString());
    return true;
   }
   Test->TestEqual(TEXT("只消费一张牌"), Board->PrototypeBackpack->GetItemCount(), Before - 1);
   Test->TestTrue(TEXT("房间保持直立且占用所选面外侧格"), Room->GetActorUpVector().Equals(FVector::UpVector)
       && Room->GetOccupiedCoordinates() == TArray<FIntVector>({BottomTarget.AdjacentCell}));
   Test->TestFalse(TEXT("同一落点占用后，旧目标不能重复放"), Board->CanPlaceBuildingAtCursor(BottomTarget, Starter, Reason));
   Test->TestEqual(TEXT("塔身未登记为建筑，只有一个房间"), Board->GetPlacedBuildingCount(), 1);
   return true;
  }
 private:
  FAutomationTestBase* Test;
  double Deadline = FPlatformTime::Seconds() + 20.0;
 };

 /** 只停止本测试启动的PIE，等待世界销毁后才把控制权交回自动化队列。 */
 class FStopDirectorMainMap final : public IAutomationLatentCommand
 {
 public:
  explicit FStopDirectorMainMap(FAutomationTestBase* InTest) : Test(InTest) {}
  virtual bool Update() override
  {
   if (!AutomationCommon::GetAnyGameWorld()) { return true; }
   if (!bRequested)
   {
    UClass* ToolClass = FindObject<UClass>(nullptr, TEXT("/Script/EditorToolset.EditorAppToolset"));
    UFunction* Stop = ToolClass ? ToolClass->FindFunctionByName(TEXT("StopPIE")) : nullptr;
    if (!Stop) { Test->AddError(TEXT("官方StopPIE工具不可用，无法结束本测试PIE。")); return true; }
    struct FStopParams { UObject* ReturnValue = nullptr; } Params;
    ToolClass->GetDefaultObject()->ProcessEvent(Stop, &Params);
    bRequested = true;
    StartedAt = FPlatformTime::Seconds();
   }
   if (FPlatformTime::Seconds() - StartedAt > 15.0) { Test->AddError(TEXT("PIE结束等待超时。")); return true; }
   return false;
  }
 private:
  FAutomationTestBase* Test;
  bool bRequested = false;
  double StartedAt = 0.0;
 };
}

/** 真实MainMap通过编辑器加载和PIE验证，避免未烘焙-game进程缺失外部Actor的差异。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorMainMapTest, "MovingCastle.Director.MainMapSmoke",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleDirectorMainMapTest::RunTest(const FString& Parameters)
{
 if (!AutomationOpenMap(TEXT("/Game/MovingCastle/Maps/MainMap"), true)) { return false; }
 ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.1f));
 ADD_LATENT_AUTOMATION_COMMAND(FCheckDirectorMainMap(this));
 ADD_LATENT_AUTOMATION_COMMAND(FStopDirectorMainMap(this));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleTowerSideMainMapTest, "MovingCastle.Placement.MainTowerSides",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleTowerSideMainMapTest::RunTest(const FString& Parameters)
{
 if (!AutomationOpenMap(TEXT("/Game/MovingCastle/Maps/MainMap"), true)) { return false; }
 ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.1f));
 ADD_LATENT_AUTOMATION_COMMAND(FCheckMainTowerSidePlacement(this));
 ADD_LATENT_AUTOMATION_COMMAND(FStopDirectorMainMap(this));
 return true;
}

#endif
