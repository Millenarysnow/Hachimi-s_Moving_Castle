// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CastleDirectorWidgets.generated.h"
class UTextBlock;
class UButton;
class UImage;
class UWidgetSwitcher;
class UCastleDirectorSubsystem;
class UCastleMainTowerComponent;

/** 屏幕空间节点标签的纯表现基类；文字和颜色来自节点 Actor 的 C++ 状态。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleDirectorNodeLabelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 验证正式 WBP 已绑定文本控件，缺失时节点生成失败。 */
	bool HasRequiredBindings() const;
	/** 设置普通或精英节点的简体中文名称和颜色。 */
	bool SetNodeLabel(const FText& Text, const FLinearColor& Color);
	/** 读取已绑定标签的实际文字和颜色；绑定失效时记录错误并返回失败。 */
	bool TryGetNodeLabel(FText& OutText, FLinearColor& OutColor) const;

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> LabelText;
};

/** HUD 的布局完全来自 WBP；C++ 只读取导演状态并更新绑定文本。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleDirectorHUDWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 /** 实例绑定校验供导演启动使用；缺控件不继续运行。 */
 bool HasRequiredBindings() const;
  /** 挂载前订阅本局主塔并立即显示生命，受击或回血时不等待 Slate Tick。 */
  bool BindMainTowerHealth(UCastleMainTowerComponent* MainTower);
protected:
 virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
  /** 在本局控件离开视口时解除对主塔的订阅。 */
  virtual void NativeDestruct() override;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> SummaryText;
 UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> StatusText;
  /** 生命行由 WBP 排版，专用绑定避免在一段摘要文本里对齐图标。 */
  UPROPERTY(meta=(BindWidget)) TObjectPtr<UImage> HeartIcon;
  UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> TowerHealthText;
private:
  /** HUD 只监听导演选择的主塔，不保存第二份生命，也不延长塔组件生命周期。 */
  TWeakObjectPtr<UCastleMainTowerComponent> HealthSource;
  UFUNCTION() void HandleTowerHealthChanged(float PreviousHealth, float CurrentHealth, float Damage);
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
  /** WBP 定义两张切图及其尺寸；顺序为普通、按下，整行仍是一个按钮。 */
  UPROPERTY(meta=(BindWidget)) TObjectPtr<UWidgetSwitcher> ShopIconStates;
  UPROPERTY(meta=(BindWidget)) TObjectPtr<UImage> HealingIcon;
  UFUNCTION() void ShowShopPressedIcon();
  UFUNCTION() void ShowShopNormalIcon();
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
  /** 结束界面在暂停时也通过 Slate 按下/释放事件切换重试切图，不依赖游戏 Tick。 */
  UPROPERTY(meta=(BindWidget)) TObjectPtr<UWidgetSwitcher> RestartIconStates;
  UPROPERTY(meta=(BindWidget)) TObjectPtr<UImage> SkullIcon;
  UFUNCTION() void ShowRestartPressedIcon();
  UFUNCTION() void ShowRestartNormalIcon();
 UFUNCTION() void Restart();
};
