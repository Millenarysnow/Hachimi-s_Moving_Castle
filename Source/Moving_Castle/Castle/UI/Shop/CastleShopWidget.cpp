// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleShopWidget.h"

#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/UI/Cards/CastleCardPortraitSubsystem.h"
#include "Castle/UI/Cards/CastleShopCardWidget.h"
#include "Castle/UI/Common/CastleHUDControls.h"
#include "Castle/UI/Shop/CastleShopBenchSlotWidget.h"
#include "Castle/UI/Shop/CastleShopCardDetailWidget.h"
#include "Castle/UI/Shop/CastleShopOfferSlotWidget.h"
#include "Castle/UI/Shop/CastleShopSellDragDropOperation.h"
#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingDefinition.h"
#include "CastleEconomyComponent.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "InputCoreTypes.h"
#include "Moving_Castle.h"

namespace
{
	/** 操作反馈 Toast 的显示与尾段淡出时长（秒）。 */
	constexpr float ShopToastDisplaySeconds = 3.2f;
	constexpr float ShopToastFadeSeconds = 0.45f;

	/** 统计背包中与指定一级建筑同定义、同色、同号的卡牌张数，仅供只读合成提示使用。 */
	int32 CountMatchingLv1CardsInBackpack(const TArray<FCastleBuildingDescriptor>& BackpackItems,
		const FCastleBuildingDescriptor& Target)
	{
		int32 Count = 0;
		for (const FCastleBuildingDescriptor& Item : BackpackItems)
		{
			if (Item.Definition == Target.Definition && Item.Level == 1
				&& Item.Color == Target.Color && Item.Number == Target.Number)
			{
				++Count;
			}
		}
		return Count;
	}

	/** 校验按钮两态切换器（常态/按下）均绑定了非零尺寸有效贴图。 */
	bool ValidateTwoStateButtonBg(const UWidgetSwitcher* Switcher, const TCHAR* SwitcherName, const UClass* OwnerClass)
	{
		if (!IsValid(Switcher) || Switcher->GetNumWidgets() != 2)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店 WBP %s 的 %s 必须包含 2 态贴图（常态/按下），实际 %d。"),
				*GetPathNameSafe(OwnerClass), SwitcherName, IsValid(Switcher) ? Switcher->GetNumWidgets() : 0);
			return false;
		}
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const UImage* StateImage = Cast<UImage>(Switcher->GetWidgetAtIndex(Index));
			if (!CastleHUDControls::HasValidTextureBrush(StateImage))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("商店 WBP %s 的 %s[%d] 贴图资源或绘制尺寸无效。"),
					*GetPathNameSafe(OwnerClass), SwitcherName, Index);
				return false;
			}
		}
		return true;
	}
}

TStaticArray<UCastleShopOfferSlotWidget*, UCastleShopWidget::FixedOfferCount> UCastleShopWidget::GetOfferSlots() const
{
	return {OfferSlot01, OfferSlot02, OfferSlot03, OfferSlot04, OfferSlot05, OfferSlot06};
}

TStaticArray<UCastleShopBenchSlotWidget*, UCastleShopWidget::FixedBenchCount> UCastleShopWidget::GetBenchSlots() const
{
	return {
		BenchSlot01, BenchSlot02, BenchSlot03, BenchSlot04, BenchSlot05,
		BenchSlot06, BenchSlot07, BenchSlot08, BenchSlot09, BenchSlot10
	};
}

TArray<UCastleShopCardWidget*> UCastleShopWidget::GetOfferCards() const
{
	TArray<UCastleShopCardWidget*> Cards;
	Cards.Reserve(FixedOfferCount);
	const auto Slots = GetOfferSlots();
	for (UCastleShopOfferSlotWidget* SlotWidget : Slots)
	{
		if (IsValid(SlotWidget) && IsValid(SlotWidget->GetCardWidget()))
		{
			Cards.Add(SlotWidget->GetCardWidget());
		}
	}
	return Cards;
}

bool UCastleShopWidget::IsSellDropOverlayVisible() const
{
	return IsValid(SellDropOverlay) && SellDropOverlay->GetVisibility() != ESlateVisibility::Collapsed;
}

