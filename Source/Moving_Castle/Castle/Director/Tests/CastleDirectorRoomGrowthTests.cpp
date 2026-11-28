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
			Test.TestEqual(TEXT("只统计棋盘上已建造的一格"), Director.GetOwnedRoomCount(), 1);

			Player->ToggleShop();
			if (!Test.TestTrue(TEXT("正式商店模态已打开"), Shop.IsShopOpen() && Player->IsModalInputActive())) { return; }
			CheckProgress(*ShopWidget, TEXT("主塔 Lv.1 (1/30)"), 1.0f / 30.0f);
			if (!Test.TestTrue(TEXT("卖牌腾一个真实背包槽"), Shop.TrySellBackpackItem(0, Reason))) { return; }
			CheckProgress(*ShopWidget, TEXT("主塔 Lv.1 (1/30)"), 1.0f / 30.0f);
			if (!Test.TestTrue(TEXT("购买一格房间"), Shop.TryBuyOffer(0, Reason))) { return; }
			CheckProgress(*ShopWidget, TEXT("主塔 Lv.1 (1/30)"), 1.0f / 30.0f);
			Player->ToggleShop();
			// 两栏库存达到阈值的数值时，导演仍须等待真实建造。
			if (!AddCells(Backpack, OneCell, 25)) { return; }
			Test.TestEqual(TEXT("二十张待放置的单格房排在临时栏"), Backpack.GetTempItemCount(), 20);
			Director.Tick(0.0f);
			Test.TestEqual(TEXT("库存达到30张仍为 Lv1"), Director.GetLevel(), 1);
			Test.TestEqual(TEXT("库存不计入导演场内占格"), Director.GetOwnedRoomCount(), 1);
			if (!Test.TestTrue(TEXT("通过真实放置达到30格"), CastleBuildingTests::FillRoomCells(Board, *Placed, OneCell, 30, Reason)))
			{
				Test.AddError(Reason.ToString());
				return;
			}
			Player->ToggleShop();
			Test.TestEqual(TEXT("商店读到30格"), Shop.GetOwnedRoomCount(), 30);
			Test.TestEqual(TEXT("导演读到30格"), Director.GetOwnedRoomCount(), 30);
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

			// 逐级走实际占格阈值，每级提交后才允许继续建造。
			for (const int32 Limit : {80, 150})
			{
				if (!Test.TestTrue(TEXT("建到当前主塔容量"), CastleBuildingTests::FillRoomCells(Board, *Placed, OneCell, Limit, Reason)))
				{
					Test.AddError(Reason.ToString());
					return;
				}
				Director.Tick(0.0f);
				const int32 ExpectedLevel = Limit == 80 ? 3 : 4;
				if (!Test.TestEqual(TEXT("达到实际占格阈值后升级"), Director.GetLevel(), ExpectedLevel)
					|| !Test.TestEqual(TEXT("对应级别待领奖励"), Rows->GetChildrenCount(), ExpectedLevel == 3 ? 7 : 12)) { return; }
				CastChecked<UButton>(Rows->GetChildAt(1))->OnClicked.Broadcast();
				Director.DiscardTowerUpgradeRewards();
			}
			Test.TestEqual(TEXT("Lv3 与 Lv4 分别领取 3 与 5 名 NPC"), Director.GetRunNPCCount(), 11);
			// 留出最后两层中一个可建造的塔侧邻格，用于检验满容量时的预览和点击。
			FCastleGridCursorTarget Extra = Target;
			Extra.Cell.Z += Board.GetUnlockedLayerCount() - 1;
			Extra.AdjacentCell.Z = Extra.Cell.Z;
			const auto* ExtraSide = Board.FindTowerSideCell(Extra.Cell, Extra.Face);
			if (!Test.TestNotNull(TEXT("满级顶层侧格"), ExtraSide)) { return; }
			FCastleBuildingDescriptor ExtraCard = OneCell;
			ExtraCard.Number = ExtraSide->Number;
			if (!Test.TestTrue(TEXT("通过真实建造达到500格"), CastleBuildingTests::FillRoomCells(
				Board, *Placed, OneCell, 500, Reason, Extra.AdjacentCell)))
			{
				Test.AddError(Reason.ToString());
				return;
			}
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
			Test.TestFalse(TEXT("满500格时预览拒绝"), Board.CanPlaceBuildingAtCursor(Extra, ExtraCard, Reason));
			Test.TestTrue(TEXT("预览提示容量原因"), Reason.ToString().Contains(TEXT("超过当前主塔房间上限")));
			Test.TestNull(TEXT("满500格时坐标放置拒绝"), Board.PlaceBuilding(Extra.AdjacentCell, ExtraCard, false, Reason));
			Test.TestTrue(TEXT("坐标入口给出同一容量原因"), Reason.ToString().Contains(TEXT("超过当前主塔房间上限")));
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
