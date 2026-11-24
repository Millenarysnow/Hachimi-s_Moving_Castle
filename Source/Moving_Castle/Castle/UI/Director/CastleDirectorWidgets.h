// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Castle/Director/CastleDirectorTypes.h"
#include "CastleDirectorWidgets.generated.h"

class ACastlePatrolRoute;
class UButton;
class UCastleDirectorSubsystem;
class UCastleHUDIconButtonWidget;
class UCastleIconBarWidget;
class UCastleMainTowerComponent;
class UCastleShopComponent;
class UImage;
class UTextBlock;
class UWidgetSwitcher;

/** 主游戏 HUD 的三种工作界面，与美术三张界面草图一一对应。 */
UENUM(BlueprintType)
enum class ECastleHUDScreenMode : uint8
{
	/** 建造页面：顶部主塔血条 + 底部建造栏 + 左下通用按钮 + 右下撤回/重做/商店。 */
	Build = 0,
	/** 路径编辑界面（按 P 进入）：顶部主塔血条 + 左下通用按钮 + 右下撤回/重做/删除。 */
	PathEdit = 1,
	/** 战斗界面：顶部主塔血条 + 底部骷髅击杀进度条 + 左下通用按钮 + 右下暂停/倍速/重试。 */
	Battle = 2
};

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

/**
 * 主游戏 HUD 的纯 C++ 逻辑父类；WBP 完全控制排版、锚点、尺寸和贴图。
 * 统一承载三界面常驻的顶部主塔 20 心血条、左下通用按钮、战斗底部 20 骷髅进度条以及右下三态动作栏。
 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleDirectorHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 实例绑定校验供导演启动使用；任一必需控件或切图缺失时报错并阻止启动。 */
	bool HasRequiredBindings() const;

	/** 挂载前订阅本局主塔、导演与路径编辑入口并立即同步一次表现。 */
	bool BindMainTowerHealth(UCastleMainTowerComponent* MainTower);
	/** 教学导演发布商店权限，正式导演的开店规则照常处理。 */
	void SetTutorialShopButtonEnabled(bool bEnabled);

	/** 根据主塔真实生命计算 20 格心形点亮数量（满血 20、存活至少 1、归零 0）；非法值返回 false。 */
	static bool ComputeFilledHearts(float CurrentHealth, float MaxHealth, int32& OutFilledCount);

	/** 根据战斗已击杀数与总预算计算 20 格骷髅点亮数量（开战 0、击杀至少 1、清场 20）；非法值返回 false。 */
	static bool ComputeFilledSkulls(int32 DefeatedCount, int32 TotalCount, int32& OutFilledCount);

	/** 查询当前 HUD 所处界面模式、已点亮心形数与骷髅数，供自动化测试核对。 */
	ECastleHUDScreenMode GetCurrentScreenMode() const { return CurrentScreenMode; }
	int32 GetFilledHeartCount() const;
	int32 GetFilledSkullCount() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	/** 离开视口时解除主塔、导演和路径编辑入口的全部事件订阅。 */
	virtual void NativeDestruct() override;

	/** 顶部居中常驻主塔血条（20 颗心，三界面持续显示）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarWidget> MainTowerHealthBar;

	/** 战斗界面底部居中的敌人击杀进度条（20 个骷髅，仅战斗界面显示）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarWidget> BattleProgressBar;

	/** 三界面共用的左下角按钮组：攻略（?）、设置（齿轮）、主菜单（房子）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> GuideButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> SettingsButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> MainMenuButton;

	/** 右下角三态切换容器：0=建造页、1=路径编辑页、2=战斗页。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> RightActionSwitcher;

	/** 建造页面右下角按钮：撤回（纯 UI）、重做（纯 UI 灰态）、商店（接入开关）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> BuildUndoButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> BuildRedoButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> BuildShopButton;

	/** 路径编辑界面右下角按钮：撤回（接 UndoDraftPoint）、重做（纯 UI 灰态）、删除（接 ClearPatrolRoute）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> PathUndoButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> PathRedoButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> PathDeleteButton;

	/** 战斗界面右下角按钮：暂停/继续、1x/2x 倍速、重试。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> BattlePauseButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> BattleSpeedButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleHUDIconButtonWidget> BattleRetryButton;

private:
	/** 查找并订阅当前世界唯一的路径编辑入口。 */
	void EnsurePatrolRouteBinding();
	/** 依据导演阶段与路径编辑状态计算并同步当前界面模式。 */
	void SyncScreenMode();
	/** 同步战斗暂停与倍速按钮的双态贴图。 */
	void SyncBattleControlButtons();
	/** 同步战斗底部的 20 格骷髅击杀进度条。 */
	void SyncBattleProgress();

	UFUNCTION()
	void HandleTowerHealthChanged(float PreviousHealth, float CurrentHealth, float Damage);

	UFUNCTION()
	void HandleShopVisibilityChanged();

	void HandleDirectorPhaseChanged(ECastleDirectorPhase NewPhase);
	void HandleBattleControlStateChanged();
	void HandlePatrolEditingChanged(bool bIsEditing);

	UFUNCTION()
	void HandleGuideClicked();
	UFUNCTION()
	void HandleSettingsClicked();
	UFUNCTION()
	void HandleMainMenuClicked();
	UFUNCTION()
	void HandleBuildUndoClicked();
	UFUNCTION()
	void HandleBuildRedoClicked();
	UFUNCTION()
	void HandleBuildShopClicked();
	UFUNCTION()
	void HandlePathUndoClicked();
	UFUNCTION()
	void HandlePathRedoClicked();
	UFUNCTION()
	void HandlePathDeleteClicked();
	UFUNCTION()
	void HandleBattlePauseClicked();
	UFUNCTION()
	void HandleBattleSpeedClicked();
	UFUNCTION()
	void HandleBattleRetryClicked();

	/** HUD 只监听主塔、导演与路径入口，不保存第二份游戏状态。 */
	TWeakObjectPtr<UCastleMainTowerComponent> HealthSource;
	TWeakObjectPtr<UCastleDirectorSubsystem> BoundDirector;
	TWeakObjectPtr<ACastlePatrolRoute> BoundPatrolRoute;
	TWeakObjectPtr<UCastleShopComponent> BoundShop;
	FDelegateHandle DirectorPhaseHandle;
	FDelegateHandle BattleControlHandle;
	FDelegateHandle PatrolEditingHandle;
	ECastleHUDScreenMode CurrentScreenMode = ECastleHUDScreenMode::Build;
	bool bButtonsBound = false;
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
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ExperienceButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> HealingButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ShopButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ExperienceLabel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> HealingLabel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ShopLabel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> FailureText;
	/** WBP 定义两张切图及其尺寸；顺序为普通、按下，整行仍是一个按钮。 */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidgetSwitcher> ShopIconStates;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> HealingIcon;
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
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SummaryText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> FailureText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> RestartButton;
	/** 结束界面在暂停时也通过 Slate 按下/释放事件切换重试切图，不依赖游戏 Tick。 */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidgetSwitcher> RestartIconStates;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> SkullIcon;
	UFUNCTION() void ShowRestartPressedIcon();
	UFUNCTION() void ShowRestartNormalIcon();
	UFUNCTION() void Restart();
};
