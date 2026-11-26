// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleShopBenchSlotWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Castle/UI/Common/CastleHUDControls.h"
#include "Castle/UI/Shop/CastleShopSellDragDropOperation.h"
#include "CastleBuildingDefinition.h"
#include "CastleBuildingPreviewWidget.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "InputCoreTypes.h"
#include "Moving_Castle.h"

namespace
{
	/** 备战席色条玩法色映射，与卡牌及棋盘完全一致。 */
	bool TryResolveBenchGameplayTint(const ECastleTileColor Color, FLinearColor& OutColor)
	{
		switch (Color)
		{
		case ECastleTileColor::Red: OutColor = FLinearColor(FColor::FromHex(TEXT("D54548"))); return true;
		case ECastleTileColor::Blue: OutColor = FLinearColor(FColor::FromHex(TEXT("287ADB"))); return true;
		case ECastleTileColor::Green: OutColor = FLinearColor(FColor::FromHex(TEXT("36A86F"))); return true;
		case ECastleTileColor::Yellow: OutColor = FLinearColor(FColor::FromHex(TEXT("E4AB31"))); return true;
		default: return false;
		}
	}
}

void UCastleShopBenchSlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(false);
}

bool UCastleShopBenchSlotWidget::ValidatePresentation() const
{
	if (!CastleHUDControls::HasValidTextureBrush(SlotNormalBg)
		|| !CastleHUDControls::HasValidTextureBrush(SlotEmptyBg))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店备战席槽位 WBP %s 缺少 SlotNormalBg 或 SlotEmptyBg 有效贴图。"),
			*GetClass()->GetPathName());
		return false;
	}
	if (!IsValid(ColorStrip) || !IsValid(PreviewHost) || !IsValid(HotkeyText)
		|| !IsValid(CardBadgeText) || !IsValid(SellPriceText))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店备战席槽位 WBP %s 缺少必需控件绑定。"), *GetClass()->GetPathName());
		return false;
	}
	return true;
}

bool UCastleShopBenchSlotWidget::ConfigureBenchSlot(const int32 InSlotIndex, const FCastleBuildingDescriptor* Card,
	const bool bCanQuoteSell, const int32 InSellPrice, const FText& QuoteFailureReason)
{
	if (!ValidatePresentation())
	{
		return false;
	}

	SlotIndex = InSlotIndex;
	const int32 KeyDigit = (InSlotIndex == 9) ? 0 : (InSlotIndex + 1);
	HotkeyText->SetText(FText::AsNumber(KeyDigit));

	if (Card == nullptr)
	{
		// 空槽只绘制占位框，不参与卡牌悬停或出售交互。
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		bHasCard = false;
		bCanSell = false;
		QuotedSellPrice = 0;
		CachedDescriptor = FCastleBuildingDescriptor();
		if (IsValid(PreviewWidget))
		{
			PreviewWidget->ClearPreview();
		}
		SlotNormalBg->SetVisibility(ESlateVisibility::Collapsed);
		SlotEmptyBg->SetVisibility(ESlateVisibility::HitTestInvisible);
		ColorStrip->SetVisibility(ESlateVisibility::Collapsed);
		PreviewHost->SetVisibility(ESlateVisibility::Collapsed);
		CardBadgeText->SetVisibility(ESlateVisibility::Collapsed);
		SellPriceText->SetVisibility(ESlateVisibility::Collapsed);
		SetToolTipText(FText::GetEmpty());
		return true;
	}

	FLinearColor GameplayTint;
	if (!TryResolveBenchGameplayTint(Card->Color, GameplayTint))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店备战席槽位 %d 收到非法玩法色 %d。"),
			InSlotIndex, static_cast<int32>(Card->Color));
		return false;
	}

	if (!IsValid(PreviewWidget))
	{
		UCastleBuildingPreviewWidget* NewPreview = CreateWidget<UCastleBuildingPreviewWidget>(GetOwningPlayer());
		if (!IsValid(NewPreview) || PreviewHost->AddChild(NewPreview) == nullptr)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店备战席槽位 %d 无法创建建筑预览控件。"), InSlotIndex);
			return false;
		}
		NewPreview->SetVisibility(ESlateVisibility::HitTestInvisible);
		PreviewWidget = NewPreview;
	}

	PreviewWidget->SetBuildingDescriptor(*Card);
	if (!PreviewWidget->HasPreviewDescriptor())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店备战席槽位 %d 无法显示建筑 %s Lv%d 预览。"),
			InSlotIndex, *GetPathNameSafe(Card->Definition.Get()), Card->Level);
		return false;
	}

	bHasCard = true;
	bCanSell = bCanQuoteSell;
	QuotedSellPrice = bCanQuoteSell ? InSellPrice : 0;
	CachedDescriptor = *Card;

	// 备战席蓝图里的槽位默认自身不参与命中。填入卡牌后由槽位本身接收鼠标按下和拖拽检测。
	SetVisibility(ESlateVisibility::Visible);
	SlotNormalBg->SetVisibility(ESlateVisibility::HitTestInvisible);
	SlotEmptyBg->SetVisibility(ESlateVisibility::Collapsed);
	ColorStrip->SetColorAndOpacity(GameplayTint);
	ColorStrip->SetVisibility(ESlateVisibility::HitTestInvisible);
	PreviewHost->SetVisibility(ESlateVisibility::HitTestInvisible);
	CardBadgeText->SetText(FText::Format(NSLOCTEXT("CastleShopBench", "CardBadge", "{0}·Lv{1}"),
		FText::AsNumber(Card->Number), FText::AsNumber(Card->Level)));
	CardBadgeText->SetVisibility(ESlateVisibility::HitTestInvisible);

	if (bCanQuoteSell)
	{
		SellPriceText->SetText(FText::Format(NSLOCTEXT("CastleShopBench", "SellPrice", "+{0}"),
			FText::AsNumber(InSellPrice)));
		SellPriceText->SetColorAndOpacity(FSlateColor(FLinearColor(0.98f, 0.85f, 0.44f, 1.0f)));
		SetToolTipText(FText::GetEmpty());
	}
	else
	{
		SellPriceText->SetText(NSLOCTEXT("CastleShopBench", "QuoteFailed", "不可售"));
		SellPriceText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.38f, 0.34f, 1.0f)));
		SetToolTipText(QuoteFailureReason);
	}
	SellPriceText->SetVisibility(ESlateVisibility::HitTestInvisible);
	return true;
}

void UCastleShopBenchSlotWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	if (bHasCard && SlotIndex != INDEX_NONE)
	{
		OnBenchHovered.Broadcast(SlotIndex, true);
	}
}

void UCastleShopBenchSlotWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	if (SlotIndex != INDEX_NONE)
	{
		OnBenchHovered.Broadcast(SlotIndex, false);
	}
}

FReply UCastleShopBenchSlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bHasCard || SlotIndex == INDEX_NONE)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	// 右键快速出售当前备战席卡牌。
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnBenchSellRequested.Broadcast(SlotIndex);
		return FReply::Handled();
	}
	// 左键按下开启拖拽检测，拖出后唤起底部货币战争风格出售回收遮罩。
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bCanSell)
	{
		return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UCastleShopBenchSlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
	if (!bHasCard || !bCanSell || SlotIndex == INDEX_NONE)
	{
		return;
	}

	UCastleShopSellDragDropOperation* DragOp = NewObject<UCastleShopSellDragDropOperation>(this);
	if (!IsValid(DragOp))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("备战席槽位 %d 创建拖拽出售操作失败。"), SlotIndex);
		return;
	}
	DragOp->BackpackSlotIndex = SlotIndex;
	DragOp->Descriptor = CachedDescriptor;
	DragOp->QuotedSellPrice = QuotedSellPrice;
	DragOp->Pivot = EDragPivot::CenterCenter;

	// 使用同一建筑预览控件作为跟随鼠标的轻量拖影，不复制任何新资源路径。
	UCastleBuildingPreviewWidget* DragVisual = CreateWidget<UCastleBuildingPreviewWidget>(GetOwningPlayer());
	if (IsValid(DragVisual))
	{
		DragVisual->SetBuildingDescriptor(CachedDescriptor);
		DragVisual->SetRenderScale(FVector2D(1.15f, 1.15f));
		DragOp->DefaultDragVisual = DragVisual;
	}

	OutOperation = DragOp;
	OnBenchDragStarted.Broadcast(SlotIndex, CachedDescriptor, QuotedSellPrice);
}

void UCastleShopBenchSlotWidget::NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	Super::NativeOnDragCancelled(InDragDropEvent, InOperation);
	OnBenchDragEnded.Broadcast();
}
