// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingCardWidget.h"

#include "Blueprint/WidgetTree.h"
#include "CastleBuildingPreviewWidget.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UCastleBuildingCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (WidgetTree == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("建筑卡片 UI 初始化失败：WidgetTree 为空。"));
		return;
	}

	// 根 SizeBox 固定卡片尺寸，防止预览线框或动态文字导致相邻卡片跳动。
	USizeBox* CardSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardSizeBox"));
	CardSizeBox->SetWidthOverride(148.0f);
	CardSizeBox->SetHeightOverride(212.0f);
	WidgetTree->RootWidget = CardSizeBox;

	CardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardBorder"));
	CardBorder->SetPadding(FMargin(8.0f, 7.0f, 8.0f, 7.0f));
	CardBorder->SetBrushColor(FLinearColor(0.015f, 0.025f, 0.045f, 0.94f));
	CardSizeBox->AddChild(CardBorder);

	UVerticalBox* CardColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CardColumn"));
	CardBorder->SetContent(CardColumn);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	TitleText->SetJustification(ETextJustify::Center);
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.96f, 1.0f, 1.0f)));
	TitleText->SetText(FText::FromString(TEXT("空槽位")));
	if (UVerticalBoxSlot* TitleSlot = CardColumn->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	}

	USizeBox* PreviewSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PreviewSizeBox"));
	PreviewSizeBox->SetWidthOverride(132.0f);
	PreviewSizeBox->SetHeightOverride(151.0f);
	PreviewWidget = WidgetTree->ConstructWidget<UCastleBuildingPreviewWidget>(UCastleBuildingPreviewWidget::StaticClass(), TEXT("PreviewWidget"));
	PreviewSizeBox->AddChild(PreviewWidget);
	if (UVerticalBoxSlot* PreviewSlot = CardColumn->AddChildToVerticalBox(PreviewSizeBox))
	{
		PreviewSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
		PreviewSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	}

	DetailText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailText"));
	DetailText->SetJustification(ETextJustify::Center);
	DetailText->SetColorAndOpacity(FSlateColor(FLinearColor(0.64f, 0.72f, 0.82f, 1.0f)));
	DetailText->SetText(FText::FromString(TEXT("未存入卡牌")));
	if (UVerticalBoxSlot* DetailSlot = CardColumn->AddChildToVerticalBox(DetailText))
	{
		DetailSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
	}

	RefreshVisuals();
}

void UCastleBuildingCardWidget::SetBuildingDescriptor(const FCastleBuildingDescriptor& InDescriptor)
{
	Descriptor = InDescriptor;
	bHasDescriptor = Descriptor.HasBuildingClass();
	RefreshVisuals();
}

void UCastleBuildingCardWidget::SetEmptySlot()
{
	Descriptor = FCastleBuildingDescriptor();
	bHasDescriptor = false;
	RefreshVisuals();
}

void UCastleBuildingCardWidget::RefreshVisuals()
{
	if (TitleText == nullptr || DetailText == nullptr || PreviewWidget == nullptr || CardBorder == nullptr)
	{
		// NativeConstruct 尚未完成时，设置数据只保留在成员变量中，构造完成后会再次刷新。
		return;
	}

	if (!bHasDescriptor)
	{
		TitleText->SetText(FText::FromString(TEXT("空槽位")));
		DetailText->SetText(FText::FromString(TEXT("未存入卡牌")));
		PreviewWidget->ClearPreview();
		PreviewWidget->SetVisibility(ESlateVisibility::Collapsed);
		CardBorder->SetBrushColor(FLinearColor(0.015f, 0.025f, 0.045f, 0.62f));
		return;
	}

	const FText TypeText = GetBuildingTypeText(Descriptor.BuildingType);
	TitleText->SetText(FText::FromString(FString::Printf(TEXT("%s  |  %d"), *TypeText.ToString(), Descriptor.Number)));
	DetailText->SetText(FText::FromString(FString::Printf(TEXT("Lv%d  |  %s"), Descriptor.Level, *GetBuildingColorText(Descriptor.Color).ToString())));
	PreviewWidget->SetVisibility(ESlateVisibility::Visible);
	PreviewWidget->SetBuildingDescriptor(Descriptor);

	const FLinearColor Color = GetCardColor(Descriptor.Color);
	CardBorder->SetBrushColor(FLinearColor(Color.R, Color.G, Color.B, 0.24f));
}

FLinearColor UCastleBuildingCardWidget::GetCardColor(ECastleTileColor Color)
{
	switch (Color)
	{
	case ECastleTileColor::Red:
		return FLinearColor(0.98f, 0.18f, 0.18f, 1.0f);
	case ECastleTileColor::Blue:
		return FLinearColor(0.16f, 0.50f, 1.0f, 1.0f);
	case ECastleTileColor::Green:
		return FLinearColor(0.16f, 0.92f, 0.36f, 1.0f);
	case ECastleTileColor::Yellow:
		return FLinearColor(1.0f, 0.78f, 0.12f, 1.0f);
	case ECastleTileColor::Purple:
		return FLinearColor(0.76f, 0.30f, 1.0f, 1.0f);
	case ECastleTileColor::Gray:
	default:
		return FLinearColor(0.72f, 0.82f, 0.90f, 1.0f);
	}
}

FText UCastleBuildingCardWidget::GetBuildingTypeText(ECastleBuildingType BuildingType)
{
	switch (BuildingType)
	{
	case ECastleBuildingType::ArcherTower:
		return FText::FromString(TEXT("箭塔"));
	case ECastleBuildingType::CannonTower:
		return FText::FromString(TEXT("炮塔"));
	case ECastleBuildingType::Farm:
		return FText::FromString(TEXT("农田"));
	case ECastleBuildingType::Road:
	default:
		return FText::FromString(TEXT("道路"));
	}
}

FText UCastleBuildingCardWidget::GetBuildingColorText(ECastleTileColor Color)
{
	switch (Color)
	{
	case ECastleTileColor::Red:
		return FText::FromString(TEXT("红色"));
	case ECastleTileColor::Blue:
		return FText::FromString(TEXT("蓝色"));
	case ECastleTileColor::Green:
		return FText::FromString(TEXT("绿色"));
	case ECastleTileColor::Yellow:
		return FText::FromString(TEXT("黄色"));
	case ECastleTileColor::Purple:
		return FText::FromString(TEXT("紫色"));
	case ECastleTileColor::Gray:
	default:
		return FText::FromString(TEXT("灰色"));
	}
}
