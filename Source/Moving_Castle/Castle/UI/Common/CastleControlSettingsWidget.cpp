// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleControlSettingsWidget.h"
#include "Audio/CastleAudioSubsystem.h"

#include "CastlePlayerController.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Moving_Castle.h"

bool UCastleControlSettingsWidget::BindController(ACastlePlayerController& Controller)
{
	// 只有 WBP 结构完整时才关联委托，调用方据此决定是否加入玩家视口。
	const TPair<const TCHAR*, UWidget*> Required[] = {{TEXT("ClassicButton"), ClassicButton.Get()},
		{TEXT("DesignButton"), DesignButton.Get()}, {TEXT("ClassicKeys"), ClassicKeys.Get()},
		{TEXT("DesignKeys"), DesignKeys.Get()}, {TEXT("CloseButton"), CloseButton.Get()}};
	for (const auto& Control : Required)
	{
		if (!IsValid(Control.Value))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("%s 缺少必需控件 %s。"), *GetClass()->GetPathName(), Control.Key);
			return false;
		}
	}
	SettingsController = &Controller;
	ClassicButton->OnClicked.AddUniqueDynamic(this, &UCastleControlSettingsWidget::SelectClassic);
	DesignButton->OnClicked.AddUniqueDynamic(this, &UCastleControlSettingsWidget::SelectDesign);
	CloseButton->OnClicked.AddUniqueDynamic(this, &UCastleControlSettingsWidget::CloseSettings);
	Controller.OnControlModeChanged.AddUObject(this, &UCastleControlSettingsWidget::RefreshMode);
	RefreshMode(Controller.GetControlMode());
	return true;
}

void UCastleControlSettingsWidget::RefreshMode(ECastleControlMode Mode)
{
	// 两套说明始终显示，文字中的物理键直接读取玩法使用的映射表。
	const auto FormatKeys = [](ECastleControlMode Preset, const FText& LookText)
	{
		const auto& Keys = CastleControlInput::GetBindings(Preset);
		return FText::Format(INVTEXT("{0} 放置 / 交互 · {1} 拆除\n{2}"),
			CastleControlInput::GetKeyDisplayText(Keys.Get(ECastleWorldInputAction::PrimaryInteract)),
			CastleControlInput::GetKeyDisplayText(Keys.Get(ECastleWorldInputAction::Demolish)), LookText);
	};
	const FText HoldKey = CastleControlInput::GetKeyDisplayText(
		CastleControlInput::GetBindings(ECastleControlMode::Design).Get(ECastleWorldInputAction::HoldCameraLook));
	ClassicKeys->SetText(FormatKeys(ECastleControlMode::Classic,
		INVTEXT("移动鼠标转动视角 · Space/Ctrl 升降 · E 空岛交互")));
	DesignKeys->SetText(FormatKeys(ECastleControlMode::Design,
		FText::Format(INVTEXT("按住{0}转动视角 · Q 下降 / E 上升"), HoldKey)));
	ClassicButton->SetIsEnabled(Mode != ECastleControlMode::Classic);
	DesignButton->SetIsEnabled(Mode != ECastleControlMode::Design);
}

void UCastleControlSettingsWidget::SelectClassic()
{
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Button);
	if (auto* Controller = SettingsController.Get()) { Controller->SetControlMode(ECastleControlMode::Classic); }
}

void UCastleControlSettingsWidget::SelectDesign()
{
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Button);
	if (auto* Controller = SettingsController.Get()) { Controller->SetControlMode(ECastleControlMode::Design); }
}

void UCastleControlSettingsWidget::CloseSettings()
{
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Button);
	// 与齿轮和 O 键复用关闭入口，让控制器按当前预设恢复输入焦点。
	if (auto* Controller = SettingsController.Get())
	{
		Controller->ToggleControlSettings();
		return;
	}
	UE_LOG(LogMoving_Castle, Error, TEXT("%s 无法关闭：设置控制器已失效。"), *GetPathName());
}

FReply UCastleControlSettingsWidget::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
	// WBP 的 Canvas 只留下设置面板可命中。面板内部的标题或留白仍属于设置界面。
	return FReply::Handled();
}

void UCastleControlSettingsWidget::NativeDestruct()
{
	if (auto* Controller = SettingsController.Get()) { Controller->OnControlModeChanged.RemoveAll(this); }
	SettingsController.Reset();
	Super::NativeDestruct();
}