bool UCastleShopWidget::ValidatePresentation() const
{
	const struct { const TCHAR* Name; const UImage* Image; } RequiredImages[] = {
		{TEXT("TopBarBg"), TopBarBg},
		{TEXT("TopCoinIcon"), TopCoinIcon},
		{TEXT("TopTowerIcon"), TopTowerIcon},
		{TEXT("PeekIcon"), PeekIcon},
		{TEXT("TopCloseIcon"), TopCloseIcon},
		{TEXT("BenchBg"), BenchBg},
		{TEXT("DockBg"), DockBg},
		{TEXT("LeftTowerIcon"), LeftTowerIcon},
		{TEXT("RefreshIcon"), RefreshIcon},
		{TEXT("SellDropBg"), SellDropBg},
		{TEXT("SellDropIcon"), SellDropIcon},
		{TEXT("RightSellIcon"), RightSellIcon}
	};
	for (const auto& Entry : RequiredImages)
	{
		if (!CastleHUDControls::HasValidTextureBrush(Entry.Image))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店 WBP %s 缺少贴图控件 %s 或其切图/绘制尺寸无效。"),
				*GetClass()->GetPathName(), Entry.Name);
			return false;
		}
	}

	if (!IsValid(GoldValueText) || !IsValid(TowerRoomSummaryText) || !IsValid(WavePhaseText)
		|| !IsValid(FoilPreview) || !IsValid(FoilLabel) || !IsValid(PeekButton) || !IsValid(TopCloseButton)
		|| !IsValid(CardDetailPanel) || !CardDetailPanel->ValidatePresentation()
		|| !IsValid(ToastContainer) || !IsValid(StatusText)
		|| !IsValid(BenchSection) || !IsValid(BenchHeaderText)
		|| !IsValid(DockSection) || !IsValid(TowerLevelText) || !IsValid(TowerExpBar) || !IsValid(RoomLimitText)
		|| !IsValid(RefreshButton) || !IsValid(RefreshLabel) || !IsValid(RefreshCostText)
		|| !IsValid(SellDropOverlay) || !IsValid(SellDropTitleText) || !IsValid(SellDropPriceText)
		|| !IsValid(CloseButton) || !IsValid(CloseLabel)
		|| !IsValid(RightSellTarget) || !IsValid(RightSellText))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店 WBP %s 缺少必需控件绑定或详情浮卡校验失败。"), *GetClass()->GetPathName());
		return false;
	}

	if (!ValidateTwoStateButtonBg(RefreshBgStates, TEXT("RefreshBgStates"), GetClass())
		|| !ValidateTwoStateButtonBg(CloseBgStates, TEXT("CloseBgStates"), GetClass()))
	{
		return false;
	}

	const auto OfferSlots = GetOfferSlots();
	for (int32 Index = 0; Index < OfferSlots.Num(); ++Index)
	{
		if (!IsValid(OfferSlots[Index]) || !OfferSlots[Index]->ValidatePresentation())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店 WBP %s 的 OfferSlot%02d 缺失或表现校验失败。"),
				*GetClass()->GetPathName(), Index + 1);
			return false;
		}
	}

	const auto BenchSlots = GetBenchSlots();
	for (int32 Index = 0; Index < BenchSlots.Num(); ++Index)
	{
		if (!IsValid(BenchSlots[Index]) || !BenchSlots[Index]->ValidatePresentation())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店 WBP %s 的 BenchSlot%02d 缺失或表现校验失败。"),
				*GetClass()->GetPathName(), Index + 1);
			return false;
		}
	}

	return true;
}

void UCastleShopWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 顶层商店面板持有键盘焦点，以承接 1~6 / Shift+1~0 / R / B 快捷键。
	SetIsFocusable(true);
	bPresentationValid = ValidatePresentation();
	if (!bPresentationValid)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	if (!bControlsBound)
	{
		RefreshButton->OnClicked.AddUniqueDynamic(this, &UCastleShopWidget::HandleRefresh);
		RefreshButton->OnPressed.AddUniqueDynamic(this, &UCastleShopWidget::HandleRefreshPressed);
		RefreshButton->OnReleased.AddUniqueDynamic(this, &UCastleShopWidget::HandleRefreshReleased);

		CloseButton->OnClicked.AddUniqueDynamic(this, &UCastleShopWidget::HandleClose);
		CloseButton->OnPressed.AddUniqueDynamic(this, &UCastleShopWidget::HandleClosePressed);
		CloseButton->OnReleased.AddUniqueDynamic(this, &UCastleShopWidget::HandleCloseReleased);

		TopCloseButton->OnClicked.AddUniqueDynamic(this, &UCastleShopWidget::HandleClose);
		FoilPreview->OnClicked.AddUniqueDynamic(this, &UCastleShopWidget::HandleToggleFoil);

		PeekButton->OnPressed.AddUniqueDynamic(this, &UCastleShopWidget::HandlePeekPressed);
		PeekButton->OnReleased.AddUniqueDynamic(this, &UCastleShopWidget::HandlePeekReleased);

		const auto OfferSlots = GetOfferSlots();
		for (UCastleShopOfferSlotWidget* OfferSlot : OfferSlots)
		{
			OfferSlot->OnOfferPurchaseRequested.AddUObject(this, &UCastleShopWidget::HandleOfferPurchaseRequested);
			OfferSlot->OnOfferHovered.AddUObject(this, &UCastleShopWidget::HandleOfferHovered);
		}

		const auto BenchSlots = GetBenchSlots();
		for (UCastleShopBenchSlotWidget* BenchSlot : BenchSlots)
		{
			BenchSlot->OnBenchSellRequested.AddUObject(this, &UCastleShopWidget::HandleBenchSellRequested);
			BenchSlot->OnBenchHovered.AddUObject(this, &UCastleShopWidget::HandleBenchHovered);
			BenchSlot->OnBenchDragStarted.AddUObject(this, &UCastleShopWidget::HandleBenchDragStarted);
			BenchSlot->OnBenchDragEnded.AddUObject(this, &UCastleShopWidget::HandleBenchDragEnded);
		}

		bControlsBound = true;
	}

	FoilLabel->SetText(NSLOCTEXT("CastleShop", "FoilOff", "箔光：关"));
	CloseLabel->SetText(NSLOCTEXT("CastleShop", "CloseDock", "继续建造 [B]"));
	RightSellText->SetText(NSLOCTEXT("CastleShop", "SellTargetHint", "拖入出售"));
	RefreshBgStates->SetActiveWidgetIndex(0);
	CloseBgStates->SetActiveWidgetIndex(0);
	SellDropOverlay->SetVisibility(ESlateVisibility::Collapsed);
	CardDetailPanel->HideDetail();
	ToastContainer->SetVisibility(ESlateVisibility::Collapsed);
	SetVisibility(ESlateVisibility::Collapsed);
}

void UCastleShopWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!bPresentationValid)
	{
		return;
	}

	ACastlePlayerController* Controller = Cast<ACastlePlayerController>(GetOwningPlayer());
	if (!IsValid(Controller) || !IsValid(Controller->GetShop()) || !IsValid(Controller->GetEconomy()))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店界面 %s 绑定失败：拥有者 %s 缺少城堡商店或经济组件。"),
			*GetClass()->GetPathName(), *GetPathNameSafe(GetOwningPlayer()));
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	Shop = Controller->GetShop();
	Controller->RegisterShopFocusWidget(*this);
	Economy = Controller->GetEconomy();
	Portraits = GetWorld() ? GetWorld()->GetSubsystem<UCastleCardPortraitSubsystem>() : nullptr;
	Shop->OnShopChanged.AddUniqueDynamic(this, &UCastleShopWidget::RefreshShop);
	Economy->OnGoldChanged.AddUniqueDynamic(this, &UCastleShopWidget::HandleGoldChanged);
	RefreshShop();
}

