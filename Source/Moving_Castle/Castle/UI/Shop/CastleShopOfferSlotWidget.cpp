// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleShopOfferSlotWidget.h"

#include "Castle/Components/CastleShopComponent.h"
#include "Castle/UI/Cards/CastleShopCardWidget.h"
#include "Castle/UI/Common/CastleHUDControls.h"
#include "CastleBuildingDefinition.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Moving_Castle.h"

namespace
{
	/** 商店槽位的色标对应红、黄、蓝、绿，调用方负责报告无效色值。 */
	bool TryResolveOfferGameplayTint(const ECastleTileColor Color, FLinearColor& OutColor)
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

void UCastleShopOfferSlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(false);
	if (IsValid(PriceButton))
	{
		PriceButton->OnClicked.AddUniqueDynamic(this, &UCastleShopOfferSlotWidget::HandlePriceButtonClicked);
		PriceButton->OnPressed.AddUniqueDynamic(this, &UCastleShopOfferSlotWidget::HandlePriceButtonPressed);
		PriceButton->OnReleased.AddUniqueDynamic(this, &UCastleShopOfferSlotWidget::HandlePriceButtonReleased);
		PriceButton->OnHovered.AddUniqueDynamic(this, &UCastleShopOfferSlotWidget::HandlePriceButtonHovered);
		PriceButton->OnUnhovered.AddUniqueDynamic(this, &UCastleShopOfferSlotWidget::HandlePriceButtonUnhovered);
	}
}

bool UCastleShopOfferSlotWidget::ValidatePresentation() const
{
	if (!CastleHUDControls::HasValidTextureBrush(PedestalBackground)
		|| !CastleHUDControls::HasValidTextureBrush(SoldBanner)
		|| !CastleHUDControls::HasValidTextureBrush(CoinIcon))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店商品槽 WBP %s 缺少 PedestalBackground、SoldBanner 或 CoinIcon 的有效贴图。"),
			*GetClass()->GetPathName());
		return false;
	}
	if (!IsValid(ColorAccentBar) || !IsValid(CardHost) || !IsValid(SoldOverlay) || !IsValid(SoldText)
		|| !IsValid(MergeHintBadge) || !IsValid(MergeHintText) || !IsValid(PriceButton)
		|| !IsValid(PriceButtonBg) || !IsValid(HotkeyText) || !IsValid(PriceText))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店商品槽 WBP %s 缺少必需控件绑定。"), *GetClass()->GetPathName());
		return false;
	}
	if (PriceButtonBg->GetNumWidgets() != 3)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店商品槽 WBP %s 的 PriceButtonBg 必须包含 3 态切图（常态/按下/禁用），实际 %d。"),
			*GetClass()->GetPathName(), PriceButtonBg->GetNumWidgets());
		return false;
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const UImage* StateImage = Cast<UImage>(PriceButtonBg->GetWidgetAtIndex(Index));
		if (!CastleHUDControls::HasValidTextureBrush(StateImage))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店商品槽 WBP %s 的 PriceButtonBg[%d] 贴图资源或尺寸无效。"),
				*GetClass()->GetPathName(), Index);
			return false;
		}
	}
	return true;
}

