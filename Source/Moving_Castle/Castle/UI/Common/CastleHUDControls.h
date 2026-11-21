// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "Containers/StaticArray.h"
#include "CoreMinimal.h"
#include "CastleHUDControls.generated.h"

class UButton;
class UImage;
class UWidgetSwitcher;

/** 按钮控件的切图状态模式；不同按钮在 WBP 中的 IconStates 子项数量必须严格匹配。 */
UENUM(BlueprintType)
enum class ECastleHUDButtonPresentationMode : uint8
{
	/** 普通单态按钮：IconStates 固定包含 [0=普通, 1=按下]。 */
	Standard,
	/** 支持不可用态的按钮（如复原/重做）：IconStates 固定包含 [0=普通, 1=按下, 2=不可用]。 */
	WithDisabledState,
	/** 双状态切换按钮（如暂停/继续、一倍速/二倍速）：IconStates 固定包含 [0=状态A普通, 1=状态A按下, 2=状态B普通, 3=状态B按下]。 */
	TwoStateToggle
};

/** 图标按钮被点击时的无参通知；只有在按钮处于可用态时触发。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCastleHUDIconButtonClicked);

/** HUD 公共控件辅助函数，统一校验 WBP 笔刷资源和绘制尺寸，避免静态辅助在 Unity 编译下重名。 */
namespace CastleHUDControls
{
	/** 检查 UImage 是否绑定了有效贴图且具备非零绘制尺寸与合法绘制类型；不补占位图。 */
	MOVING_CASTLE_API bool HasValidTextureBrush(const UImage* Image);
}

/**
 * 20 格图标进度条的单个槽位基类（用于主塔心形血条与战斗骷髅进度条）。
 * WBP 控制尺寸与两张贴图（0=空态、1=点亮态），C++ 只校验资源并切换状态。
 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleIconBarSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 校验 IconStates 是否准确包含 2 张有效贴图（空态、点亮态）；缺图或零尺寸时输出 Error。 */
	bool ValidatePresentation() const;

	/** 切换当前槽位为点亮（true）或空态（false）；绑定无效时报错并返回失败。 */
	bool SetFilled(bool bInFilled);

	/** 返回当前槽位是否点亮。 */
	bool IsFilled() const { return bFilled; }

protected:
	/** 固定 2 项切换器：索引 0 为 Empty 图标，索引 1 为 Filled 图标。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> IconStates;

private:
	bool bFilled = false;
};

/**
 * 固定 20 格横向图标进度条基类（WBP_HeartBar 与 WBP_SkullBar 共享同一 C++ 父类）。
 * 槽位间距、尺寸与居中排版全部由 WBP 设计器控制，C++ 只做严格校验和比例计数同步。
 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleIconBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 美术与策划约定的固定单排图标总数。 */
	static constexpr int32 FixedIconCount = 20;

	/** 校验全部 20 个槽位及其空/亮贴图是否有效；任意缺失立即输出 Error 并返回 false。 */
	bool ValidatePresentation() const;

	/** 设置当前从左到右点亮的图标数量（合法范围 0～20）；越界或未通过校验时报错返回 false。 */
	bool SetFilledCount(int32 NewFilledCount);

	/** 读取当前已点亮的图标数量；未初始化前为 INDEX_NONE。 */
	int32 GetFilledCount() const { return FilledCount; }

	/** 返回固定槽位数（20）。 */
	int32 GetSlotCount() const { return FixedIconCount; }

protected:
	/** 20 个固定命名槽位，由 WBP 在编译期验证名称与类型，禁止运行时动态拼树。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot01;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot02;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot03;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot04;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot05;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot06;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot07;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot08;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot09;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot10;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot11;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot12;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot13;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot14;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot15;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot16;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot17;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot18;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot19;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleIconBarSlotWidget> Slot20;

private:
	/** 按从左到右顺序返回 20 个槽位，校验与渲染共用同一顺序。 */
	TStaticArray<UCastleIconBarSlotWidget*, FixedIconCount> GetSlots() const;

	int32 FilledCount = INDEX_NONE;
};

/**
 * 统一图标按钮基类：承载按压反馈、禁用态切图（如无复原步骤）与双态切换（如暂停/继续、1x/2x）。
 * WBP 控制按钮尺寸、位置与各态切图，C++ 只绑定事件并管理切换索引。
 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleHUDIconButtonWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 按期望模式校验 ActionButton 与 IconStates 的全部贴图；不匹配时报错返回 false。 */
	bool ValidatePresentation(ECastleHUDButtonPresentationMode ExpectedMode) const;

	/** 配置按钮的展示模式并刷新当前图标；应在父级 HUD 初始化阶段调用。 */
	bool ConfigurePresentationMode(ECastleHUDButtonPresentationMode InMode);

	/** 切换按钮是否可用；WithDisabledState 模式下禁用时展示索引 2 的灰态切图。 */
	void SetActionEnabled(bool bInEnabled);

	/** 切换双态按钮的第二状态（例如暂停态/二倍速态）；仅 TwoStateToggle 模式生效。 */
	void SetAlternateState(bool bInAlternate);

	/** 返回当前是否处于第二状态。 */
	bool IsAlternateState() const { return bAlternateState; }

	/** 返回当前按钮是否启用。 */
	bool IsActionEnabled() const { return bActionEnabled; }

	/** 返回当前激活的 IconStates 索引，便于自动化测试直接核对表现状态。 */
	int32 GetActiveIconIndex() const;

	/** 按钮有效点击通知，由父 HUD 接入具体业务入口。 */
	UPROPERTY(BlueprintAssignable, Category = "移动城堡|HUD")
	FCastleHUDIconButtonClicked OnButtonClicked;

protected:
	/** 绑定内部按钮的点击、按下、释放事件，并禁止抢占键盘焦点。 */
	virtual void NativeOnInitialized() override;

	/** 承载 Slate 点击/按压事件的底层按钮，背景在 WBP 中设为透明。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ActionButton;

	/** 按模式存放普通/按下/不可用/第二态切图的切换器。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> IconStates;

private:
	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandlePressed();

	UFUNCTION()
	void HandleReleased();

	/** 根据模式、启用态、第二态与按压态计算唯一目标索引并应用。 */
	void RefreshIconState();

	ECastleHUDButtonPresentationMode PresentationMode = ECastleHUDButtonPresentationMode::Standard;
	bool bActionEnabled = true;
	bool bAlternateState = false;
	bool bPressed = false;
};
