// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "Containers/StaticArray.h"
#include "CoreMinimal.h"
#include "CastleShopWidget.generated.h"

class UBorder;
class UButton;
class UCastleCardPortraitSubsystem;
class UCastleEconomyComponent;
class UCastleShopBenchSlotWidget;
class UCastleShopCardDetailWidget;
class UCastleShopCardWidget;
class UCastleShopComponent;
class UCastleShopOfferSlotWidget;
class UImage;
class UOverlay;
class UProgressBar;
class UTextBlock;
class UWidget;
class UWidgetSwitcher;
struct FCastleBuildingDescriptor;

/**
 * 货币战争风格的房间商店界面抽象父类（WBP_RoomShop 控制排版、锚点、尺寸与贴图）：
 * - 顶部轻量化经济与主塔容量状态栏 + 箔光对照 + 按住透视城堡 + 关闭按钮；
 * - 中部大面积透景展示 3D 立体城堡，左侧随悬停淡入建筑机制浮卡，中部上方展示轻量化操作 Toast；
 * - 底部一体化操作台：10 格备战席（支持左键拖拽至下方出售区变卖、右键快捷出售）+ 左翼主塔容量与刷新 + 中央 6 张 3D 建筑商品卡 + 右翼继续建造与回收区。
 * 所有交易、报价与容量校验均由 UCastleShopComponent 和 UCastleEconomyComponent 执行，本类不复制任何买卖算法。
 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleShopWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 固定 6 个商品槽位与 10 个备战席槽位，与策划案及后端组件保持一致。 */
	static constexpr int32 FixedOfferCount = 6;
	static constexpr int32 FixedBenchCount = 10;

	/** 严格校验全部必需 BindWidget、子槽位及贴图资源；缺失任意项即报错并返回 false。 */
	bool ValidatePresentation() const;

	/** 返回当前 6 个商品槽内嵌的 3D 卡牌控件，供外部检查与自动化测试读取。 */
	TArray<UCastleShopCardWidget*> GetOfferCards() const;

	/** 返回全部 6 个商品槽位，便于自动化测试验证状态。 */
	TStaticArray<UCastleShopOfferSlotWidget*, FixedOfferCount> GetOfferSlots() const;

	/** 返回全部 10 个备战席槽位，便于自动化测试验证状态。 */
	TStaticArray<UCastleShopBenchSlotWidget*, FixedBenchCount> GetBenchSlots() const;

	/** 查询当前是否正处于拖拽卖卡的出售遮罩显示状态。 */
	bool IsSellDropOverlayVisible() const;

	/** 查询当前是否处于按住透视城堡状态。 */
	bool IsPeekingCastle() const { return bPeekingCastle; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

	// ===== 顶部状态栏 =====
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> TopBarBg;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> TopCoinIcon;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> GoldValueText;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> TopTowerIcon;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TowerRoomSummaryText;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> WavePhaseText;

	/** 保持名称 FoilPreview，延续既有箔光对照按钮及自动化测试约定。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> FoilPreview;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> FoilLabel;

	/** 按住临时隐藏底部操作台以无遮挡观察 3D 城堡，松手恢复。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> PeekButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> PeekIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> TopCloseButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> TopCloseIcon;

	// ===== 中部透景区浮层 =====
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopCardDetailWidget> CardDetailPanel;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> ToastContainer;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusText;

	// ===== 备战席（10 格背包区） =====
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> BenchSection;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> BenchBg;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BenchHeaderText;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopBenchSlotWidget> BenchSlot01;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopBenchSlotWidget> BenchSlot02;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopBenchSlotWidget> BenchSlot03;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopBenchSlotWidget> BenchSlot04;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopBenchSlotWidget> BenchSlot05;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopBenchSlotWidget> BenchSlot06;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopBenchSlotWidget> BenchSlot07;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopBenchSlotWidget> BenchSlot08;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopBenchSlotWidget> BenchSlot09;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopBenchSlotWidget> BenchSlot10;

	// ===== 底部主操作台（左翼 + 6 商品卡座 + 拖拽出售遮罩 + 右翼） =====
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidget> DockSection;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> DockBg;

	// 左翼：主塔成长与刷新
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> LeftTowerIcon;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TowerLevelText;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> TowerExpBar;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RoomLimitText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RefreshButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> RefreshBgStates;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> RefreshIcon;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RefreshLabel;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RefreshCostText;

	// 中央：6 个固定商品卡座
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopOfferSlotWidget> OfferSlot01;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopOfferSlotWidget> OfferSlot02;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopOfferSlotWidget> OfferSlot03;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopOfferSlotWidget> OfferSlot04;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopOfferSlotWidget> OfferSlot05;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCastleShopOfferSlotWidget> OfferSlot06;

	// 中央拖拽出售遮罩（拖拽备战席卡牌时覆盖 6 卡区显示）
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UOverlay> SellDropOverlay;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SellDropBg;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SellDropIcon;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SellDropTitleText;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SellDropPriceText;

	// 右翼：继续建造关闭按钮 + 常驻回收提示靶区
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> CloseBgStates;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CloseLabel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UOverlay> RightSellTarget;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> RightSellIcon;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RightSellText;

private:
	friend class UCastleShopCardWidget;

	/** 新卡被抓住时旧卡立即归位；共享动态纹理不能同时交给仍在回正的旧卡。 */
	void ActivateCardInspection(UCastleShopCardWidget& Card);
	void ReleaseCardInspection(const UCastleShopCardWidget& Card);
	void CancelOfferInspection();

	/** 同步商店、经济、导演与背包数据到 WBP。 */
	UFUNCTION()
	void RefreshShop();

	UFUNCTION()
	void HandleGoldChanged(int32 PreviousGold, int32 CurrentGold);

	UFUNCTION()
	void HandleRefresh();

	UFUNCTION()
	void HandleRefreshPressed();

	UFUNCTION()
	void HandleRefreshReleased();

	UFUNCTION()
	void HandleClose();

	UFUNCTION()
	void HandleClosePressed();

	UFUNCTION()
	void HandleCloseReleased();

	UFUNCTION()
	void HandleToggleFoil();

	UFUNCTION()
	void HandlePeekPressed();

	UFUNCTION()
	void HandlePeekReleased();

	void HandleOfferPurchaseRequested(int32 OfferIndex);
	void HandleOfferHovered(int32 OfferIndex, bool bHovered);
	void HandleBenchSellRequested(int32 BackpackSlotIndex);
	void HandleBenchHovered(int32 BackpackSlotIndex, bool bHovered);
	void HandleBenchDragStarted(int32 BackpackSlotIndex, const FCastleBuildingDescriptor& Descriptor, int32 SellPrice);
	void HandleBenchDragEnded();

	void HandleTrade(bool bSelling, int32 SlotIndex);
	void ShowResult(bool bSuccess, const FString& SuccessMessage, const FText& FailureReason);
	void RefreshHoverDetail();
	void RefreshTowerAndWaveHeader();
	void ApplyPeekVisibility();

	UPROPERTY(Transient)
	TObjectPtr<UCastleShopComponent> Shop;

	UPROPERTY(Transient)
	TObjectPtr<UCastleEconomyComponent> Economy;

	UPROPERTY(Transient)
	TObjectPtr<UCastleCardPortraitSubsystem> Portraits;

	TWeakObjectPtr<UCastleShopCardWidget> InspectedCard;

	bool bPresentationValid = false;
	bool bControlsBound = false;
	bool bFoilPreview = false;
	bool bPeekingCastle = false;
	bool bDraggingBenchSell = false;
	int32 HoveredOfferIndex = INDEX_NONE;
	int32 HoveredBenchIndex = INDEX_NONE;
	float ToastRemainingSeconds = 0.0f;
};