bool UCastleShopOfferSlotWidget::ConfigureOffer(UCastleShopWidget& InShop, const int32 InOfferIndex,
	const FCastleShopOffer& Offer, const ACastleBoard* Board, const bool bCanAfford, const bool bHasCapacity,
	const bool bFoilPreview, const int32 MatchingInBackpackCount)
{
	if (!ValidatePresentation())
	{
		return false;
	}

	OfferIndex = InOfferIndex;
	bSoldState = Offer.bSold;
	bPricePressed = false;

	FLinearColor GameplayTint;
	if (!TryResolveOfferGameplayTint(Offer.Descriptor.Color, GameplayTint))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店商品槽 %d 收到非法玩法色 %d。"),
			InOfferIndex, static_cast<int32>(Offer.Descriptor.Color));
		return false;
	}

	// 首次使用时在 CardHost 中实例化现有 3D 建筑卡牌控件，后续仅重新配置参数，不反复拆树。
	if (!IsValid(CardWidget))
	{
		UCastleShopCardWidget* NewCard = CreateWidget<UCastleShopCardWidget>(GetOwningPlayer());
		if (!IsValid(NewCard) || CardHost->AddChild(NewCard) == nullptr)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店商品槽 %d 无法在 CardHost 中创建 UCastleShopCardWidget。"), InOfferIndex);
			return false;
		}
		NewCard->OnCardClicked.AddUObject(this, &UCastleShopOfferSlotWidget::HandleCardShortClicked);
		NewCard->OnCardHovered.AddUObject(this, &UCastleShopOfferSlotWidget::HandleCardHovered);
		CardWidget = NewCard;
	}

	const bool bCardConfigured = CardWidget->Configure(InShop, Offer.Descriptor, Board, Offer.bSold);
	CardWidget->SetFoilPreview(bFoilPreview);

	ColorAccentBar->SetColorAndOpacity(Offer.bSold ? (GameplayTint * 0.35f) : GameplayTint);
	HotkeyText->SetText(FText::AsNumber(InOfferIndex + 1));

	// 只读合成提示：仅功能建筑（非普通房间）、未售罄且背包内已有同色同号牌时点亮。
	const bool bShowMergeHint = !Offer.bSold && IsValid(Offer.Descriptor.Definition)
		&& Offer.Descriptor.Definition->BuildingType != ECastleBuildingType::NormalRoom
		&& MatchingInBackpackCount > 0;
	MergeHintBadge->SetVisibility(bShowMergeHint ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bShowMergeHint)
	{
		MergeHintText->SetText(FText::Format(NSLOCTEXT("CastleShopOffer", "MergeHint", "可合成 ×{0}"),
			FText::AsNumber(MatchingInBackpackCount)));
	}

	SoldOverlay->SetVisibility(Offer.bSold ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	SoldText->SetText(NSLOCTEXT("CastleShopOffer", "SoldStamp", "已售罄"));

	// 未售罄时保留按钮可点，确保资金不足或满包点击仍能收到商店组件的中文失败提示。
	PriceButton->SetIsEnabled(!Offer.bSold);
	CoinIcon->SetVisibility(Offer.bSold ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (Offer.bSold)
	{
		PriceText->SetText(NSLOCTEXT("CastleShopOffer", "SoldPrice", "已售罄"));
		PriceText->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.60f, 0.68f, 1.0f)));
	}
	else
	{
		PriceText->SetText(FText::AsNumber(Offer.Price));
		const bool bCanBuyNow = bCanAfford && bHasCapacity;
		PriceText->SetColorAndOpacity(FSlateColor(bCanBuyNow
			? FLinearColor(0.98f, 0.85f, 0.45f, 1.0f)
			: FLinearColor(0.96f, 0.38f, 0.34f, 1.0f)));
	}
	RefreshPriceButtonBg();
	return bCardConfigured;
}

void UCastleShopOfferSlotWidget::SetFoilPreview(const bool bEnabled)
{
	if (IsValid(CardWidget))
	{
		CardWidget->SetFoilPreview(bEnabled);
	}
}

void UCastleShopOfferSlotWidget::HandlePriceButtonClicked()
{
	if (!bSoldState && OfferIndex != INDEX_NONE)
	{
		OnOfferPurchaseRequested.Broadcast(OfferIndex);
	}
}

void UCastleShopOfferSlotWidget::HandlePriceButtonPressed()
{
	if (!bSoldState)
	{
		bPricePressed = true;
		RefreshPriceButtonBg();
	}
}

void UCastleShopOfferSlotWidget::HandlePriceButtonReleased()
{
	bPricePressed = false;
	RefreshPriceButtonBg();
}

void UCastleShopOfferSlotWidget::HandlePriceButtonHovered()
{
	bButtonHovered = true;
	if (OfferIndex != INDEX_NONE)
	{
		OnOfferHovered.Broadcast(OfferIndex, true);
	}
}

void UCastleShopOfferSlotWidget::HandlePriceButtonUnhovered()
{
	bButtonHovered = false;
	if (!bCardHovered && OfferIndex != INDEX_NONE)
	{
		OnOfferHovered.Broadcast(OfferIndex, false);
	}
}

void UCastleShopOfferSlotWidget::HandleCardShortClicked(UCastleShopCardWidget& Card)
{
	if (!bSoldState && OfferIndex != INDEX_NONE)
	{
		OnOfferPurchaseRequested.Broadcast(OfferIndex);
	}
}

void UCastleShopOfferSlotWidget::HandleCardHovered(UCastleShopCardWidget& Card, const bool bInHovered)
{
	bCardHovered = bInHovered;
	if (OfferIndex != INDEX_NONE)
	{
		OnOfferHovered.Broadcast(OfferIndex, bCardHovered || bButtonHovered);
	}
}

void UCastleShopOfferSlotWidget::RefreshPriceButtonBg()
{
	if (!IsValid(PriceButtonBg))
	{
		return;
	}
	if (bSoldState)
	{
		PriceButtonBg->SetActiveWidgetIndex(2);
	}
	else
	{
		PriceButtonBg->SetActiveWidgetIndex(bPricePressed ? 1 : 0);
	}
}
