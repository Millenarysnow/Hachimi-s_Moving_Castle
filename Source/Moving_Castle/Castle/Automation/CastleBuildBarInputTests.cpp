// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Castle/UI/BuildBar/CastleBuildBarWidget.h"
#include "Castle/UI/BuildBar/CastleBuildBarSlotWidget.h"
#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Input/Events.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Slate/SceneViewport.h"
#include "Tests/AutomationCommon.h"

namespace
{
	/** 测试使用正式视口、控制器和 WBP；每个操作都等待下一帧处理输入和刷新画面。 */
	class FCheckBuildBarWheel final : public IAutomationLatentCommand
	{
	public:
		explicit FCheckBuildBarWheel(FAutomationTestBase& InTest) : Test(InTest) {}

		bool Update() override
		{
			UGameViewportClient* ViewportClient = GEngine->GameViewport;
			UWorld* World = IsValid(ViewportClient) ? ViewportClient->GetWorld() : nullptr;
			FSceneViewport* Viewport = IsValid(ViewportClient) ? ViewportClient->GetGameViewport() : nullptr;
			if (!Test.TestNotNull(TEXT("MainMap 的游戏世界"), World)
				|| !Test.TestNotNull(TEXT("原生场景视口有有效尺寸"), Viewport)
				|| !Test.TestTrue(TEXT("场景视口完成布局"), Viewport->GetSizeXY() != FIntPoint::ZeroValue))
			{
				return true;
			}
			ACastlePlayerController* Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
			ACastleBoard* Board = Cast<ACastleBoard>(UGameplayStatics::GetActorOfClass(World, ACastleBoard::StaticClass()));
			if (!Test.TestNotNull(TEXT("正式城堡控制器"), Controller) || !Test.TestNotNull(TEXT("正式棋盘"), Board)
				|| !Test.TestNotNull(TEXT("棋盘背包"), Board->PrototypeBackpack.Get()))
			{
				return true;
			}
			UCastleBackpackComponent& Backpack = *Board->PrototypeBackpack;
			UCastleShopComponent& Shop = *Controller->GetShop();
			if (NextStep == 0 && !Test.TestEqual(TEXT("MainMap 开局有满 10 张牌"), Backpack.GetItemCount(), 10))
			{
				return true;
			}
			const FStep& Previous = Steps[NextStep];
			if (!Test.TestEqual(Previous.Description, Controller->GetSelectedBackpackIndex(), Previous.ExpectedSlot)
				|| !CheckPresentation(*World, Backpack.GetItems(), Previous)
				|| !Test.TestTrue(TEXT("输入步骤期间保持指定操作模式"), Controller->GetControlMode() == ExpectedMode))
			{
				Finish(Backpack, Shop);
				return true;
			}
			if (++NextStep == UE_ARRAY_COUNT(Steps))
			{
				Finish(Backpack, Shop);
				return true;
			}

			const FStep& Step = Steps[NextStep];
			bool bActionSucceeded = true;
			switch (Step.Action)
			{
			case EAction::Up:
			case EAction::Down:
			{
				// 从真实视口入口注入滚轮，覆盖引擎方向事件和轴事件的分发；不直接调用被测切换函数。
				const FGeometry& Geometry = Viewport->GetCachedGeometry();
				const FVector2D Position = Geometry.LocalToAbsolute(FVector2D(Geometry.GetLocalSize()) * 0.5);
				const float Delta = Step.Action == EAction::Up ? 1.0f : -1.0f;
				const FPointerEvent Event(0u, 0u, Position, Position, TSet<FKey>(), FKey(), Delta, FModifierKeysState());
				Viewport->OnMouseWheel(Geometry, Event);
				break;
			}
			case EAction::EnterDesignMode:
				ExpectedMode = ECastleControlMode::Design;
				Controller->SetControlMode(ExpectedMode, false);
				break;
			case EAction::EnterClassicMode:
				ExpectedMode = ECastleControlMode::Classic;
				Controller->SetControlMode(ExpectedMode, false);
				break;
			case EAction::ClickFirstSlot:
			case EAction::ClickSecondSlot:
			{
				TArray<UUserWidget*> Bars;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Bars, UCastleBuildBarWidget::StaticClass(), true);
				auto* Slot = Bars.Num() == 1
					? Cast<UCastleBuildBarSlotWidget>(Bars[0]->GetWidgetFromName(
						Step.Action == EAction::ClickFirstSlot ? TEXT("Slot01") : TEXT("Slot02"))) : nullptr;
				if (!Test.TestNotNull(TEXT("真实建造栏点击卡槽"), Slot)) { bActionSucceeded = false; break; }
				const FGeometry& Geometry = Slot->GetCachedGeometry();
				const FVector2D Position = Geometry.LocalToAbsolute(FVector2D(Geometry.GetLocalSize()) * 0.5);
				const FPointerEvent Click(0u, 0u, Position, Position, TSet<FKey>{EKeys::LeftMouseButton},
					EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
				bActionSucceeded = Slot->NativeOnMouseButtonDown(Geometry, Click).IsEventHandled();
				break;
			}
			case EAction::Zero:
			case EAction::One:
			{
				// 数字键仍由 PlayerInput 分发，确认它和滚轮读写同一个实际选中槽位。
				const FKey Key = Step.Action == EAction::Zero ? EKeys::Zero : EKeys::One;
				Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.0f));
				Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.0f));
				break;
			}
			case EAction::ReduceCards:
				// 用真实背包接口移除尾部卡牌，不改 WBP 或控制器的私有状态。
				while (Backpack.GetItemCount() > Step.ExpectedSlot + 1)
				{
					FCastleBuildingDescriptor Card;
					FText Reason;
					if (!Backpack.TryRemoveItemAt(Backpack.GetItemCount() - 1, Card, Reason))
					{
						Test.AddError(Reason.ToString());
						bActionSucceeded = false;
						break;
					}
					RemovedCards.Add(Card);
				}
				break;
			case EAction::OpenShop:
			{
				// 直接走正式奖励开店事务，令商店组件驱动实际模态输入；不要求测试等待完整战斗波次。
				FText Reason;
				bActionSucceeded = Shop.TryOpenForReward(Controller->GetEconomy(), Board, Reason);
				if (!bActionSucceeded) { Test.AddError(Reason.ToString()); }
				break;
			}
			case EAction::CloseShop:
				Shop.CloseShop();
				break;
			case EAction::RestoreCards:
				bActionSucceeded = RestoreCards(Backpack);
				break;
			case EAction::Initial:
				checkNoEntry(); // 第一项只验证初始状态，没有输入动作。
				break;
			}
			if (!bActionSucceeded) { Finish(Backpack, Shop); }
			return !bActionSucceeded;
		}

	private:
		enum class EAction { Initial, Up, Down, EnterDesignMode, EnterClassicMode, ClickFirstSlot, ClickSecondSlot,
			Zero, One, ReduceCards, RestoreCards, OpenShop, CloseShop };
		struct FStep
		{
			EAction Action;
			int32 ExpectedSlot;
			const TCHAR* Description;
		};

		/** 每一项先提交输入，再核对该帧处理后的控制器索引和 WBP 金环。 */
		static constexpr FStep Steps[] = {
			{EAction::Initial, 0, TEXT("开局选中第一槽")},
			{EAction::EnterDesignMode, 0, TEXT("设计模式操作下保持选牌状态")},
			{EAction::ClickSecondSlot, 1, TEXT("设计模式点击第二张卡牌后选中状态切换")},
			{EAction::EnterClassicMode, 1, TEXT("返回经典模式后保持选牌状态")},
			{EAction::ClickFirstSlot, 0, TEXT("经典模式点击第一张卡牌后选中状态切换")},
			{EAction::Up, 0, TEXT("满包首槽向上保持选中，不穿透到尾槽")},
			{EAction::Down, 1, TEXT("首槽向下只向右移动一格")},
			{EAction::Up, 0, TEXT("滚轮向上只向左移动一格")},
			{EAction::Zero, 9, TEXT("数字键 0 仍直选第十槽")},
			{EAction::Down, 9, TEXT("满包尾槽向下保持选中，不穿透到首槽")},
			{EAction::Up, 8, TEXT("满包尾槽反向滚动可回到相邻槽")},
			{EAction::ReduceCards, 1, TEXT("背包缩至两张后选中尾槽")},
			{EAction::Down, 1, TEXT("未满背包的最后一张向下不进入空槽或跳到首槽")},
			{EAction::Up, 0, TEXT("未满背包向上仍可选中相邻卡牌")},
			{EAction::Up, 0, TEXT("未满背包首槽向上仍停在首槽")},
			{EAction::Down, 1, TEXT("未满背包向下可回到最后一张")},
			{EAction::ReduceCards, 0, TEXT("缩至单张后选中首槽")},
			{EAction::Up, 0, TEXT("仅一张牌时向上不改变选择")},
			{EAction::Down, 0, TEXT("仅一张牌时向下不改变选择")},
			{EAction::ReduceCards, INDEX_NONE, TEXT("空包没有选中槽位")},
			{EAction::Up, INDEX_NONE, TEXT("空包滚动不会伪造选中槽位")},
			{EAction::RestoreCards, 0, TEXT("还原测试库存")},
			{EAction::OpenShop, 0, TEXT("实际商店接管游戏输入")},
			{EAction::Down, 0, TEXT("商店开启时滚轮不移动建造栏选中项")},
			{EAction::CloseShop, 0, TEXT("关闭商店不修改选中项")},
			{EAction::Down, 1, TEXT("关闭商店后滚轮恢复切换")},
			{EAction::One, 0, TEXT("数字键 1 仍直选首槽")}
		};

		/** 通过正式 WBP 同时检查选中金环、独立功能图标和数字等级。 */
		bool CheckPresentation(UWorld& World, const TArray<FCastleBuildingDescriptor>& Cards, const FStep& Step) const
		{
			TArray<UUserWidget*> Bars;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&World, Bars, UCastleBuildBarWidget::StaticClass(), true);
			if (!Test.TestEqual(TEXT("唯一的运行时建造栏"), Bars.Num(), 1)) { return false; }
			// 与贴花交付表独立核对，不复用被测生产映射，防止两端同时发生同一个编号错误。
			const TMap<ECastleBuildingType, FString> ExpectedTextures = {
				{ECastleBuildingType::NormalRoom, TEXT("T_BuildBarIcon_0_NormalRoom")},
				{ECastleBuildingType::Farm, TEXT("T_BuildBarIcon_1_Farm")},
				{ECastleBuildingType::CannonTower, TEXT("T_BuildBarIcon_2_CannonTower")},
				{ECastleBuildingType::ArcherTower, TEXT("T_BuildBarIcon_3_ArcherTower")},
				{ECastleBuildingType::ShieldTower, TEXT("T_BuildBarIcon_4_ShieldTower")},
				{ECastleBuildingType::BalloonTower, TEXT("T_BuildBarIcon_5_BalloonTower")},
				{ECastleBuildingType::BeeHive, TEXT("T_BuildBarIcon_6_BeeHive")}
			};
			for (int32 Index = 0; Index < 10; ++Index)
			{
				const UUserWidget* Slot = Cast<UUserWidget>(Bars[0]->GetWidgetFromName(FName(*FString::Printf(TEXT("Slot%02d"), Index + 1))));
				const UWidget* Ring = IsValid(Slot) ? Slot->GetWidgetFromName(TEXT("SelectedCircle")) : nullptr;
				if (!Test.TestNotNull(TEXT("槽位拥有选中态图片"), Ring)
					|| !Test.TestEqual(FString::Printf(TEXT("%s时第 %d 槽金环"), Step.Description, Index + 1), Ring->IsVisible(), Index == Step.ExpectedSlot))
				{
					return false;
				}
				const auto* Area = Cast<USizeBox>(Slot->GetWidgetFromName(TEXT("PreviewArea")));
				const auto* Stats = Cast<UTextBlock>(Slot->GetWidgetFromName(TEXT("CardStats")));
				if (!Test.TestNotNull(TEXT("正式槽位已绑定图标区"), Area)
					|| !Test.TestNotNull(TEXT("正式槽位已绑定卡牌数字和等级"), Stats)) { return false; }
				const auto* Icon = Cast<UImage>(Area->GetContent());
				if (!Cards.IsValidIndex(Index))
				{
					Test.TestTrue(TEXT("空槽隐藏图标区与文字并释放旧图片"), !Area->IsVisible() && !Stats->IsVisible()
						&& (!Icon || !Icon->GetBrush().GetResourceObject()));
					continue;
				}
				if (!Test.TestNotNull(TEXT("有牌的槽位使用 UImage"), Icon)) { return false; }
				const FCastleBuildingDescriptor& Card = Cards[Index];
				const FString* ExpectedName = ExpectedTextures.Find(Card.GetBuildingType());
				if (!Test.TestNotNull(TEXT("当前牌类别有已声明的独立图标"), ExpectedName)) { return false; }
				Test.TestEqual(TEXT("实际刷子纹理匹配卡牌类型"), GetNameSafe(Icon->GetBrush().GetResourceObject()), *ExpectedName);
				FLinearColor Tint;
				Test.TestTrue(TEXT("图标使用真实卡牌色"), CastleTileColors::TryGetGridTint(Card.Color, Tint)
					&& Icon->GetColorAndOpacity().Equals(Tint, 0.001f));
				Test.TestTrue(TEXT("数字与等级保持可见且与卡牌一致"), Area->IsVisible() && Stats->IsVisible()
					&& Stats->GetText().EqualTo(FText::Format(NSLOCTEXT("CastleBuildBar", "CardStats", "{0} · Lv{1}"), Card.Number, Card.Level)));
			}
			return true;
		}

		/** 测试移牌时逆序保存，恢复时按原始顺序追加；不保存运行时测试世界。 */
		bool RestoreCards(UCastleBackpackComponent& Backpack)
		{
			while (!RemovedCards.IsEmpty())
			{
				int32 RestoredIndex;
				FText Reason;
				if (!Backpack.TryAddItem(RemovedCards.Last(), RestoredIndex, Reason))
				{
					Test.AddError(Reason.ToString());
					return false;
				}
				RemovedCards.Pop(EAllowShrinking::No);
			}
			return true;
		}

		void Finish(UCastleBackpackComponent& Backpack, UCastleShopComponent& Shop)
		{
			if (Shop.IsShopOpen()) { Shop.CloseShop(); }
			RestoreCards(Backpack);
		}

		FAutomationTestBase& Test;
		TArray<FCastleBuildingDescriptor> RemovedCards;
		ECastleControlMode ExpectedMode = ECastleControlMode::Classic;
		int32 NextStep = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildBarWheelTest, "MovingCastle.BuildBar.WheelSelection",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildBarWheelTest::RunTest(const FString& Parameters)
{
	// 测试要求正式主关卡的十张开局牌，每次运行重载关卡，避免复用上一次运行的库存或商店。
	if (!AutomationOpenMap(TEXT("/Game/MovingCastle/Maps/MainMap"), true)) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FCheckBuildBarWheel(*this));
	return true;
}

#endif
