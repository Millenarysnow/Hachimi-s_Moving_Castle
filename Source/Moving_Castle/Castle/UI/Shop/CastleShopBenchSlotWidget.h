// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleGameplayTypes.h"
#include "CoreMinimal.h"
#include "CastleShopBenchSlotWidget.generated.h"

class UCastleBuildingPreviewWidget;
class UImage;
class USizeBox;
class UTextBlock;

/** 备战席槽位直接请求出售（右键点击触发）。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCastleShopBenchSellRequested, int32);
/** 备战席槽位开始左键拖拽出售（通知商店在底部商品区展示回收出售遮罩）。 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FCastleShopBenchDragStarted, int32, const FCastleBuildingDescriptor&, int32);
/** 备战席槽位结束或取消拖拽出售（通知商店收起回收出售遮罩）。 */
DECLARE_MULTICAST_DELEGATE(FCastleShopBenchDragEnded);
/** 备战席槽位悬停变化，通知商店展示左侧详情浮卡。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCastleShopBenchHovered, int32, bool);

/**
 * 商店备战席单个槽位（共 10 格，对应玩家背包槽位 0～9）：
 * 复用 UCastleBuildingPreviewWidget 展示与建造栏同源的建筑轮廓，
 * 支持左键拖拽到下方出售区变卖、右键快捷出售以及悬停查看详情。
 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleShopBenchSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 严格校验必需 BindWidget 与贴图资源；缺失时输出 Error 并返回失败。 */
	bool ValidatePresentation() const;

	/** 同步槽位索引、背包卡牌及出售报价；空槽为合法正常态。 */
	bool ConfigureBenchSlot(int32 InSlotIndex, const FCastleBuildingDescriptor* Card, bool bCanQuoteSell,
		int32 InSellPrice, const FText& QuoteFailureReason);

	/** 右键快速出售请求。 */
	FCastleShopBenchSellRequested OnBenchSellRequested;

	/** 拖拽出售生命周期通知。 */
	FCastleShopBenchDragStarted OnBenchDragStarted;
	FCastleShopBenchDragEnded OnBenchDragEnded;

	/** 悬停通知。 */
	FCastleShopBenchHovered OnBenchHovered;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
		UDragDropOperation*& OutOperation) override;
	virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

	/** 有卡态槽位框（T_Shop_BenchSlot）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SlotNormalBg;

	/** 空槽态槽位框（T_Shop_BenchSlot_Empty）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SlotEmptyBg;

	/** 顶部玩法色色条；空槽隐藏。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ColorStrip;

	/** 正方形建筑预览宿主，复用 UCastleBuildingPreviewWidget。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> PreviewHost;

	/** 槽位快捷键编号（1～9、0）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> HotkeyText;

	/** 卡牌数字与等级标签（如“3 · Lv1”）；空槽隐藏。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CardBadgeText;

	/** 出售所得金币标签（如“+7”）；空槽隐藏，报价失败显示错误提示。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SellPriceText;

private:
	/** 缓存单个建筑预览控件，避免频繁刷新重建材质与渲染目标绑定。 */
	UPROPERTY(Transient)
	TObjectPtr<UCastleBuildingPreviewWidget> PreviewWidget;

	int32 SlotIndex = INDEX_NONE;
	FCastleBuildingDescriptor CachedDescriptor;
	int32 QuotedSellPrice = 0;
	bool bHasCard = false;
	bool bCanSell = false;
};
