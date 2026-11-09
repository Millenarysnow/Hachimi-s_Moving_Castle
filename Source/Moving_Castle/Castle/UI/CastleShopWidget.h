// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "CastleShopWidget.generated.h"

class UButton;
class UCastleEconomyComponent;
class UCastleShopComponent;
class UHorizontalBox;
class UTextBlock;
class UVerticalBox;

/** UE 5.8 的按钮焦点只能在构造阶段配置，统一在此关闭，保留游戏按键入口。 */
UCLASS()
class MOVING_CASTLE_API UCastleShopButton : public UButton
{
	GENERATED_BODY()

public:
	/** 通过引擎提供的构造期接口初始化焦点属性，不直接访问已废弃的成员。 */
	UCastleShopButton();
};

/** 同一种按钮支持购买或出售，通过明确槽位传递交易意图。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCastleShopTradeRequested, bool, bSelling, int32, SlotIndex);

/** 带槽位上下文的小按钮，使 UMG 的无参 OnClicked 能安全关联具体商品。 */
UCLASS()
class MOVING_CASTLE_API UCastleShopActionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 配置交易方向、槽位、标价和可用状态；此控件不直接改背包和金币。 */
	void Configure(bool bInSelling, int32 InIndex, const FText& Label, bool bEnabled);

	/** 父面板监听请求，再交给商店组件执行完整交易。 */
	UPROPERTY()
	FCastleShopTradeRequested OnTradeRequested;

protected:
	/** 创建固定大小的按钮和居中文本，禁用键盘焦点以保留 B 键关闭入口。 */
	virtual void NativeOnInitialized() override;

	UFUNCTION()
	void HandleClicked();

	/** 子控件引用需要 UPROPERTY，防止在交易刷新和 GC 之间失效。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> Button;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LabelText;

	bool bSelling = false;
	int32 Index = INDEX_NONE;
};

/**
 * 商店的纯 C++ 测试界面：复用现有建筑卡片和线框预览，增加价格、余额和操作。
 * 界面订阅逻辑事件，不自行随机、不自行计算价格，也不持有独立金币副本。
 */
UCLASS()
class MOVING_CASTLE_API UCastleShopWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	/** 创建可随视口缩放的居中面板，不依赖新的 Widget Blueprint 或美术资产。 */
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 测试阶段保留明确的键盘操作，与点击按钮调用同一交易入口。 */
	virtual FReply NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** 所有事件最终同步相同的数据源，确保按钮价格与扣费一致。 */
	UFUNCTION()
	void RefreshShop();

	UFUNCTION()
	void HandleGoldChanged(int32 PreviousGold, int32 CurrentGold);

	UFUNCTION()
	void HandleTrade(bool bSelling, int32 SlotIndex);

	UFUNCTION()
	void HandleRefresh();

	UFUNCTION()
	void HandleClose();

	/** 结果保留在面板内，并写日志，金币不足或满包都有直接可见反馈。 */
	void ShowResult(bool bSuccess, const FString& SuccessMessage, const FText& FailureReason);

	/** 运行时从拥有者控制器取得的逻辑依赖。 */
	UPROPERTY(Transient)
	TObjectPtr<UCastleShopComponent> Shop;

	UPROPERTY(Transient)
	TObjectPtr<UCastleEconomyComponent> Economy;

	/** 可变文本和容器；固定布局只在初始化时创建一次。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SummaryText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RefreshLabel;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> OfferRow;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> SellRows;

	UPROPERTY(Transient)
	TObjectPtr<UButton> RefreshButton;

	bool bWidgetTreeBuilt = false;
};
