// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Castle/Automation/CastlePIEAutomation.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "Castle/UI/Director/CastleTowerUpgradeRewardWidget.h"
#include "Castle/UI/Shop/CastleShopWidget.h"
#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingDefinition.h"
#include "CastleMainTowerComponent.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "EngineUtils.h"

namespace
{
	/** 在正式 MainMap 上使用真实商店、侧格、背包和领奖控件，手动推进导演 Tick 核对触发边界。 */
	class FCastleRoomGrowthScenario final : public IAutomationLatentCommand
	{
	public:
		explicit FCastleRoomGrowthScenario(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = GEditor->PlayWorld;
			auto* Director = World ? World->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
			if ((!Director || !Director->IsRunning()) && FPlatformTime::Seconds() < StartupDeadline) { return false; }
			if (Test.TestTrue(TEXT("正式导演已启动"), Director && Director->IsRunning())) { VerifyRoomGrowth(*World, *Director); }
			return true;
		}

	private:
		template<typename TWidget>
		TWidget* FindViewportWidget(UWorld& World)
		{
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&World, Widgets, TWidget::StaticClass(), true);
			if (!Test.TestEqual(TWidget::StaticClass()->GetName(), Widgets.Num(), 1)) { return nullptr; }
			return CastChecked<TWidget>(Widgets[0]);
		}

		void CheckProgress(const UCastleShopWidget& Widget, const FString& Label, const float Percent)
		{
			const auto* Text = Cast<UTextBlock>(Widget.GetWidgetFromName(TEXT("TowerLevelText")));
			const auto* Bar = Cast<UProgressBar>(Widget.GetWidgetFromName(TEXT("TowerExpBar")));
			if (Test.TestNotNull(TEXT("正式商店绑定进度文字"), Text) && Test.TestNotNull(TEXT("正式商店绑定进度条"), Bar))
			{
				Test.TestEqual(TEXT("标题使用当前占格与本级容量"), Text->GetText().ToString(), Label);
				Test.TestTrue(TEXT("正式进度条反映已提交主塔等级"), FMath::IsNearlyEqual(Bar->GetPercent(), Percent));
			}
		}

		bool AddCells(UCastleBackpackComponent& Backpack, const FCastleBuildingDescriptor& Card, const int32 Cards)
		{
			if (!Test.TestTrue(TEXT("测试补牌数量有效"), Cards > 0)) { return false; }
			TArray<FCastleBuildingDescriptor> Batch;
			Batch.Init(Card, Cards);
			FText Reason;
			if (Backpack.TryAddItemsAllowOverflow(Batch, Reason)) { return true; }
			Test.AddError(Reason.ToString());
			return false;
		}