void UCastleShopWidget::NativeDestruct()
{
	if (auto* Controller = GetOwningPlayer<ACastlePlayerController>()) { Controller->UnregisterShopFocusWidget(*this); }
	if (IsValid(Shop))
	{
		Shop->OnShopChanged.RemoveDynamic(this, &UCastleShopWidget::RefreshShop);
	}
	if (IsValid(Economy))
	{
		Economy->OnGoldChanged.RemoveDynamic(this, &UCastleShopWidget::HandleGoldChanged);
	}
	CancelOfferInspection();
	if (IsValid(Portraits))
	{
		Portraits->SetShopVisible(this, false);
	}
	Super::NativeDestruct();
}

void UCastleShopWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (ToastRemainingSeconds > 0.0f && IsValid(ToastContainer))
	{
		ToastRemainingSeconds = FMath::Max(0.0f, ToastRemainingSeconds - InDeltaTime);
		if (ToastRemainingSeconds <= 0.0f)
		{
			ToastContainer->SetVisibility(ESlateVisibility::Collapsed);
			ToastContainer->SetRenderOpacity(1.0f);
		}
		else if (ToastRemainingSeconds < ShopToastFadeSeconds)
		{
			ToastContainer->SetRenderOpacity(ToastRemainingSeconds / ShopToastFadeSeconds);
		}
	}
}

void UCastleShopWidget::HandleGoldChanged(const int32 PreviousGold, const int32 CurrentGold)
{
	RefreshShop();
}

