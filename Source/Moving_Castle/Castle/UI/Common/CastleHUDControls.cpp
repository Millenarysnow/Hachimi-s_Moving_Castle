// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleHUDControls.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/WidgetSwitcher.h"
#include "Moving_Castle.h"

namespace CastleHUDControls
{
	bool HasValidTextureBrush(const UImage* Image)
	{
		if (!IsValid(Image))
		{
			return false;
		}
		// 同时校验贴图资源、绘制方式和非零图像尺寸，避免出现隐形占位控件。
		const FSlateBrush& Brush = Image->GetBrush();
		const auto Size = Brush.GetImageSize();
		return IsValid(Brush.GetResourceObject())
			&& Brush.GetDrawType() != ESlateBrushDrawType::NoDrawType
			&& Size.X > 0.0f && Size.Y > 0.0f;
	}
}

namespace
{
	/** 根据展示模式返回 IconStates 必须具备的精确子项数量。 */
	int32 GetRequiredIconStateCount(const ECastleHUDButtonPresentationMode Mode)
	{
		switch (Mode)
		{
		case ECastleHUDButtonPresentationMode::Standard:
			return 2;
		case ECastleHUDButtonPresentationMode::WithDisabledState:
			return 3;
		case ECastleHUDButtonPresentationMode::TwoStateToggle:
			return 4;
		default:
			return 0;
		}
	}
}

bool UCastleIconBarSlotWidget::ValidatePresentation() const
{
	if (!IsValid(IconStates) || IconStates->GetNumWidgets() != 2)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("图标进度槽 WBP %s 缺少 IconStates 或子项数不为 2（实际 %d）。"),
			*GetClass()->GetPathName(), IsValid(IconStates) ? IconStates->GetNumWidgets() : 0);
		return false;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const UImage* StateImage = Cast<UImage>(IconStates->GetWidgetAtIndex(Index));
		if (!CastleHUDControls::HasValidTextureBrush(StateImage))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("图标进度槽 WBP %s 的 IconStates[%d] 不是有效贴图或尺寸为零。"),
				*GetClass()->GetPathName(), Index);
			return false;
		}
	}
	return true;
}

bool UCastleIconBarSlotWidget::SetFilled(const bool bInFilled)
{
	if (!IsValid(IconStates) || IconStates->GetNumWidgets() != 2)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("图标进度槽 WBP %s 切换状态失败：IconStates 无效。"), *GetClass()->GetPathName());
		return false;
	}
	bFilled = bInFilled;
	IconStates->SetActiveWidgetIndex(bFilled ? 1 : 0);
	return true;
}

TStaticArray<UCastleIconBarSlotWidget*, UCastleIconBarWidget::FixedIconCount> UCastleIconBarWidget::GetSlots() const
{
	return {
		Slot01, Slot02, Slot03, Slot04, Slot05,
		Slot06, Slot07, Slot08, Slot09, Slot10,
		Slot11, Slot12, Slot13, Slot14, Slot15,
		Slot16, Slot17, Slot18, Slot19, Slot20
	};
}

bool UCastleIconBarWidget::ValidatePresentation() const
{
	const auto Slots = GetSlots();
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (!IsValid(Slots[Index]) || !Slots[Index]->ValidatePresentation())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("图标进度条 WBP %s 的 Slot%02d 缺失或贴图无效。"),
				*GetClass()->GetPathName(), Index + 1);
			return false;
		}
	}
	return true;
}

bool UCastleIconBarWidget::SetFilledCount(const int32 NewFilledCount)
{
	if (NewFilledCount < 0 || NewFilledCount > FixedIconCount)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("图标进度条 WBP %s 收到非法点亮数 %d（合法范围 0～%d）。"),
			*GetClass()->GetPathName(), NewFilledCount, FixedIconCount);
		return false;
	}
	const auto Slots = GetSlots();
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (!IsValid(Slots[Index]) || !Slots[Index]->SetFilled(Index < NewFilledCount))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("图标进度条 WBP %s 更新 Slot%02d 失败。"),
				*GetClass()->GetPathName(), Index + 1);
			return false;
		}
	}
	FilledCount = NewFilledCount;
	return true;
}

void UCastleHUDIconButtonWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 图标按钮不抢占视口键盘焦点，保证 WASD、数字键、P/B/T/F/R 等快捷键始终生效。
	SetIsFocusable(false);
	if (!IsValid(ActionButton))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("HUD 图标按钮 WBP %s 缺少必需控件 ActionButton。"), *GetClass()->GetPathName());
		return;
	}
	ActionButton->OnClicked.AddUniqueDynamic(this, &UCastleHUDIconButtonWidget::HandleClicked);
	ActionButton->OnPressed.AddUniqueDynamic(this, &UCastleHUDIconButtonWidget::HandlePressed);
	ActionButton->OnReleased.AddUniqueDynamic(this, &UCastleHUDIconButtonWidget::HandleReleased);
	RefreshIconState();
}

bool UCastleHUDIconButtonWidget::ValidatePresentation(const ECastleHUDButtonPresentationMode ExpectedMode) const
{
	const int32 RequiredCount = GetRequiredIconStateCount(ExpectedMode);
	if (!IsValid(ActionButton) || !IsValid(IconStates) || RequiredCount <= 0 || IconStates->GetNumWidgets() != RequiredCount)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("HUD 图标按钮 WBP %s 校验失败：ActionButton=%s，IconStates 子项数=%d（期望 %d）。"),
			*GetClass()->GetPathName(), IsValid(ActionButton) ? TEXT("有效") : TEXT("空"),
			IsValid(IconStates) ? IconStates->GetNumWidgets() : 0, RequiredCount);
		return false;
	}
	for (int32 Index = 0; Index < RequiredCount; ++Index)
	{
		const UImage* StateImage = Cast<UImage>(IconStates->GetWidgetAtIndex(Index));
		if (!CastleHUDControls::HasValidTextureBrush(StateImage))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("HUD 图标按钮 WBP %s 的 IconStates[%d] 贴图资源或尺寸无效。"),
				*GetClass()->GetPathName(), Index);
			return false;
		}
	}
	return true;
}

bool UCastleHUDIconButtonWidget::ConfigurePresentationMode(const ECastleHUDButtonPresentationMode InMode)
{
	if (!ValidatePresentation(InMode))
	{
		return false;
	}
	PresentationMode = InMode;
	RefreshIconState();
	return true;
}

void UCastleHUDIconButtonWidget::SetActionEnabled(const bool bInEnabled)
{
	bActionEnabled = bInEnabled;
	if (!bActionEnabled)
	{
		bPressed = false;
	}
	if (IsValid(ActionButton))
	{
		ActionButton->SetIsEnabled(bActionEnabled);
	}
	RefreshIconState();
}

void UCastleHUDIconButtonWidget::SetAlternateState(const bool bInAlternate)
{
	if (PresentationMode != ECastleHUDButtonPresentationMode::TwoStateToggle)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("HUD 图标按钮 WBP %s 不是双态模式，拒绝设置第二态。"), *GetClass()->GetPathName());
		return;
	}
	bAlternateState = bInAlternate;
	RefreshIconState();
}

int32 UCastleHUDIconButtonWidget::GetActiveIconIndex() const
{
	return IsValid(IconStates) ? IconStates->GetActiveWidgetIndex() : INDEX_NONE;
}

void UCastleHUDIconButtonWidget::HandleClicked()
{
	if (!bActionEnabled)
	{
		return;
	}
	OnButtonClicked.Broadcast();
}

void UCastleHUDIconButtonWidget::HandlePressed()
{
	if (!bActionEnabled)
	{
		return;
	}
	bPressed = true;
	RefreshIconState();
}

void UCastleHUDIconButtonWidget::HandleReleased()
{
	bPressed = false;
	RefreshIconState();
}

void UCastleHUDIconButtonWidget::RefreshIconState()
{
	if (!IsValid(IconStates))
	{
		return;
	}
	int32 TargetIndex = 0;
	if (!bActionEnabled && PresentationMode == ECastleHUDButtonPresentationMode::WithDisabledState)
	{
		TargetIndex = 2;
	}
	else if (PresentationMode == ECastleHUDButtonPresentationMode::TwoStateToggle)
	{
		TargetIndex = (bAlternateState ? 2 : 0) + (bPressed ? 1 : 0);
	}
	else
	{
		TargetIndex = bPressed ? 1 : 0;
	}
	if (TargetIndex < IconStates->GetNumWidgets())
	{
		IconStates->SetActiveWidgetIndex(TargetIndex);
	}
}
