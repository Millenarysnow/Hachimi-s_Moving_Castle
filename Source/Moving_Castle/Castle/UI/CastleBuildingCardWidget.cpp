// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingCardWidget.h"

#include "CastleBuildingDefinition.h"

#include "Blueprint/WidgetTree.h"
#include "CastleBuildingPreviewWidget.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Moving_Castle.h"

namespace
{
	constexpr TCHAR BuildingCardFontAssetPath[] = TEXT("/Game/MovingCastle/Fonts/MicrosoftYaHei_Font.MicrosoftYaHei_Font");
	const FName BuildingCardTypefaceName(TEXT("Default"));

	/** 统一字体资源和已登记字重，同时保留控件原有样式及各自指定的字号。 */
	void ApplyChineseFont(UTextBlock& TextBlock, const UFont& FontAsset, int32 Size)
	{
		FSlateFontInfo Font = TextBlock.GetFont();
		Font.FontObject = &FontAsset;
		Font.TypefaceFontName = BuildingCardTypefaceName;
		Font.Size = Size;
		TextBlock.SetFont(Font);
	}

	/** 同时设置边框色和显示名，确保两者始终表达同一张牌的玩法色。 */
	bool TryGetCardColor(const ECastleTileColor Color, FLinearColor& OutTint, FText& OutName)
	{
		switch (Color)
		{
		case ECastleTileColor::Red:
			OutTint = FLinearColor(0.98f, 0.18f, 0.18f);
			OutName = INVTEXT("红色");
			return true;
		case ECastleTileColor::Yellow:
			OutTint = FLinearColor(1.0f, 0.78f, 0.12f);
			OutName = INVTEXT("黄色");
			return true;
		case ECastleTileColor::Blue:
			OutTint = FLinearColor(0.16f, 0.50f, 1.0f);
			OutName = INVTEXT("蓝色");
			return true;
		case ECastleTileColor::Green:
			OutTint = FLinearColor(0.16f, 0.92f, 0.36f);
			OutName = INVTEXT("绿色");
			return true;
		default:
			return false;
		}
	}
}

void UCastleBuildingCardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UE_LOG(LogMoving_Castle, Log, TEXT("建筑卡片 NativeOnInitialized 已执行。"));

	if (bWidgetTreeBuilt)
	{
		return;
	}

	if (WidgetTree == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("建筑卡片 UI 初始化失败：WidgetTree 为空。"));
		return;
	}
	// 在创建卡片根控件前加载项目字体，避免资源错误时显示引擎默认字体。
	const UFont* FontAsset = LoadObject<UFont>(nullptr, BuildingCardFontAssetPath);
	if (!IsValid(FontAsset))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡片 %s 无法初始化：字体资产 %s 加载失败或类型不是 UFont。"),
			*GetPathName(), BuildingCardFontAssetPath);
		return;
	}
	// 只接受该资产自己的运行时复合字体，防止 Slate 从离线字体改用引擎字体。
	const FCompositeFont* CompositeFont = FontAsset->GetCompositeFont();
	if (CompositeFont != &FontAsset->GetInternalCompositeFont())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡片 %s 无法初始化：资产 %s 未使用自身的运行时复合字体。"),
			*GetPathName(), BuildingCardFontAssetPath);
		return;
	}
	// 确认指定字重有实际字体数据，再创建标题和详情控件。
	const FTypefaceEntry* Typeface = CompositeFont->DefaultTypeface.Fonts.FindByPredicate(
		[](const FTypefaceEntry& Entry) { return Entry.Name == BuildingCardTypefaceName; });
	if (!Typeface || !Typeface->Font.HasFont())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡片 %s 无法初始化：字体资产 %s 中的字重 %s 缺失或未配置字体数据。"),
			*GetPathName(), BuildingCardFontAssetPath, *BuildingCardTypefaceName.ToString());
		return;
	}

	// 根 SizeBox 固定卡片尺寸，防止预览模型或动态文字导致相邻卡片跳动。
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
	// 标题和详情使用明确字号，使固定 212 高度的卡片能同时容纳模型和文字，
	// 商店在卡片外增加价格按钮时也不会被溢出的默认大字号覆盖。
	ApplyChineseFont(*TitleText, *FontAsset, 16);
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
	ApplyChineseFont(*DetailText, *FontAsset, 13);
	DetailText->SetColorAndOpacity(FSlateColor(FLinearColor(0.64f, 0.72f, 0.82f, 1.0f)));
	DetailText->SetText(FText::FromString(TEXT("未存入卡牌")));
	if (UVerticalBoxSlot* DetailSlot = CardColumn->AddChildToVerticalBox(DetailText))
	{
		DetailSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
	}

	bWidgetTreeBuilt = true;
}

void UCastleBuildingCardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshVisuals();
}

void UCastleBuildingCardWidget::SetBuildingDescriptor(const FCastleBuildingDescriptor& InDescriptor)
{
	// 有定义的卡必须先通过色值校验，拒绝覆盖当前控件已展示的有效描述。
	if (InDescriptor.HasDefinition() && !CastleTileColors::IsPlayable(InDescriptor.Color))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡片 %s 的定义 %s 使用了无效色值 %d。"),
			*GetPathName(), *GetPathNameSafe(InDescriptor.Definition.Get()), static_cast<int32>(InDescriptor.Color));
		return;
	}
	Descriptor = InDescriptor;
	bHasDescriptor = Descriptor.HasDefinition();
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

	FLinearColor Color;
	FText ColorName;
	if (!TryGetCardColor(Descriptor.Color, Color, ColorName))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡片 %s 的定义 %s 缺少色值 %d 的显示配置。"),
			*GetPathName(), *GetPathNameSafe(Descriptor.Definition.Get()), static_cast<int32>(Descriptor.Color));
		return;
	}

	const FText TypeText = Descriptor.Definition->DisplayName;
	TitleText->SetText(FText::FromString(FString::Printf(TEXT("%s  |  %d"), *TypeText.ToString(), Descriptor.Number)));
	DetailText->SetText(FText::FromString(FString::Printf(TEXT("Lv%d  |  %s"), Descriptor.Level, *ColorName.ToString())));
	PreviewWidget->SetVisibility(ESlateVisibility::Visible);
	PreviewWidget->SetBuildingDescriptor(Descriptor);

	CardBorder->SetBrushColor(FLinearColor(Color.R, Color.G, Color.B, 0.24f));
}