void UCastleShopWidget::RefreshShop()
{
	if (!bPresentationValid || !IsValid(Shop) || !IsValid(Economy))
	{
		return;
	}

	const bool bWasVisible = GetVisibility() == ESlateVisibility::Visible;
	const bool bIsOpen = Shop->IsShopOpen();

	// 换牌或关闭前先归还旧卡捕获与拖拽遮罩，防止悬空鼠标路由吞掉后续事件。
	CancelOfferInspection();
	if (bDraggingBenchSell)
	{
		HandleBenchDragEnded();
	}

	SetVisibility(bIsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (IsValid(Portraits))
	{
		Portraits->SetShopVisible(this, bIsOpen);
	}
	if (!bIsOpen)
	{
		bPeekingCastle = false;
		HoveredOfferIndex = INDEX_NONE;
		HoveredBenchIndex = INDEX_NONE;
		CardDetailPanel->HideDetail();
		ToastRemainingSeconds = 0.0f;
		ToastContainer->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (!bWasVisible)
	{
		bPeekingCastle = false;
		ApplyPeekVisibility();
		// 首次打开时将键盘焦点交给商店，关闭时由 ACastlePlayerController 恢复视口输入。
		SetKeyboardFocus();
	}

	const int32 CurrentGold = Economy->GetGold();
	const int32 OwnedRooms = Shop->GetOwnedRoomCount();
	const int32 RoomLimit = Shop->RoomLimit;
	const TArray<FCastleBuildingDescriptor> BackpackItems = Shop->GetBackpackItems();
	const ACastleBoard* BoundBoard = Shop->GetBoundBoard();
	const int32 BackpackCapacity = (IsValid(BoundBoard) && IsValid(BoundBoard->PrototypeBackpack))
		? BoundBoard->PrototypeBackpack->Capacity : FixedBenchCount;

	GoldValueText->SetText(FText::AsNumber(CurrentGold));
	RefreshTowerAndWaveHeader();

	// 刷新按钮文案与余额预警着色。
	const bool bFreeRefresh = Shop->RefreshCost == 0;
	const bool bCanAffordRefresh = CurrentGold >= Shop->RefreshCost;
	RefreshLabel->SetText(NSLOCTEXT("CastleShop", "RefreshBtnTitle", "刷新商店 [R]"));
	RefreshCostText->SetText(bFreeRefresh
		? NSLOCTEXT("CastleShop", "RefreshFree", "免费")
		: FText::Format(NSLOCTEXT("CastleShop", "RefreshPaid", "{0} 金币"), FText::AsNumber(Shop->RefreshCost)));
	RefreshCostText->SetColorAndOpacity(FSlateColor(bCanAffordRefresh
		? FLinearColor(0.99f, 0.89f, 0.56f, 1.0f)
		: FLinearColor(0.96f, 0.38f, 0.34f, 1.0f)));
	RefreshBgStates->SetActiveWidgetIndex(0);
	CloseBgStates->SetActiveWidgetIndex(0);

	// 仅教学货架支持空槽，保留常规商店六个商品的配置约束。
	const TArray<FCastleShopOffer> Offers = Shop->GetOffers();
	const auto OfferSlots = GetOfferSlots();
	if (Offers.Num() > OfferSlots.Num() || (!Shop->UsesScriptedOffers() && Offers.Num() != OfferSlots.Num()))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店商品数量 %d 与 WBP 固定槽位数 %d 不一致。"),
			Offers.Num(), OfferSlots.Num());
		return;
	}

	const bool bHasRoomAndBagCapacity = (OwnedRooms + 1 <= RoomLimit) && (BackpackItems.Num() < BackpackCapacity);
	for (int32 Index = 0; Index < OfferSlots.Num(); ++Index)
	{
		const bool bHasOffer = Offers.IsValidIndex(Index);
		OfferSlots[Index]->SetVisibility(bHasOffer ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		if (!bHasOffer) { continue; }
		const FCastleShopOffer& Offer = Offers[Index];
		const bool bCanAfford = CurrentGold >= Offer.Price;
		const int32 MatchingCount = CountMatchingLv1CardsInBackpack(BackpackItems, Offer.Descriptor);
		OfferSlots[Index]->ConfigureOffer(*this, Index, Offer, BoundBoard,
			bCanAfford, bHasRoomAndBagCapacity, bFoilPreview, MatchingCount);
	}
	if (Shop->UsesScriptedOffers() && Offers.IsEmpty())
	{
		RefreshLabel->SetText(INVTEXT("暂时缺货 · 刷新 [R]"));
	}

	// 同步 10 格备战席。
	BenchHeaderText->SetText(FText::Format(
		NSLOCTEXT("CastleShop", "BenchHeader", "备战席  {0} / {1}     ·     左键拖入下方回收区或右键出售  ·  快捷键 Shift+1~0"),
		FText::AsNumber(BackpackItems.Num()), FText::AsNumber(BackpackCapacity)));

	const auto BenchSlots = GetBenchSlots();
	for (int32 Index = 0; Index < BenchSlots.Num(); ++Index)
	{
		const FCastleBuildingDescriptor* Card = BackpackItems.IsValidIndex(Index) ? &BackpackItems[Index] : nullptr;
		int32 SellPrice = 0;
		FText QuoteReason;
		const bool bCanQuote = (Card != nullptr) && Shop->TryGetSellPrice(Index, SellPrice, QuoteReason);
		BenchSlots[Index]->ConfigureBenchSlot(Index, Card, bCanQuote, SellPrice, QuoteReason);
	}

	RefreshHoverDetail();
}

void UCastleShopWidget::RefreshTowerAndWaveHeader()
{
	const int32 OwnedRooms = Shop->GetOwnedRoomCount();
	const int32 RoomLimit = Shop->RoomLimit;
	const bool bNearLimit = OwnedRooms >= RoomLimit;
	const FLinearColor CapacityColor = bNearLimit
		? FLinearColor(0.96f, 0.42f, 0.34f, 1.0f)
		: FLinearColor(0.84f, 0.93f, 0.99f, 1.0f);

	RoomLimitText->SetText(FText::Format(NSLOCTEXT("CastleShop", "RoomLimitFormat", "房间总量  {0} / {1}"),
		FText::AsNumber(OwnedRooms), FText::AsNumber(RoomLimit)));
	RoomLimitText->SetColorAndOpacity(FSlateColor(CapacityColor));

	const UCastleDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
	if (IsValid(Director) && Director->IsRunning() && IsValid(Director->GetConfig()))
	{
		const int32 TowerLv = Director->GetLevel();
		const int32 Exp = Director->GetExperience();
		const int32 ExpPerLevel = FMath::Max(1, Director->GetConfig()->ExpPerLevel);
		const int32 NextStep = Director->GetNextStep();

		TowerLevelText->SetText(FText::Format(NSLOCTEXT("CastleShop", "TowerLvExp", "主塔 Lv.{0}   ({1}/{2})"),
			FText::AsNumber(TowerLv), FText::AsNumber(Exp), FText::AsNumber(ExpPerLevel)));
		TowerExpBar->SetPercent(FMath::Clamp(static_cast<float>(Exp) / static_cast<float>(ExpPerLevel), 0.0f, 1.0f));
		TowerExpBar->SetVisibility(ESlateVisibility::HitTestInvisible);

		TowerRoomSummaryText->SetText(FText::Format(
			NSLOCTEXT("CastleShop", "TopTowerSummary", "主塔 Lv.{0}   ·   房间 {1} / {2}"),
			FText::AsNumber(TowerLv), FText::AsNumber(OwnedRooms), FText::AsNumber(RoomLimit)));
		WavePhaseText->SetText(FText::Format(
			NSLOCTEXT("CastleShop", "WaveHeader", "第 {0} 关休整  ·  房间商店"),
			FText::AsNumber(NextStep)));
	}
	else
	{
		// 非导演运行的测试关卡不伪造主塔等级与经验条，如实展示自由测试状态与房间上限。
		TowerLevelText->SetText(NSLOCTEXT("CastleShop", "SandboxTowerLabel", "主塔容量"));
		TowerExpBar->SetVisibility(ESlateVisibility::Collapsed);
		TowerRoomSummaryText->SetText(FText::Format(
			NSLOCTEXT("CastleShop", "TopRoomOnlySummary", "房间 {0} / {1}"),
			FText::AsNumber(OwnedRooms), FText::AsNumber(RoomLimit)));
		WavePhaseText->SetText(NSLOCTEXT("CastleShop", "SandboxShopHeader", "房间商店"));
	}
}

void UCastleShopWidget::RefreshHoverDetail()
{
	if (!IsValid(Shop) || !Shop->IsShopOpen() || bPeekingCastle || bDraggingBenchSell)
	{
		CardDetailPanel->HideDetail();
		return;
	}

	const TArray<FCastleBuildingDescriptor> BackpackItems = Shop->GetBackpackItems();
	if (HoveredOfferIndex != INDEX_NONE)
	{
		const TArray<FCastleShopOffer> Offers = Shop->GetOffers();
		if (Offers.IsValidIndex(HoveredOfferIndex))
		{
			const FCastleShopOffer& Offer = Offers[HoveredOfferIndex];
			const int32 MatchingCount = CountMatchingLv1CardsInBackpack(BackpackItems, Offer.Descriptor);
			CardDetailPanel->ShowCardDetail(Offer.Descriptor, false, Offer.Price, MatchingCount);
			return;
		}
	}
	if (HoveredBenchIndex != INDEX_NONE && BackpackItems.IsValidIndex(HoveredBenchIndex))
	{
		int32 SellPrice = 0;
		FText Reason;
		if (Shop->TryGetSellPrice(HoveredBenchIndex, SellPrice, Reason))
		{
			CardDetailPanel->ShowCardDetail(BackpackItems[HoveredBenchIndex], true, SellPrice, 0);
			return;
		}
	}
	CardDetailPanel->HideDetail();
}

void UCastleShopWidget::ApplyPeekVisibility()
{
	const ESlateVisibility ConsoleVis = bPeekingCastle ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible;
	BenchSection->SetVisibility(ConsoleVis);
	DockSection->SetVisibility(ConsoleVis);
	if (bPeekingCastle)
	{
		CardDetailPanel->HideDetail();
		ToastContainer->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		RefreshHoverDetail();
	}
}

void UCastleShopWidget::ActivateCardInspection(UCastleShopCardWidget& Card)
{
	if (InspectedCard.Get() != &Card)
	{
		CancelOfferInspection();
		InspectedCard = &Card;
	}
}

void UCastleShopWidget::ReleaseCardInspection(const UCastleShopCardWidget& Card)
{
	if (InspectedCard.Get() == &Card)
	{
		InspectedCard.Reset();
	}
}

void UCastleShopWidget::CancelOfferInspection()
{
	if (UCastleShopCardWidget* Card = InspectedCard.Get())
	{
		Card->CancelInspection();
	}
	InspectedCard.Reset();
}

void UCastleShopWidget::HandleToggleFoil()
{
	bFoilPreview = !bFoilPreview;
	FoilLabel->SetText(bFoilPreview
		? NSLOCTEXT("CastleShop", "FoilOn", "箔光：开")
		: NSLOCTEXT("CastleShop", "FoilOff", "箔光：关"));
	const auto OfferSlots = GetOfferSlots();
	for (UCastleShopOfferSlotWidget* OfferSlot : OfferSlots)
	{
		if (IsValid(OfferSlot))
		{
			OfferSlot->SetFoilPreview(bFoilPreview);
		}
	}
}

void UCastleShopWidget::HandlePeekPressed()
{
	bPeekingCastle = true;
	CancelOfferInspection();
	ApplyPeekVisibility();
}

void UCastleShopWidget::HandlePeekReleased()
{
	bPeekingCastle = false;
	ApplyPeekVisibility();
}

void UCastleShopWidget::HandleOfferPurchaseRequested(const int32 OfferIndex)
{
	HandleTrade(false, OfferIndex);
}

void UCastleShopWidget::HandleOfferHovered(const int32 OfferIndex, const bool bHovered)
{
	if (bHovered)
	{
		HoveredOfferIndex = OfferIndex;
		HoveredBenchIndex = INDEX_NONE;
	}
	else if (HoveredOfferIndex == OfferIndex)
	{
		HoveredOfferIndex = INDEX_NONE;
	}
	RefreshHoverDetail();
}

void UCastleShopWidget::HandleBenchSellRequested(const int32 BackpackSlotIndex)
{
	HandleTrade(true, BackpackSlotIndex);
}

void UCastleShopWidget::HandleBenchHovered(const int32 BackpackSlotIndex, const bool bHovered)
{
	if (bHovered)
	{
		HoveredBenchIndex = BackpackSlotIndex;
		HoveredOfferIndex = INDEX_NONE;
	}
	else if (HoveredBenchIndex == BackpackSlotIndex)
	{
		HoveredBenchIndex = INDEX_NONE;
	}
	RefreshHoverDetail();
}

void UCastleShopWidget::HandleBenchDragStarted(const int32 BackpackSlotIndex,
	const FCastleBuildingDescriptor& Descriptor, const int32 SellPrice)
{
	CancelOfferInspection();
	bDraggingBenchSell = true;
	CardDetailPanel->HideDetail();
	const FString CardName = IsValid(Descriptor.Definition)
		? Descriptor.Definition->DisplayName.ToString()
		: TEXT("建筑卡牌");
	SellDropTitleText->SetText(FText::FromString(FString::Printf(TEXT("松手出售 · %s %d"), *CardName, Descriptor.Number)));
	SellDropPriceText->SetText(FText::Format(NSLOCTEXT("CastleShop", "SellDropGain", "+{0} 金币"),
		FText::AsNumber(SellPrice)));
	SellDropOverlay->SetVisibility(ESlateVisibility::Visible);
	RightSellText->SetText(FText::Format(NSLOCTEXT("CastleShop", "SellTargetActive", "松手 +{0}"),
		FText::AsNumber(SellPrice)));
}

void UCastleShopWidget::HandleBenchDragEnded()
{
	bDraggingBenchSell = false;
	if (IsValid(SellDropOverlay))
	{
		SellDropOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IsValid(RightSellText))
	{
		RightSellText->SetText(NSLOCTEXT("CastleShop", "SellTargetHint", "拖入出售"));
	}
}

bool UCastleShopWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	UCastleShopSellDragDropOperation* SellOp = Cast<UCastleShopSellDragDropOperation>(InOperation);
	if (!IsValid(SellOp))
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}

	const FVector2D ScreenPos = InDragDropEvent.GetScreenSpacePosition();
	const bool bOverOfferSellZone = IsValid(SellDropOverlay)
		&& SellDropOverlay->GetCachedGeometry().IsUnderLocation(ScreenPos);
	const bool bOverRightSellTarget = IsValid(RightSellTarget)
		&& RightSellTarget->GetCachedGeometry().IsUnderLocation(ScreenPos);

	const int32 TargetSlot = SellOp->BackpackSlotIndex;
	HandleBenchDragEnded();

	if (bOverOfferSellZone || bOverRightSellTarget)
	{
		HandleTrade(true, TargetSlot);
		return true;
	}
	return false;
}

