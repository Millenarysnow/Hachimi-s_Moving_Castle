// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildBarSlotWidget.h"

#include "CastleBuildingDefinition.h"
#include "CastleBuildingPreviewWidget.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Moving_Castle.h"

bool UCastleBuildBarSlotWidget::ValidatePresentation() const
{
	const struct { const TCHAR* Name; const UImage* Image; } RequiredImages[] = {
		{TEXT("NormalCircle"), NormalCircle}, {TEXT("EmptyCircle"), EmptyCircle},
		{TEXT("SelectedCircle"), SelectedCircle}, {TEXT("SelectionDot"), SelectionDot}
	};
	for (const auto& RequiredImage : RequiredImages)
	{
		if (!IsValid(RequiredImage.Image) || !IsValid(RequiredImage.Image->GetBrush().GetResourceObject()))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 缺少控件 %s 或其切图。"),
				*GetClass()->GetPathName(), RequiredImage.Name);
			return false;
		}
	}
	if (!IsValid(PreviewBackdrop) || !IsValid(PreviewArea) || !IsValid(CardStats))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 缺少 PreviewBackdrop、PreviewArea 或 CardStats。"), *GetClass()->GetPathName());
		return false;
	}
	return true;
}

bool UCastleBuildBarSlotWidget::SetCard(const FCastleBuildingDescriptor* Card)
{
	if (Card == nullptr)
	{
		// 游戏库存里的空槽不是错误，也不展示任何虚构的建筑。
		if (Preview != nullptr)
		{
			Preview->ClearPreview();
		}
		PreviewBackdrop->SetVisibility(ESlateVisibility::Collapsed);
		PreviewArea->SetVisibility(ESlateVisibility::Collapsed);
		CardStats->SetVisibility(ESlateVisibility::Collapsed);
		bHasCard = false;
		return true;
	}

	if (Preview == nullptr)
	{
		UCastleBuildingPreviewWidget* NewPreview = CreateWidget<UCastleBuildingPreviewWidget>(GetOwningPlayer());
		if (NewPreview == nullptr || PreviewArea->AddChild(NewPreview) == nullptr)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 无法在 PreviewArea 创建建筑预览。"), *GetClass()->GetPathName());
			return false;
		}
		Preview = NewPreview;
	}

	// 直接用现有建筑预览控件；捕获、描边材质和卡牌颜色与原背包及商店完全一致。
	Preview->SetBuildingDescriptor(*Card);
	if (!Preview->HasPreviewDescriptor())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 无法显示建筑定义 %s 的 %d 级预览。"),
			*GetClass()->GetPathName(), *GetPathNameSafe(Card->Definition), Card->Level);
		return false;
	}

	CardStats->SetText(FText::Format(NSLOCTEXT("CastleBuildBar", "CardStats", "{0} · Lv{1}"), Card->Number, Card->Level));
	CardStats->SetVisibility(ESlateVisibility::HitTestInvisible);
	PreviewBackdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
	PreviewArea->SetVisibility(ESlateVisibility::HitTestInvisible);
	bHasCard = true;
	return true;
}

void UCastleBuildBarSlotWidget::SetSelected(bool bSelected)
{
	// 黄点与选中底盘共用控制器状态；即便库存改变早于输入更新，也不会给空槽画选中环。
	const bool bShowSelection = bHasCard && bSelected;
	EmptyCircle->SetVisibility(bHasCard ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	NormalCircle->SetVisibility(bHasCard && !bShowSelection ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	SelectedCircle->SetVisibility(bShowSelection ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	SelectionDot->SetVisibility(bShowSelection ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}
