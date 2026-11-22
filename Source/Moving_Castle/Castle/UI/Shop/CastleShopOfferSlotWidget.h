// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "CastleShopOfferSlotWidget.generated.h"

class ACastleBoard;
class UBorder;
class UButton;
class UCastleShopCardWidget;
class UCastleShopWidget;
class UImage;
class UOverlay;
class USizeBox;
class UTextBlock;
class UWidgetSwitcher;
struct FCastleShopOffer;

/** 商品槽位发起购买请求（可由短点卡面或点击底部价格条触发）。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCastleShopOfferPurchaseRequested, int32);
/** 商品槽位悬停状态变化，通知商店展示或隐藏左侧建筑机制详情浮卡。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCastleShopOfferHovered, int32, bool);

/**
 * 货币战争风格商店的单个商品卡座：
 * 托管现有 UCastleShopCardWidget（3D 建筑取景、视差、透视倾斜与箔光），
 * 并在卡座外层提供玩法色顶条、一体化价格购买条、售罄印章与只读合成角标。
 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleShopOfferSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 严格校验必需 BindWidget 与所有切图资源；缺失时输出 Error 并返回失败。 */
	bool ValidatePresentation() const;

	/** 将真实商品数据同步到卡座与内嵌 3D 卡牌；所有报价和售罄态均只读外部数据源。 */
	bool ConfigureOffer(UCastleShopWidget& InShop, int32 InOfferIndex, const FCastleShopOffer& Offer,
		const ACastleBoard* Board, bool bCanAfford, bool bHasCapacity, bool bFoilPreview,
		int32 MatchingInBackpackCount);

	/** 同步当前卡面的箔光预览开关，不改变商品、报价或摄影缓存。 */
	void SetFoilPreview(bool bEnabled);

	/** 返回内嵌的 3D 建筑卡牌控件，供商店统一取消赏牌与自动化测试核对。 */
	UCastleShopCardWidget* GetCardWidget() const { return CardWidget; }

	/** 购买请求委托（方案 A：短点卡面或点击底部价格条均触发同一购买入口）。 */
	FCastleShopOfferPurchaseRequested OnOfferPurchaseRequested;

	/** 悬停通知委托，驱动商店左侧战术详情浮卡。 */
	FCastleShopOfferHovered OnOfferHovered;

protected:
	virtual void NativeOnInitialized() override;

	/** 卡座外框背景（T_Shop_CardPedestal，内部保持统一深黑蓝衬底以突出 5 种玩法色）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> PedestalBackground;

	/** 卡座顶部玩法色灯条，强化第一眼色号识别。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ColorAccentBar;

	/** 固定 162×226 的 3D 卡牌宿主容器；RetainerBox 留在卡牌内部，按钮留在外部。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> CardHost;

	/** 已售罄遮罩层（保留原卡座占位与压暗卡面，不重排槽位）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UOverlay> SoldOverlay;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SoldBanner;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SoldText;

	/** 只读合成提示角标：背包中存在同类型同色同号一级牌时显示“可合成”。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> MergeHintBadge;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MergeHintText;

	/** 底部一体化购买按钮（禁用键盘焦点，保留 1~6 / R / B 快捷键）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> PriceButton;

	/** 底部购买条背景三态切换器：[0=常态, 1=按下, 2=售罄禁用]。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> PriceButtonBg;

	/** 槽位快捷键编号（1～6）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> HotkeyText;

	/** 价格条金币图标（复用 T_UI_Coin）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> CoinIcon;

	/** 价格文本：可购为鎏金色、资金/容量不足为警示红、已售罄为灰态文字。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PriceText;

private:
	UFUNCTION()
	void HandlePriceButtonClicked();

	UFUNCTION()
	void HandlePriceButtonPressed();

	UFUNCTION()
	void HandlePriceButtonReleased();

	UFUNCTION()
	void HandlePriceButtonHovered();

	UFUNCTION()
	void HandlePriceButtonUnhovered();

	void HandleCardShortClicked(UCastleShopCardWidget& Card);
	void HandleCardHovered(UCastleShopCardWidget& Card, bool bInHovered);
	void RefreshPriceButtonBg();

	/** 复用单个 3D 卡牌实例，避免每次金币变化都销毁重建 RetainerBox。 */
	UPROPERTY(Transient)
	TObjectPtr<UCastleShopCardWidget> CardWidget;

	int32 OfferIndex = INDEX_NONE;
	bool bSoldState = false;
	bool bPricePressed = false;
	bool bCardHovered = false;
	bool bButtonHovered = false;
};