void UCastleShopWidget::ShowResult(const bool bSuccess, const FString& SuccessMessage, const FText& FailureReason)
{
	const FString Message = bSuccess ? SuccessMessage : FailureReason.ToString();
	StatusText->SetText(FText::FromString(Message));
	StatusText->SetColorAndOpacity(FSlateColor(bSuccess
		? FLinearColor(0.46f, 0.96f, 0.72f, 1.0f)
		: FLinearColor(1.0f, 0.48f, 0.34f, 1.0f)));
	ToastContainer->SetRenderOpacity(1.0f);
	ToastContainer->SetVisibility(ESlateVisibility::HitTestInvisible);
	ToastRemainingSeconds = ShopToastDisplaySeconds;
	UE_LOG(LogMoving_Castle, Log, TEXT("商店操作：%s"), *Message);
}

void UCastleShopWidget::HandleTrade(const bool bSelling, const int32 SlotIndex)
{
	if (!IsValid(Shop))
	{
		return;
	}
	FText Reason;
	const bool bSuccess = bSelling
		? Shop->TrySellBackpackItem(SlotIndex, Reason)
		: Shop->TryBuyOffer(SlotIndex, Reason);
	ShowResult(bSuccess,
		bSelling
			? TEXT("出售成功，金币已入账；备战席已自动前移。")
			: TEXT("购买成功，卡牌已存入备战席。"),
		Reason);
}

