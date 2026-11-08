// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleShopWidget.h"

#include "Blueprint/WidgetTree.h"
#include "CastleBuildingCardWidget.h"
#include "CastleEconomyComponent.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Moving_Castle.h"
#include "InputCoreTypes.h"

namespace
{
	/** 统一使用较小字体，让六张卡和交易提示在同一个测试面板中完整显示。 */
	UTextBlock* MakeShopText(UWidgetTree* Tree, const TCHAR* Name, const FString& Text, int32 FontSize)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		Label->SetText(FText::FromString(Text));
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.94f, 1.0f)));
		return Label;
	}

	/** 统一纵向间距，按钮行和状态文字不会挤到卡片预览上。 */
	void AddShopRow(UVerticalBox* Column, UWidget* Child, float BottomPadding)
	{
		UVerticalBoxSlot* Slot = Column->AddChildToVerticalBox(Child);
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BottomPadding));
	}
}

UCastleShopButton::UCastleShopButton()
{
	InitIsFocusable(false);
}

void UCastleShopActionWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店交易按钮初始化失败：WidgetTree 为空。"));
		return;
	}
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(164.0f);
	Size->SetHeightOverride(52.0f);
	WidgetTree->RootWidget = Size;
	Button = WidgetTree->ConstructWidget<UCastleShopButton>();
	Size->AddChild(Button);
	LabelText = MakeShopText(WidgetTree, TEXT("TradeLabel"), TEXT("购买"), 14);
	Button->AddChild(LabelText);
	Button->OnClicked.AddDynamic(this, &UCastleShopActionWidget::HandleClicked);
}

void UCastleShopActionWidget::Configure(bool bInSelling, int32 InIndex, const FText& Label, bool bEnabled)
{
	bSelling = bInSelling;
	Index = InIndex;
	if (Button != nullptr && LabelText != nullptr)
	{
		LabelText->SetText(Label);
		Button->SetIsEnabled(bEnabled);
		Button->SetBackgroundColor(bSelling ? FLinearColor(0.18f, 0.32f, 0.30f) : FLinearColor(0.25f, 0.34f, 0.48f));
	}
}

void UCastleShopActionWidget::HandleClicked()
{
	OnTradeRequested.Broadcast(bSelling, Index);
}

void UCastleShopWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (bWidgetTreeBuilt)
	{
		return;
	}
	SetIsFocusable(true);
	if (WidgetTree == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店界面初始化失败：WidgetTree 为空。"));
		return;
	}
	// 全屏背景阻挡世界点击；内容在 ScaleBox 中保持比例，窗口较小时整体缩小。
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>();
	Backdrop->SetBrushColor(FLinearColor(0.002f, 0.006f, 0.012f, 0.94f));
	UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop);
	BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
	BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>();
	Scale->SetStretch(EStretch::ScaleToFit);
	UOverlaySlot* ScaleSlot = Root->AddChildToOverlay(Scale);
	ScaleSlot->SetHorizontalAlignment(HAlign_Fill);
	ScaleSlot->SetVerticalAlignment(VAlign_Fill);
	ScaleSlot->SetPadding(FMargin(24.0f));
	USizeBox* PanelSize = WidgetTree->ConstructWidget<USizeBox>();
	PanelSize->SetWidthOverride(1080.0f);
	PanelSize->SetHeightOverride(700.0f);
	Scale->AddChild(PanelSize);
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetPadding(FMargin(24.0f, 18.0f));
	Panel->SetBrushColor(FLinearColor(0.025f, 0.045f, 0.075f, 1.0f));
	PanelSize->AddChild(Panel);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Column);
	AddShopRow(Column, MakeShopText(WidgetTree, TEXT("ShopTitle"), TEXT("房间商店"), 28), 6.0f);
	SummaryText = MakeShopText(WidgetTree, TEXT("ShopSummary"), TEXT("金币 0"), 19);
	SummaryText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.78f, 0.24f)));
	AddShopRow(Column, SummaryText, 8.0f);
	AddShopRow(Column, MakeShopText(WidgetTree, TEXT("ShopHelp"), TEXT("购买 [1-6]   |   出售 [Shift+1-9、0]   |   刷新 [R]   |   关闭 [B]"), 15), 14.0f);
	OfferRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddShopRow(Column, OfferRow, 18.0f);
	AddShopRow(Column, MakeShopText(WidgetTree, TEXT("SellTitle"), TEXT("背包出售  ·  按槽位顺序显示；按钮标明出售所得金币"), 17), 8.0f);
	SellRows = WidgetTree->ConstructWidget<UVerticalBox>();
	AddShopRow(Column, SellRows, 12.0f);
	StatusText = MakeShopText(WidgetTree, TEXT("ShopStatus"), TEXT("首次打开免费出牌，重新开关商店会保留本轮商品。"), 16);
	StatusText->SetWrapTextAt(990.0f);
	AddShopRow(Column, StatusText, 12.0f);
	UHorizontalBox* Controls = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddShopRow(Column, Controls, 0.0f);
	RefreshButton = WidgetTree->ConstructWidget<UCastleShopButton>();
	RefreshLabel = MakeShopText(WidgetTree, TEXT("RefreshLabel"), TEXT("刷新"), 18);
	RefreshButton->AddChild(RefreshLabel);
	RefreshButton->OnClicked.AddDynamic(this, &UCastleShopWidget::HandleRefresh);
	UHorizontalBoxSlot* RefreshSlot = Controls->AddChildToHorizontalBox(RefreshButton);
	RefreshSlot->SetPadding(FMargin(12.0f, 4.0f));
	UButton* CloseButton = WidgetTree->ConstructWidget<UCastleShopButton>();
	CloseButton->AddChild(MakeShopText(WidgetTree, TEXT("CloseLabel"), TEXT("关闭商店 [B]"), 18));
	CloseButton->OnClicked.AddDynamic(this, &UCastleShopWidget::HandleClose);
	Controls->AddChildToHorizontalBox(CloseButton)->SetPadding(FMargin(12.0f, 4.0f));
	bWidgetTreeBuilt = true;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UCastleShopWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ACastlePlayerController* Controller = Cast<ACastlePlayerController>(GetOwningPlayer());
	if (Controller == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店界面绑定失败：拥有者不是 CastlePlayerController。"));
		return;
	}
	Shop = Controller->GetShop();
	Economy = Controller->GetEconomy();
	Shop->OnShopChanged.AddUniqueDynamic(this, &UCastleShopWidget::RefreshShop);
	Economy->OnGoldChanged.AddUniqueDynamic(this, &UCastleShopWidget::HandleGoldChanged);
	RefreshShop();
}

