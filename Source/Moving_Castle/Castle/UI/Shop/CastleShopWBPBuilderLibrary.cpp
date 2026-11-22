// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleShopWBPBuilderLibrary.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "Moving_Castle.h"

#if WITH_EDITOR
#include "Kismet2/KismetEditorUtilities.h"
#include "WidgetBlueprint.h"
#endif

UWidget* UCastleShopWBPBuilderLibrary::ConstructAndAttachWidget(UObject* WidgetBlueprintObject,
	const TSubclassOf<UWidget> WidgetClass, const FName WidgetName, UPanelWidget* ParentPanel, const bool bIsVariable)
{
#if WITH_EDITOR
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(WidgetBlueprintObject);
	if (!IsValid(WidgetBlueprint) || !IsValid(WidgetBlueprint->WidgetTree) || !IsValid(WidgetClass.Get()) || WidgetName.IsNone())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("ConstructAndAttachWidget 参数无效：Blueprint=%s, Class=%s, Name=%s。"),
			*GetPathNameSafe(WidgetBlueprintObject), *GetPathNameSafe(WidgetClass.Get()), *WidgetName.ToString());
		return nullptr;
	}

	WidgetBlueprint->Modify();
	UWidget* NewWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
	if (!IsValid(NewWidget))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("在 %s 中构造控件 %s (%s) 失败。"),
			*WidgetBlueprint->GetPathName(), *WidgetName.ToString(), *WidgetClass->GetName());
		return nullptr;
	}

	NewWidget->bIsVariable = bIsVariable;
	if (ParentPanel == nullptr)
	{
		WidgetBlueprint->WidgetTree->RootWidget = NewWidget;
	}
	else
	{
		if (ParentPanel->AddChild(NewWidget) == nullptr)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("将控件 %s 添加到父面板 %s 失败（%s）。"),
				*WidgetName.ToString(), *ParentPanel->GetName(), *WidgetBlueprint->GetPathName());
			return nullptr;
		}
	}
	return NewWidget;
#else
	UE_LOG(LogMoving_Castle, Error, TEXT("ConstructAndAttachWidget 仅允许在编辑器环境中调用。"));
	return nullptr;
#endif
}

void UCastleShopWBPBuilderLibrary::ConfigureImageBrush(UImage* Image, UTexture2D* Texture, const FVector2D ImageSize,
	const bool bUseBox9Slice, const FMargin BoxMargin, const FLinearColor TintColor)
{
	if (!IsValid(Image))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("ConfigureImageBrush 失败：Image 为空。"));
		return;
	}
	FSlateBrush Brush;
	Brush.SetResourceObject(Texture);
	Brush.ImageSize = FDeprecateSlateVector2D(static_cast<float>(ImageSize.X), static_cast<float>(ImageSize.Y));
	Brush.DrawAs = bUseBox9Slice ? ESlateBrushDrawType::Box : ESlateBrushDrawType::Image;
	if (bUseBox9Slice)
	{
		Brush.Margin = BoxMargin;
	}
	Brush.TintColor = FSlateColor(TintColor);
	Image->SetBrush(Brush);
}

void UCastleShopWBPBuilderLibrary::ConfigureTransparentButton(UButton* Button, const bool bIsFocusable)
{
	if (!IsValid(Button))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("ConfigureTransparentButton 失败：Button 为空。"));
		return;
	}
	FSlateBrush NoBrush;
	NoBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
	FButtonStyle Style;
	Style.SetNormal(NoBrush);
	Style.SetHovered(NoBrush);
	Style.SetPressed(NoBrush);
	Style.SetDisabled(NoBrush);
	Style.SetNormalPadding(FMargin(0.0f));
	Style.SetPressedPadding(FMargin(0.0f));
	Button->SetStyle(Style);
	if (FBoolProperty* FocusProp = FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable")))
	{
		FocusProp->SetPropertyValue_InContainer(Button, bIsFocusable);
	}
}

void UCastleShopWBPBuilderLibrary::ConfigureProgressBarColors(UProgressBar* ProgressBar,
	const FLinearColor BackgroundColor, const FLinearColor FillColor)
{
	if (!IsValid(ProgressBar))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("ConfigureProgressBarColors 失败：ProgressBar 为空。"));
		return;
	}
	FProgressBarStyle Style = ProgressBar->GetWidgetStyle();
	Style.BackgroundImage.DrawAs = ESlateBrushDrawType::Image;
	Style.BackgroundImage.TintColor = FSlateColor(BackgroundColor);
	Style.FillImage.DrawAs = ESlateBrushDrawType::Image;
	Style.FillImage.TintColor = FSlateColor(FillColor);
	ProgressBar->SetWidgetStyle(Style);
	ProgressBar->SetFillColorAndOpacity(FLinearColor::White);
}

bool UCastleShopWBPBuilderLibrary::CompileWidgetBlueprintAsset(UObject* WidgetBlueprintObject)
{
#if WITH_EDITOR
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(WidgetBlueprintObject);
	if (!IsValid(WidgetBlueprint))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("CompileWidgetBlueprintAsset 失败：对象 %s 不是有效 WidgetBlueprint。"),
			*GetPathNameSafe(WidgetBlueprintObject));
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint, EBlueprintCompileOptions::None);
	if (WidgetBlueprint->Status == BS_Error)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("WidgetBlueprint %s 编译失败（Status=BS_Error）。"),
			*WidgetBlueprint->GetPathName());
		return false;
	}
	return true;
#else
	UE_LOG(LogMoving_Castle, Error, TEXT("CompileWidgetBlueprintAsset 仅允许在编辑器环境中调用。"));
	return false;
#endif
}
