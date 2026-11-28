// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Castle/Automation/CastlePIEAutomation.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/SkyIslands/Rest/CastleRestSkyIsland.h"
#include "Castle/UI/BuildBar/CastleBuildBarWidget.h"
#include "Castle/UI/Common/CastleHUDControls.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Castle/UI/Economy/CastleEconomyHUDWidget.h"
#include "CastlePlayerController.h"
#include "CastleBoard.h"
#include "CastleBackpackComponent.h"
#include "CastleGroundTileActor.h"
#include "CastleEconomyComponent.h"
#include "CastleMainTowerComponent.h"
#include "CastleBuildingTestUtilities.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetSwitcher.h"
#include "CastleShopComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/OutputDeviceNull.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Tests/AutomationCommon.h"
#include "NiagaraComponent.h"
#include "Units/Navigation/Interaction/CastlePatrolRoute.h"


namespace
{
 /** 只有视口内真实挂载的实例才算通过，不从资产临时构造替代控件。 */
 template <typename TWidget>
 TWidget* FindMainMapWidget(FAutomationTestBase& Test, UWorld* World)
 {
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, TWidget::StaticClass(), true);
	return Test.TestEqual(FString::Printf(TEXT("视口中唯一挂载 %s"), *TWidget::StaticClass()->GetName()), Widgets.Num(), 1)
		? Cast<TWidget>(Widgets[0]) : nullptr;
 }

 /** 在已初始化真实按钮上检查 C++ 事件接线；不点击，以免此检查执行交易或重载关卡。 */
 void CheckActionIconStates(FAutomationTestBase& Test, UUserWidget* Owner, const FName ButtonName, const FName SwitcherName)
 {
	if (!Owner) { return; }
	UButton* Button = Cast<UButton>(Owner->GetWidgetFromName(ButtonName));
	UWidgetSwitcher* Icons = Cast<UWidgetSwitcher>(Owner->GetWidgetFromName(SwitcherName));
	if (!Test.TestNotNull(ButtonName.ToString(), Button) || !Test.TestNotNull(SwitcherName.ToString(), Icons)) { return; }
	Test.TestEqual(TEXT("初始显示普通切图"), Icons->GetActiveWidgetIndex(), 0);
	Button->OnPressed.Broadcast();
	Test.TestEqual(TEXT("按下后显示按压切图"), Icons->GetActiveWidgetIndex(), 1);
	Button->OnReleased.Broadcast();
	Test.TestEqual(TEXT("释放后回到普通切图"), Icons->GetActiveWidgetIndex(), 0);
 }

 /** 在编辑器完成地图加载并启动PIE后检查正式开局，不保存地图。 */
 class FCheckDirectorMainMap final : public IAutomationLatentCommand
 {
 public:
  explicit FCheckDirectorMainMap(FAutomationTestBase* InTest) : Test(InTest) {}
  virtual bool Update() override
  {
	UWorld* World = GEditor->PlayWorld;
	// 正式地图的 BeginPlay 完成后才检查导演、HUD 和视口输入。
	if (FPlatformTime::Seconds() < StartupDeadline && (!World || !World->HasBegunPlay())) { return false; }
	if (!Test->TestTrue(TEXT("独立 MainMap PIE 已开始游戏"), World && World->HasBegunPlay())) { return true; }
	UCastleDirectorSubsystem* Director = World->GetSubsystem<UCastleDirectorSubsystem>();
	ACastlePlayerController* Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
	if (!Test->TestNotNull(TEXT("本地控制器类型正确"), Controller)) { return true; }
	// 编辑器的隐式测试命令可能在 PlayerInput 前推进。至少跨过一帧完整的游戏 Tick，
	// 以免两次 P 键在同一次输入分发中连续进入并退出路径编辑。
	if (GFrameCounter < CheckInputAfterFrame) { return false; }

	if (Stage == 0)
	{
		Test->TestTrue(TEXT("当前确实加载MainMap"), UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()).EndsWith(TEXT("/MainMap")));
		if (!Test->TestTrue(TEXT("真实MainMap导演已自动启动"), Director && Director->IsRunning())) { return true; }
		Test->TestEqual(TEXT("真实开局为编辑阶段"), Director->GetPhase(), ECastleDirectorPhase::Editing);
		Test->TestEqual(TEXT("开局等级为1"), Director->GetLevel(), 1);
		Test->TestEqual(TEXT("开局 Lv1 房间上限30"), Director->GetCurrentRoomLimit(), 30);
		int32 NodeCount = 0;
		for (TActorIterator<ACastleDirectorNodeActor> It(World); It; ++It)
		{
			++NodeCount;
			const auto* Portal = It->FindComponentByClass<UNiagaraComponent>();
			Test->TestTrue(TEXT("已激活浮空节点播放正式 Niagara 圆环，同时保有屏幕标签"),
				Portal && Portal->GetAsset() == Director->GetConfig()->NodePortalSystem && It->IsLabelReady());
			Test->TestTrue(TEXT("门户特效自身不参与碰撞或导航"), Portal
				&& Portal->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Portal->CanEverAffectNavigation());
		}
		Test->TestTrue(TEXT("真实世界中生成2至3个节点"), NodeCount >= 2 && NodeCount <= 3);
		const TActorIterator<APlayerStart> Start(World);
		Test->TestTrue(TEXT("正式 PIE 从主塔正门前的新起点出生"), Start && Controller->GetPawn()
			&& Controller->GetPawn()->GetActorLocation().Equals(Start->GetActorLocation(), 1.0)
			&& Start->GetActorLocation().Equals(FVector(2680, 580, 1882), 1.0));
		FText Reason;
		// 真实 MainMap 开局通过控制器的统一开关入口打开商店，再关闭后继续验证 HUD。
		Test->TestTrue(TEXT("正式开局即可使用普通商店"), Director->IsShopAccessAllowed(Reason));
		Controller->ToggleShop();
		Test->TestTrue(TEXT("正式开局的B键入口可打开商店"), Controller->GetShop()->IsShopOpen());
		Controller->ToggleShop();
		Test->TestFalse(TEXT("关闭后商店界面收起"), Controller->GetShop()->IsShopOpen());
		Test->TestTrue(TEXT("真实HUD显示开局房间计数5/30"), Director->GetHUDSummary().Contains(TEXT("房间 5/30")));
		if (UCastleEconomyHUDWidget* Balance = FindMainMapWidget<UCastleEconomyHUDWidget>(*Test, World))
		{
			// 真实账户事件应该在同一帧更新 WBP，HUD 不应参与中心指针的命中测试。
			Test->TestEqual(TEXT("金币 HUD 不拦截游戏输入"), Balance->GetVisibility(), ESlateVisibility::HitTestInvisible);
			UTextBlock* GoldText = Cast<UTextBlock>(Balance->GetWidgetFromName(TEXT("GoldText")));
			if (Test->TestNotNull(TEXT("金币 WBP 的数值控件"), GoldText))
			{
				const FString OldText = GoldText->GetText().ToString();
				UCastleEconomyComponent* Economy = Controller->GetEconomy();
				Test->TestTrue(TEXT("测试入账成功"), Economy->TryAddGold(25, Reason));
				const FString NewText = GoldText->GetText().ToString();
				Test->TestNotEqual(TEXT("入账事件立即更新 WBP"), NewText, OldText);
				Test->TestTrue(TEXT("WBP 展示真实余额"), NewText.EndsWith(FText::AsNumber(Economy->GetGold()).ToString()));
			}
		}
		FOutputDeviceNull Output;
		if (UCastleDirectorHUDWidget* Status = FindMainMapWidget<UCastleDirectorHUDWidget>(*Test, World))
		{
			// 核对 20 格心形血条与 20 格骷髅进度条的离散比例算法边界。
			int32 FilledCount = 0;
			Test->TestTrue(TEXT("满血为 20 颗心"), UCastleDirectorHUDWidget::ComputeFilledHearts(1000.0f, 1000.0f, FilledCount) && FilledCount == 20);
			Test->TestTrue(TEXT("半血为 10 颗心"), UCastleDirectorHUDWidget::ComputeFilledHearts(500.0f, 1000.0f, FilledCount) && FilledCount == 10);
			Test->TestTrue(TEXT("残血存活至少 1 颗心"), UCastleDirectorHUDWidget::ComputeFilledHearts(1.0f, 1000.0f, FilledCount) && FilledCount == 1);
			Test->TestTrue(TEXT("归零为 0 颗心"), UCastleDirectorHUDWidget::ComputeFilledHearts(0.0f, 1000.0f, FilledCount) && FilledCount == 0);
			Test->TestTrue(TEXT("开战 0 击杀为 0 颗骷髅"), UCastleDirectorHUDWidget::ComputeFilledSkulls(0, 40, FilledCount) && FilledCount == 0);
			Test->TestTrue(TEXT("击杀 1 敌至少点亮 1 颗骷髅"), UCastleDirectorHUDWidget::ComputeFilledSkulls(1, 40, FilledCount) && FilledCount == 1);
			Test->TestTrue(TEXT("半程点亮 10 颗骷髅"), UCastleDirectorHUDWidget::ComputeFilledSkulls(20, 40, FilledCount) && FilledCount == 10);
			Test->TestTrue(TEXT("清场点亮 20 颗骷髅"), UCastleDirectorHUDWidget::ComputeFilledSkulls(40, 40, FilledCount) && FilledCount == 20);

			const UCastleMainTowerComponent* MainTower = World->GetSubsystem<UCastleMainTowerSubsystem>()->GetRegisteredMainTower();
			UCastleBuildBarWidget* BuildBar = FindMainMapWidget<UCastleBuildBarWidget>(*Test, World);
			const UWidgetSwitcher* RightSwitcher = Cast<UWidgetSwitcher>(Status->GetWidgetFromName(TEXT("RightActionSwitcher")));
			const UCastleIconBarWidget* BattleBar = Cast<UCastleIconBarWidget>(Status->GetWidgetFromName(TEXT("BattleProgressBar")));
			const UCastleHUDIconButtonWidget* BuildRedo = Cast<UCastleHUDIconButtonWidget>(Status->GetWidgetFromName(TEXT("BuildRedoButton")));
			const UCastleHUDIconButtonWidget* PathRedo = Cast<UCastleHUDIconButtonWidget>(Status->GetWidgetFromName(TEXT("PathRedoButton")));
			const UCastleHUDIconButtonWidget* GuideBtn = Cast<UCastleHUDIconButtonWidget>(Status->GetWidgetFromName(TEXT("GuideButton")));

			if (Test->TestNotNull(TEXT("已有注册主塔"), MainTower)
				&& Test->TestNotNull(TEXT("右下三态切换器存在"), RightSwitcher)
				&& Test->TestNotNull(TEXT("战斗骷髅进度条存在"), BattleBar)
				&& Test->TestNotNull(TEXT("建造重做按钮存在"), BuildRedo)
				&& Test->TestNotNull(TEXT("路径重做按钮存在"), PathRedo)
				&& Test->TestNotNull(TEXT("左下攻略按钮存在"), GuideBtn))
			{
				Test->TestEqual(TEXT("开局满血显示 20 颗亮心"), Status->GetFilledHeartCount(), 20);
				Test->TestEqual(TEXT("开局为建造页面模式"), Status->GetCurrentScreenMode(), ECastleHUDScreenMode::Build);
				Test->TestEqual(TEXT("开局右下动作栏在建造页(0)"), RightSwitcher->GetActiveWidgetIndex(), 0);
				Test->TestEqual(TEXT("建造页隐藏战斗骷髅进度条"), BattleBar->GetVisibility(), ESlateVisibility::Collapsed);
				Test->TestEqual(TEXT("建造重做无步骤时显示灰态(2)"), BuildRedo->GetActiveIconIndex(), 2);
				Test->TestEqual(TEXT("路径重做无步骤时显示灰态(2)"), PathRedo->GetActiveIconIndex(), 2);
				CheckActionIconStates(*Test, const_cast<UCastleHUDIconButtonWidget*>(GuideBtn), TEXT("ActionButton"), TEXT("IconStates"));
				if (BuildBar)
				{
					Test->TestEqual(TEXT("建造页显示且仅卡槽接收鼠标的建造栏"),
						BuildBar->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
				}

				Test->TestTrue(TEXT("非致死伤害走原生命入口"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleDamageMainTower 25"), Output, World));
				Test->TestEqual(TEXT("受击 25 点后顶部血条立即降至 19 颗心"), Status->GetFilledHeartCount(), 19);
			}
		}

		// 通过真实输入栈投递 P 键，等待下一帧 PlayerInput 分发后再检查路径编辑界面切换。
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::P, IE_Pressed, 1.0f));
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::P, IE_Released, 0.0f));
		CheckInputAfterFrame = GFrameCounter + 2;
		Stage = 1;
		return false;
	}

	if (Stage == 1)
	{
		ACastlePatrolRoute* Route = Cast<ACastlePatrolRoute>(UGameplayStatics::GetActorOfClass(World, ACastlePatrolRoute::StaticClass()));
		UCastleDirectorHUDWidget* Status = FindMainMapWidget<UCastleDirectorHUDWidget>(*Test, World);
		UCastleBuildBarWidget* BuildBar = FindMainMapWidget<UCastleBuildBarWidget>(*Test, World);
		if (Test->TestNotNull(TEXT("场景存在巡逻路径编辑入口"), Route) && Status)
		{
			const UWidgetSwitcher* RightSwitcher = Cast<UWidgetSwitcher>(Status->GetWidgetFromName(TEXT("RightActionSwitcher")));
			Test->TestTrue(TEXT("按 P 进入路径编辑状态"), Route->IsEditing());
			Test->TestEqual(TEXT("HUD 切换到路径编辑界面"), Status->GetCurrentScreenMode(), ECastleHUDScreenMode::PathEdit);
			if (RightSwitcher)
			{
				Test->TestEqual(TEXT("右下动作栏切到路径编辑页(1)"), RightSwitcher->GetActiveWidgetIndex(), 1);
			}
			Test->TestEqual(TEXT("路径编辑界面保持顶部 19 颗心"), Status->GetFilledHeartCount(), 19);
			if (BuildBar)
			{
				Test->TestEqual(TEXT("路径编辑界面隐藏建造栏"), BuildBar->GetVisibility(), ESlateVisibility::Collapsed);
			}
		}
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::P, IE_Pressed, 1.0f));
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::P, IE_Released, 0.0f));
		CheckInputAfterFrame = GFrameCounter + 2;
		Stage = 2;
		return false;
	}

	ACastlePatrolRoute* Route = Cast<ACastlePatrolRoute>(UGameplayStatics::GetActorOfClass(World, ACastlePatrolRoute::StaticClass()));
	UCastleDirectorHUDWidget* Status = FindMainMapWidget<UCastleDirectorHUDWidget>(*Test, World);
	UCastleBuildBarWidget* BuildBar = FindMainMapWidget<UCastleBuildBarWidget>(*Test, World);
	if (Route && Status)
	{
		const UWidgetSwitcher* RightSwitcher = Cast<UWidgetSwitcher>(Status->GetWidgetFromName(TEXT("RightActionSwitcher")));
		Test->TestFalse(TEXT("再次按 P 退出路径编辑"), Route->IsEditing());
		Test->TestEqual(TEXT("HUD 回到建造页面"), Status->GetCurrentScreenMode(), ECastleHUDScreenMode::Build);
		if (RightSwitcher)
		{
			Test->TestEqual(TEXT("右下动作栏回到建造页(0)"), RightSwitcher->GetActiveWidgetIndex(), 0);
		}
		if (BuildBar)
		{
			Test->TestEqual(TEXT("回到建造页恢复建造栏及卡槽命中穿透"),
				BuildBar->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
		}
	}

	FOutputDeviceNull Output;
	CheckActionIconStates(*Test, FindMainMapWidget<UCastleRewardChoiceWidget>(*Test, World), TEXT("ShopButton"), TEXT("ShopIconStates"));
	Test->TestTrue(TEXT("真实伤害命令可执行"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleDamageMainTower 99999"), Output, World));
	Test->TestEqual(TEXT("主塔死亡触发正式游戏结束"), Director->GetPhase(), ECastleDirectorPhase::GameOver);
	Test->TestTrue(TEXT("世界已暂停"), World->IsPaused());
	Test->TestTrue(TEXT("游戏结束鼠标可见"), Controller->bShowMouseCursor);
	Test->TestTrue(TEXT("游戏结束已锁定移动"), Controller->IsMoveInputIgnored());
	Test->TestTrue(TEXT("游戏结束已锁定视角"), Controller->IsLookInputIgnored());
	CheckActionIconStates(*Test, FindMainMapWidget<UCastleGameOverWidget>(*Test, World), TEXT("RestartButton"), TEXT("RestartIconStates"));
	Test->AddInfo(TEXT("MainMap启动、节点、三态HUD、商店门控、正式主塔死亡与暂停路径已验证。"));
	return true;
  }
 private:
  FAutomationTestBase* Test;
  const double StartupDeadline = FPlatformTime::Seconds() + 20.0;
  uint64 CheckInputAfterFrame = 0;
  int32 Stage = 0;
 };
#if !UE_BUILD_SHIPPING
	/** 在正式 MainMap 领取商店奖励，通过休息岛交互验证常驻商店及建造栏。 */
 class FCheckIslandShopAndBuildBar final : public IAutomationLatentCommand
 {
 public:
  explicit FCheckIslandShopAndBuildBar(FAutomationTestBase& InTest) : Test(InTest) {}

  bool Update() override
  {
	UWorld* World = GEditor->PlayWorld;
	auto* Director = World ? World->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
	if (!Director || !Director->IsRunning()) { return WaitForDirector(TEXT("MainMap 导演未就绪")); }
	auto* Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
	if (!Test.TestNotNull(TEXT("MainMap 本地控制器"), Controller)) { return true; }
	FText Reason;
	FOutputDeviceNull Output;

	switch (Stage)
	{
	case EStage::EarnShop:
	{
		// 本用例从奖励选择开始。使用现有阶段测试入口，略过由战斗测试覆盖的路线布置与清场。
		TActorIterator<ACastleBoard> Board(World);
		auto* Tower = World->GetSubsystem<UCastleMainTowerSubsystem>()->GetRegisteredMainTower();
		if (!Test.TestTrue(TEXT("仍使用正式 MainMap 的棋盘、主塔与导演配置"), Board && Tower && Director->GetConfig()))
		{
			return true;
		}
		Director->ConfigureForAutomation(Director->GetConfig(), *Board, Controller, Tower);
		if (!Test.TestTrue(TEXT("建立清场后的奖励阶段"), Director->SetPhaseForAutomation(ECastleDirectorPhase::Battle)
			&& Director->SetPhaseForAutomation(ECastleDirectorPhase::Reward)))
		{
			return true;
		}
		if (!Test.TestTrue(TEXT("领取商店奖励"), Director->TryChooseReward(ECastleDirectorReward::Shop, Reason)))
		{
			Test.AddError(Reason.ToString());
			return true;
		}
		Test.TestTrue(TEXT("进岛前已拥有主塔商店权限"), Director->IsShopAccessAllowed(Reason));
		if (!Test.TestTrue(TEXT("休息岛调试命令已执行"),
			IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleEnterSkyIsland Rest"), Output, World))
			|| !Test.TestEqual(TEXT("当前为空岛阶段"), Director->GetPhase(), ECastleDirectorPhase::SkyIsland))
		{
			return true;
		}
		Stage = EStage::InteractOnIsland;
		return false;
	}
	case EStage::InteractOnIsland:
	{
		// 读取视口中实际挂载的 WBP，将射线对准休息岛商店台及离开传送门。
		auto* BuildBar = FindMainMapWidget<UCastleBuildBarWidget>(Test, World);
		TActorIterator<ACastleRestSkyIsland> Island(World);
		if (!BuildBar || !Test.TestTrue(TEXT("正式休息岛已生成"), static_cast<bool>(Island))) { return true; }
		ACastleRestSkyIsland* RestIsland = *Island;
		Test.TestEqual(TEXT("入岛后建造栏可见"), BuildBar->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
		Test.TestFalse(TEXT("岛内主塔商店权限不可用"), Director->IsShopAccessAllowed(Reason));
		Controller->ToggleShop();
		Test.TestFalse(TEXT("岛内 B 键入口无法打开主塔商店"), Controller->GetShop()->IsShopOpen());
		auto* Shop = FindObjectFast<UCastleRestSkyIslandStation>(RestIsland, TEXT("ShopStation"));
		if (!Interact(*Director, Shop, TEXT("休息岛免费商店台"))) { return true; }
		Test.TestTrue(TEXT("空岛商店已打开"), Controller->GetShop()->IsShopOpen());
		Test.TestEqual(TEXT("空岛商店打开时折叠建造栏"), BuildBar->GetVisibility(), ESlateVisibility::Collapsed);
		Controller->GetShop()->CloseShop();
		Test.TestEqual(TEXT("空岛商店关闭后恢复建造栏"), BuildBar->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
		if (!Interact(*Director, RestIsland->FindComponentByClass<UCastleSkyIslandDepartureComponent>(), TEXT("离开传送门")))
		{
			return true;
		}
		Stage = EStage::Returned;
		return false;
	}
	case EStage::Returned:
		if (Director->GetPhase() == ECastleDirectorPhase::SkyIsland) { return WaitForDirector(TEXT("未完成离岛")); }
		Test.TestEqual(TEXT("返塔进入编辑阶段"), Director->GetPhase(), ECastleDirectorPhase::Editing);
		Test.TestTrue(TEXT("返塔后普通商店仍可用"), Director->IsShopAccessAllowed(Reason));
		Controller->ToggleShop();
		Test.TestTrue(TEXT("返塔后 B 键入口可打开主塔商店"), Controller->GetShop()->IsShopOpen());
		Controller->GetShop()->CloseShop();
		return true;
	}
	return true;
  }

 private:
  enum class EStage { EarnShop, InteractOnIsland, Returned };
  FAutomationTestBase& Test;
  EStage Stage = EStage::EarnShop;
  double Deadline = 0.0;

  /** PIE 事件由世界 Tick 提交，按真实时间设置上限并报告超时阶段。 */
  bool WaitForDirector(const TCHAR* Failure)
  {
	const double Now = FPlatformTime::Seconds();
	if (Deadline == 0.0) { Deadline = Now + 30.0; }
	if (Now < Deadline) { return false; }
	Test.AddError(Failure);
	return true;
  }

  /** 保留正式的交互射线检查，从可重复的位置瞄准目标。 */
  bool Interact(UCastleDirectorSubsystem& Director, const USceneComponent* Target, const TCHAR* Name)
  {
	if (!Test.TestNotNull(Name, Target)) { return false; }
	FText Reason;
	const FVector Location = Target->GetComponentLocation();
	const bool bInteracted = Director.TryInteractWithSkyIsland(Location + FVector::UpVector * 450.0, Location, Reason);
	return Test.TestTrue(FString::Printf(TEXT("交互 %s：%s"), Name, *Reason.ToString()), bInteracted);
  }
 };
#endif

 /** 在实际塔模型和正式地图上验证四侧每一格；不是通过构造光标结构绕过射线。 */
 class FCheckMainTowerSidePlacement final : public IAutomationLatentCommand
 {
 public:
  explicit FCheckMainTowerSidePlacement(FAutomationTestBase* InTest) : Test(InTest) {}
  bool Update() override
  {
   UWorld* World = GEditor->PlayWorld;
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
   for (int32 Layer = 0; Layer < Board->GetUnlockedLayerCount(); ++Layer)
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
     Test->TestTrue(TEXT("合法玩法色与 1～9 数字"), CastleTileColors::IsPlayable(Data->Color)
         && Data->Number >= 1 && Data->Number <= 9);
     // 高层同样能直接起建，不要求在塔脚先造一根支撑柱。
     Probe.Color = Data->Color;
     Probe.Number = Data->Number;
     Test->TestTrue(TEXT("实际命中的每个侧格都可摆放匹配房间"), Board->CanPlaceBuildingAtCursor(Target, Probe, Reason));
     UMaterialInstanceDynamic* PanelMaterial = Cast<UMaterialInstanceDynamic>(Data->Panel->GetMaterial(0));
     FLinearColor ExpectedTint;
     Test->TestTrue(TEXT("纹理面板可见且仅参与可见性查询，不占实体碰撞或导航"), Data->Panel->IsVisible()
         && Data->Panel->GetCollisionEnabled() == ECollisionEnabled::QueryOnly
         && !Data->Panel->CanEverAffectNavigation() && PanelMaterial != nullptr);
     if (PanelMaterial)
     {
         Test->TestTrue(TEXT("棋盘贴图数字帧与格子数字一致"), FMath::IsNearlyEqual(
             PanelMaterial->K2_GetScalarParameterValue(TEXT("Frame (S)")), static_cast<float>(Data->Number - 1)));
         Test->TestTrue(TEXT("棋盘贴图颜色与格子颜色一致"), CastleTileColors::TryGetGridTint(Data->Color, ExpectedTint)
             && PanelMaterial->K2_GetVectorParameterValue(TEXT("BackgroundColor")).Equals(ExpectedTint, 0.001f));
     }
    }
   }

   FCastleGridCursorTarget BottomTarget, Other;
   if (!Test->TestTrue(TEXT("左键样例牌入口可解析首层 -X 格"), Resolve(Base, Offsets[0], BottomTarget))) { return true; }
   const FVector TowerBottom = Board->MainTowerActor->GetActorLocation();
   Test->TestFalse(TEXT("塔顶不提供放置格"), Board->TryResolveGridCursorTarget(
       TowerBottom + FVector(0, 0, 7000), TowerBottom + FVector(0, 0, 5000), Other));
   // 正式手牌已打乱。按实际落点找到能匹配后门的任一张牌，再从同一槽用生产入口放置。
   const auto Hand = Board->PrototypeBackpack->GetItems();
   const int32 StarterIndex = Hand.IndexOfByPredicate([&](const FCastleBuildingDescriptor& Card)
   {
    return Board->CanPlaceBuildingAtCursor(BottomTarget, Card, Reason);
   });
   if (!Test->TestTrue(TEXT("五张实际开局牌中至少一张匹配首层 -X"), Hand.Num() == 5 && StarterIndex != INDEX_NONE)) { return true; }
   const FCastleBuildingDescriptor& Starter = Hand[StarterIndex];
   const FCastleTowerSideCell* BottomCell = Board->FindTowerSideCell(Base, Faces[0]);
   FCastleBuildingDescriptor OffColor = Starter;
   OffColor.Color = BottomCell->Color == ECastleTileColor::Red ? ECastleTileColor::Blue : ECastleTileColor::Red;
   OffColor.Number = BottomCell->Number;
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
   ACastleBuildingBase* Room = Board->PlaceBackpackBuildingAtCursor(BottomTarget, StarterIndex, Reason);
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

}

/** 真实MainMap通过编辑器加载和PIE验证，避免未烘焙-game进程缺失外部Actor的差异。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorMainMapTest, "MovingCastle.Director.MainMapSmoke",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleDirectorMainMapTest::RunTest(const FString& Parameters)
{
 if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
 ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.1f));
 ADD_LATENT_AUTOMATION_COMMAND(FCheckDirectorMainMap(this));
 // 游戏结束场景会暂停世界，仍由编辑器统一结束本用例的 PIE。
 FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
 return true;
}
#if !UE_BUILD_SHIPPING
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorIslandShopAndBuildBarTest, "MovingCastle.Director.IslandShopAndBuildBar",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleDirectorIslandShopAndBuildBarTest::RunTest(const FString& Parameters)
{
 if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
 ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.1f));
 ADD_LATENT_AUTOMATION_COMMAND(FCheckIslandShopAndBuildBar(*this));
 FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
 return true;
}
#endif
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleTowerSideMainMapTest, "MovingCastle.Placement.MainTowerSides",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleTowerSideMainMapTest::RunTest(const FString& Parameters)
{
 if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
 ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.1f));
 ADD_LATENT_AUTOMATION_COMMAND(FCheckMainTowerSidePlacement(this));
 FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
 return true;
}

#endif