void UCastleShopWidget::NativeDestruct()
{
	if (Shop != nullptr)
	{
		Shop->OnShopChanged.RemoveDynamic(this, &UCastleShopWidget::RefreshShop);
	}
	if (Economy != nullptr)
	{
		Economy->OnGoldChanged.RemoveDynamic(this, &UCastleShopWidget::HandleGoldChanged);
	}
	Super::NativeDestruct();
}

void UCastleShopWidget::HandleGoldChanged(int32 PreviousGold, int32 CurrentGold)
{
	RefreshShop();
}

void UCastleShopWidget::RefreshShop()
{
	if (Shop == nullptr || Economy == nullptr || !bWidgetTreeBuilt)
	{
		return;
	}
	const bool bWasVisible = GetVisibility() == ESlateVisibility::Visible;
	SetVisibility(Shop->IsShopOpen() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!Shop->IsShopOpen())
	{
		return;
	}
	if (!bWasVisible)
	{
		// 首次打开时把键盘焦点交给面板，关闭时由控制器恢复到游戏视口。
		SetKeyboardFocus();
	}
	SummaryText->SetText(FText::FromString(FString::Printf(TEXT("金币 %d     |     已有房间 %d / %d"),
		Economy->GetGold(), Shop->GetOwnedRoomCount(), Shop->RoomLimit)));
	RefreshLabel->SetText(FText::FromString(FString::Printf(TEXT("刷新商品  ·  %d 金币"), Shop->RefreshCost)));
	OfferRow->ClearChildren();
	const TArray<FCastleShopOffer> Offers = Shop->GetOffers();
	for (int32 Index = 0; Index < Offers.Num(); ++Index)
	{
		UVerticalBox* Entry = WidgetTree->ConstructWidget<UVerticalBox>();
		UCastleBuildingCardWidget* Card = CreateWidget<UCastleBuildingCardWidget>(GetOwningPlayer());
		UCastleShopActionWidget* Action = CreateWidget<UCastleShopActionWidget>(GetOwningPlayer());
		if (Card == nullptr || Action == nullptr)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店卡片创建失败，商品槽位 %d。"), Index);
			return;
		}
		// 直接使用已有卡片与线框渲染，只在外侧增加购买标价按钮。
		Card->SetBuildingDescriptor(Offers[Index].Descriptor);
		Card->SetVisibility(ESlateVisibility::HitTestInvisible);
		Card->SetRenderOpacity(Offers[Index].bSold ? 0.35f : 1.0f);
		AddShopRow(Entry, Card, 7.0f);
		Action->Configure(false, Index, FText::FromString(Offers[Index].bSold ? TEXT("已售罄")
			: FString::Printf(TEXT("购买  ·  %d 金币"), Offers[Index].Price)), !Offers[Index].bSold);
		Action->OnTradeRequested.AddDynamic(this, &UCastleShopWidget::HandleTrade);
		AddShopRow(Entry, Action, 0.0f);
		OfferRow->AddChildToHorizontalBox(Entry)->SetPadding(FMargin(3.0f, 0.0f));
	}
	SellRows->ClearChildren();
	const TArray<FCastleBuildingDescriptor> BackpackItems = Shop->GetBackpackItems();
	for (int32 Row = 0; Row < 2; ++Row)
	{
		UHorizontalBox* SellRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		AddShopRow(SellRows, SellRow, 4.0f);
		for (int32 Column = 0; Column < 5; ++Column)
		{
			const int32 SlotIndex = Row * 5 + Column;
			int32 Price = 0;
			FText Reason;
			const bool bHasItem = BackpackItems.IsValidIndex(SlotIndex);
			const bool bCanQuote = bHasItem && Shop->TryGetSellPrice(SlotIndex, Price, Reason);
			UCastleShopActionWidget* Action = CreateWidget<UCastleShopActionWidget>(GetOwningPlayer());
			if (Action == nullptr)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("商店出售按钮创建失败。"));
				return;
			}
			FString Label = FString::Printf(TEXT("第 %d 格 · 空槽位"), SlotIndex + 1);
			if (bHasItem)
			{
				const FCastleBuildingDescriptor& Item = BackpackItems[SlotIndex];
				const FString Type = StaticEnum<ECastleBuildingType>()->GetDisplayNameTextByValue(static_cast<int64>(Item.BuildingType)).ToString();
				const FString Color = StaticEnum<ECastleTileColor>()->GetDisplayNameTextByValue(static_cast<int64>(Item.Color)).ToString();
				Label = FString::Printf(TEXT("%d %s %s%d\nLv%d · %s"), SlotIndex + 1, *Type, *Color, Item.Number, Item.Level,
					bCanQuote ? *FString::Printf(TEXT("出售 +%d 金币"), Price) : TEXT("报价失败"));
			}
			Action->Configure(true, SlotIndex, FText::FromString(Label), bCanQuote);
			if (bHasItem && !bCanQuote)
			{
				Action->SetToolTipText(Reason);
			}
			Action->OnTradeRequested.AddDynamic(this, &UCastleShopWidget::HandleTrade);
			SellRow->AddChildToHorizontalBox(Action)->SetPadding(FMargin(4.0f, 0.0f));
		}
	}
}

