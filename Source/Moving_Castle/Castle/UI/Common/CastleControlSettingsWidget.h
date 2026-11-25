// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "Castle/Input/CastleControlMode.h"
#include "CastleControlSettingsWidget.generated.h"

class ACastlePlayerController;
class UButton;
class UTextBlock;

/** 左下角的操作预设面板。模式及保存状态都由控制器持有。 */
UCLASS()
class MOVING_CASTLE_API UCastleControlSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	bool BindController(ACastlePlayerController& Controller);

protected:
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& Event) override;

private:
	void RefreshMode(ECastleControlMode Mode);
	UFUNCTION() void SelectClassic();
	UFUNCTION() void SelectDesign();
	UFUNCTION() void CloseSettings();

	UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> ClassicButton;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> DesignButton;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ClassicKeys;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> DesignKeys;
	/** 编辑器可加载未生成的旧资产，正式面板挂载仍由 BindController 严格检查。 */
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CloseButton;
	TWeakObjectPtr<ACastlePlayerController> SettingsController;
};