void UCastleShopWidget::HandleRefresh()
{
	if (!IsValid(Shop))
	{
		return;
	}
	FText Reason;
	const bool bSuccess = Shop->TryRefreshOffers(Reason);
	ShowResult(bSuccess, FString::Printf(TEXT("刷新成功，已上架 %d 张建筑卡牌。"), Shop->GetOffers().Num()), Reason);
}

void UCastleShopWidget::HandleRefreshPressed()
{
	if (IsValid(RefreshBgStates))
	{
		RefreshBgStates->SetActiveWidgetIndex(1);
	}
}

void UCastleShopWidget::HandleRefreshReleased()
{
	if (IsValid(RefreshBgStates))
	{
		RefreshBgStates->SetActiveWidgetIndex(0);
	}
}

void UCastleShopWidget::HandleClose()
{
	if (IsValid(Shop))
	{
		Shop->CloseShop();
	}
}

void UCastleShopWidget::HandleClosePressed()
{
	if (IsValid(CloseBgStates))
	{
		CloseBgStates->SetActiveWidgetIndex(1);
	}
}

void UCastleShopWidget::HandleCloseReleased()
{
	if (IsValid(CloseBgStates))
	{
		CloseBgStates->SetActiveWidgetIndex(0);
	}
}

FReply UCastleShopWidget::NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsValid(Shop) && Shop->IsShopOpen())
	{
		const FKey Key = InKeyEvent.GetKey();
		if (Key == EKeys::B)
		{
			HandleClose();
			return FReply::Handled();
		}
		if (Key == EKeys::R)
		{
			HandleRefresh();
			return FReply::Handled();
		}
		const FKey SlotKeys[] = {
			EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
			EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(SlotKeys); ++Index)
		{
			if (Key == SlotKeys[Index] && (InKeyEvent.IsShiftDown() || Index < FixedOfferCount))
			{
				HandleTrade(InKeyEvent.IsShiftDown(), Index);
				return FReply::Handled();
			}
		}
	}
	return Super::NativeOnKeyDown(MyGeometry, InKeyEvent);
}