		void VerifyRoomGrowth(UWorld& World, UCastleDirectorSubsystem& Director)
		{
			auto* Player = Cast<ACastlePlayerController>(World.GetFirstPlayerController());
			TActorIterator<ACastleBoard> Boards(&World);
			auto* Tower = World.GetSubsystem<UCastleMainTowerSubsystem>()->GetRegisteredMainTower();
			if (!Test.TestNotNull(TEXT("正式本地控制器"), Player) || !Test.TestTrue(TEXT("正式棋盘存在"), bool(Boards))
				|| !Test.TestNotNull(TEXT("正式主塔生命组件"), Tower)) { return; }
			ACastleBoard& Board = **Boards;
			UCastleBackpackComponent& Backpack = *Board.PrototypeBackpack;
			UCastleShopComponent& Shop = *Player->GetShop();
			auto* ShopWidget = FindViewportWidget<UCastleShopWidget>(World);
			auto* Upgrade = FindViewportWidget<UCastleTowerUpgradeRewardWidget>(World);
			if (!ShopWidget || !Upgrade) { return; }
			auto* Rows = Cast<UPanelWidget>(Upgrade->GetWidgetFromName(TEXT("RewardRows")));
			if (!Test.TestNotNull(TEXT("升级奖励 WBP 绑定行容器"), Rows)) { return; }

			FText Reason;
			const auto* Normal = Board.FindBuildingDefinition(ECastleBuildingType::NormalRoom);
			FCastleBuildingDescriptor OneCell;
			const FVector Center = Board.GetGridWorldLocation(Board.GetBoardCenterCoordinate());
			FCastleGridCursorTarget Target;
			if (!Test.TestTrue(TEXT("真实主塔侧格可选中"), Board.TryResolveGridCursorTarget(
				Center - Board.GetActorForwardVector() * Board.CellSize * 2.0, Center, Target))) { return; }
			const auto* Side = Board.FindTowerSideCell(Target.Cell, Target.Face);
			if (!Test.TestTrue(TEXT("一级房牌面匹配实际塔格"), Side && Normal && Normal->CreateDescriptor(
				1, ECastleTileColor::Red, Side->Number, OneCell, Reason))) { return; }
			auto* Placed = Board.PlaceBuildingAtCursor(Target, OneCell, false, Reason);
			if (!Test.TestTrue(TEXT("正式建造面登记一格"), CastleBuildingTests::CompletePlacement(Placed))) { return; }
			Test.TestEqual(TEXT("棋盘一格加开局主栏五格"), Director.GetOwnedRoomCount(), 6);

			Player->ToggleShop();
			if (!Test.TestTrue(TEXT("正式商店模态已打开"), Shop.IsShopOpen() && Player->IsModalInputActive())) { return; }
			CheckProgress(*ShopWidget, TEXT("主塔 Lv.1 (6/30)"), 6.0f / 30.0f);
			if (!Test.TestTrue(TEXT("卖牌腾一个真实背包槽"), Shop.TrySellBackpackItem(0, Reason))) { return; }
			CheckProgress(*ShopWidget, TEXT("主塔 Lv.1 (5/30)"), 5.0f / 30.0f);
			if (!Test.TestTrue(TEXT("购买一格房间"), Shop.TryBuyOffer(0, Reason))) { return; }
			CheckProgress(*ShopWidget, TEXT("主塔 Lv.1 (6/30)"), 6.0f / 30.0f);
			Player->ToggleShop();
			// 新发的 24 张先补满主栏剩余五槽，再把 19 张压入临时栏，合计恰好达阈值。
			if (!AddCells(Backpack, OneCell, 24)) { return; }
			Test.TestEqual(TEXT("十九张待放置的单格房排在临时栏"), Backpack.GetTempItemCount(), 19);
			Player->ToggleShop();
			Test.TestEqual(TEXT("导演和商店共同读到30格"), Shop.GetOwnedRoomCount(), Director.GetOwnedRoomCount());
			CheckProgress(*ShopWidget, TEXT("主塔 Lv.1 (30/30)"), 1.0f);
			const auto Offers = Shop.GetOffers();
			const int32 Unsold = Offers.IndexOfByPredicate([](const FCastleShopOffer& Offer) { return !Offer.bSold; });
			if (!Test.TestTrue(TEXT("留有可购买的商品用于上限判定"), Unsold != INDEX_NONE)) { return; }
			Test.TestFalse(TEXT("当前已达到容量时禁止购买"), Shop.TryBuyOffer(Unsold, Reason));
			Test.TestTrue(TEXT("拒绝来自房间总量"), Reason.ToString().Contains(TEXT("房间总量已达到上限")));
			Director.Tick(0.0f);
			Test.TestEqual(TEXT("开店时不升级"), Director.GetLevel(), 1);
			Test.TestTrue(TEXT("商店仍保持打开"), Shop.IsShopOpen());
			Test.TestEqual(TEXT("模态期间升级奖励面板收起"), Upgrade->GetVisibility(), ESlateVisibility::Collapsed);
			Player->ToggleShop();

			// 缺少定义池只触发一次错误，既不生成待领条目，也不推进主塔状态。
			Tower->ApplyDamage(25.0f);
			const auto Definitions = Board.BuildingDefinitions;
			Board.BuildingDefinitions.Reset();
			Test.AddExpectedError(TEXT("万能卡选项配置无效"), EAutomationExpectedErrorFlags::Contains, 1);
			Test.AddExpectedError(TEXT("主塔升级失败，配置"), EAutomationExpectedErrorFlags::Contains, 1);
			Director.Tick(0.0f);
			Director.Tick(0.0f);
			Board.BuildingDefinitions = Definitions;
			Test.TestEqual(TEXT("失败保留等级"), Director.GetLevel(), 1);
			Test.TestEqual(TEXT("失败保留塔层"), Board.GetUnlockedLayerCount(), 3);
			Test.TestEqual(TEXT("失败保留血量"), Tower->CurrentHealth, 975.0f);
			Test.TestEqual(TEXT("失败保留房间上限"), Shop.RoomLimit, 30);
			Test.TestEqual(TEXT("失败不发布奖励"), Rows->GetChildrenCount(), 0);

			Player->ToggleControlSettings();
			Director.Tick(0.0f);
			Test.TestEqual(TEXT("设置模态保持等级为1"), Director.GetLevel(), 1);
			Player->ToggleControlSettings();
			Director.Tick(0.0f);
			if (!Test.TestEqual(TEXT("无模态的第一个 Tick 升 Lv2"), Director.GetLevel(), 2)
				|| !Test.TestEqual(TEXT("Lv2 四条待领项目"), Rows->GetChildrenCount(), 4)) { return; }
			Test.TestEqual(TEXT("Lv2 奖励面板可见"), Upgrade->GetVisibility(), ESlateVisibility::Visible);
			Test.TestEqual(TEXT("Lv2 扩为五层"), Board.GetUnlockedLayerCount(), 5);
			Test.TestEqual(TEXT("Lv2 补满到2000"), Tower->CurrentHealth, 2000.0f);
			Test.TestEqual(TEXT("Lv2 容量更新到80"), Shop.RoomLimit, 80);
			CastChecked<UButton>(Rows->GetChildAt(1))->OnClicked.Broadcast();
			Test.TestEqual(TEXT("点击 Lv2 普通 NPC 项后待命池加2"), Director.GetRunNPCCount(), 3);
			Director.DiscardTowerUpgradeRewards();

			// 一次达到 Lv3 阈值，从 Lv2 直接升到 Lv4，两级待领奖励在同一面板。
			if (!AddCells(Backpack, OneCell, 120)) { return; }
			Director.Tick(0.0f);
			if (!Test.TestEqual(TEXT("150 格从 Lv2 跨到 Lv4"), Director.GetLevel(), 4)
				|| !Test.TestEqual(TEXT("Lv3 与 Lv4 合计 19 行赠品"), Rows->GetChildrenCount(), 19)) { return; }
			CastChecked<UButton>(Rows->GetChildAt(1))->OnClicked.Broadcast();
			CastChecked<UButton>(Rows->GetChildAt(7))->OnClicked.Broadcast();
			Test.TestEqual(TEXT("跨级两行分别可领 3 与 5 名 NPC"), Director.GetRunNPCCount(), 11);
			Director.DiscardTowerUpgradeRewards();
			if (!AddCells(Backpack, OneCell, 350)) { return; }
			Director.Tick(0.0f);
			Test.TestEqual(TEXT("达到 Lv4 容量后仍是 Lv4"), Director.GetLevel(), 4);
			Test.TestEqual(TEXT("满级继续保持九层"), Board.GetUnlockedLayerCount(), 9);
			Test.TestEqual(TEXT("达到500格没有新奖励面板"), Upgrade->GetVisibility(), ESlateVisibility::Collapsed);
			Player->ToggleShop();
			Test.TestEqual(TEXT("满级房间上限500"), Shop.RoomLimit, 500);
			Test.TestEqual(TEXT("当前已拥有500格"), Shop.GetOwnedRoomCount(), 500);
			Test.TestFalse(TEXT("Lv4 达上限仍阻止购买"), Shop.TryBuyOffer(Unsold, Reason));
			CheckProgress(*ShopWidget, TEXT("主塔 Lv.4 (已满级)"), 1.0f);
			Player->ToggleShop();
			Test.TestTrue(TEXT("拆除已登记房间"), Board.RemoveBuilding(Placed, Reason));
			Director.Tick(0.0f);
			Test.TestEqual(TEXT("房间减少后保持满级"), Director.GetLevel(), 4);
			Test.TestEqual(TEXT("房间减少后仍保留容量"), Director.GetCurrentRoomLimit(), 500);
		}

		FAutomationTestBase& Test;
		double StartupDeadline = FPlatformTime::Seconds() + 30.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorRoomGrowthTest, "MovingCastle.Director.RoomCapacityAndGrowth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDirectorRoomGrowthTest::RunTest(const FString&)
{
	if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FCastleRoomGrowthScenario(*this));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
	return true;
}
#endif