void UCastleShopWidget::ShowResult(bool bSuccess, const FString& SuccessMessage, const FText& FailureReason)
{
	const FString Message = bSuccess ? SuccessMessage : FailureReason.ToString();
	StatusText->SetText(FText::FromString(Message));
	StatusText->SetColorAndOpacity(FSlateColor(bSuccess ? FLinearColor(0.4f, 1.0f, 0.65f) : FLinearColor(1.0f, 0.55f, 0.25f)));
	UE_LOG(LogMoving_Castle, Log, TEXT("商店操作：%s"), *Message);
}

void UCastleShopWidget::HandleTrade(bool bSelling, int32 SlotIndex)
{
	FText Reason;
	const bool bSuccess = bSelling ? Shop->TrySellBackpackItem(SlotIndex, Reason) : Shop->TryBuyOffer(SlotIndex, Reason);
	ShowResult(bSuccess, bSelling ? TEXT("出售成功，金币已到账；剩余卡牌槽位已向前排列。") : TEXT("购买成功，卡牌已加入背包；关闭商店后按对应数字键选择并放置。"), Reason);
}

void UCastleShopWidget::HandleRefresh()
{
	FText Reason;
	const bool bSuccess = Shop->TryRefreshOffers(Reason);
	ShowResult(bSuccess, TEXT("刷新成功，已重新生成六张商品。"), Reason);
}

void UCastleShopWidget::HandleClose()
{
	Shop->CloseShop();
}

FReply UCastleShopWidget::NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Shop != nullptr && Shop->IsShopOpen())
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
		const FKey SlotKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
			EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(SlotKeys); ++Index)
		{
			if (Key == SlotKeys[Index] && (InKeyEvent.IsShiftDown() || Index < 6))
			{
				HandleTrade(InKeyEvent.IsShiftDown(), Index);
				return FReply::Handled();
			}
		}
	}
	return Super::NativeOnKeyDown(MyGeometry, InKeyEvent);
}
