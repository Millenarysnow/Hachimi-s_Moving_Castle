// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CastleDirectorWidgets.generated.h"
class UTextBlock;
class UButton;
class UCastleDirectorSubsystem;

/** HUD 的布局完全来自 WBP；C++ 只读取导演状态并更新绑定文本。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleDirectorHUDWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 /** 实例绑定校验供导演启动使用；缺控件不继续运行。 */
 bool HasRequiredBindings() const;
protected:
 virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> SummaryText;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> StatusText;
};

/** 奖励面板只提交三种固定意图，所有成长和交易由导演原子执行。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleRewardChoiceWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 /** 实例绑定校验供导演启动使用；缺控件不继续运行。 */
 bool HasRequiredBindings() const;
protected:
 virtual void NativeOnInitialized() override;
 virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> ExperienceButton;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> HealingButton;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> ShopButton;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ExperienceLabel;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> HealingLabel;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ShopLabel;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> FailureText;
 UFUNCTION() void ChooseExperience();
 UFUNCTION() void ChooseHealing();
 UFUNCTION() void ChooseShop();
};

/** 暂停时仍由 Slate 处理重新开始按钮；蓝图只保存外观和布局。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleGameOverWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 /** 实例绑定校验供导演启动使用；缺控件不继续运行。 */
 bool HasRequiredBindings() const;
protected:
 virtual void NativeOnInitialized() override;
 virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> SummaryText;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> FailureText;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> RestartButton;
 UFUNCTION() void Restart();
};
