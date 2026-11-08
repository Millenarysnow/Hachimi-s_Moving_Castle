// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CastleGameplayTypes.h"
#include "CastleBackpackWidget.generated.h"

class ACastleBoard;
class UCastleBuildingCardWidget;
class UHorizontalBox;
class USizeBox;
class UTextBlock;

/**
 * 原型背包的屏幕空间展示。
 *
 * 当前只负责把棋盘上的原型背包内容绘制成固定容量卡片，并展示卡片中央的
 * 三维线框预览。鼠标、拖放和放置行为留给后续交互步骤，本组件对鼠标命中
 * 设置为不可命中，因此不会改变当前中心指针操作。
 */
UCLASS()
class MOVING_CASTLE_API UCastleBackpackWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	/** 创建背包面板和卡片容器。 */
	virtual void NativeConstruct() override;

	/** 检查棋盘背包是否变化，变化后重建卡片内容。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 查找当前世界的逻辑棋盘。 */
	void FindActiveBoard();

	/** 从棋盘原型背包刷新卡片和容量文本。 */
	void RefreshFromBackpack();

	/** 比较两份建筑描述数组，避免每帧重建 UMG 子树。 */
	static bool AreDescriptorsEqual(const TArray<FCastleBuildingDescriptor>& Left, const TArray<FCastleBuildingDescriptor>& Right);

	/** 顶部容量文本。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CapacityText;

	/** 横向卡片容器。 */
	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> CardBox;

	/** 根尺寸盒，用于根据背包容量稳定面板宽度。 */
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RootSizeBox;

	/** 当前世界的逻辑棋盘。 */
	UPROPERTY(Transient)
	TObjectPtr<ACastleBoard> ActiveBoard;

	/** 当前已经展示过的背包内容副本。 */
	UPROPERTY(Transient)
	TArray<FCastleBuildingDescriptor> CachedItems;

	/** 当前卡片实例，销毁旧内容时由 UMG 层级统一回收。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCastleBuildingCardWidget>> CardWidgets;

	/** 是否已经执行过至少一次背包同步。 */
	bool bHasSynchronizedBackpack = false;

	/** 选中槽位或容量变化也需要刷新，即使卡牌描述数组相同。 */
	int32 CachedSelection = INDEX_NONE;
	int32 CachedCapacity = INDEX_NONE;
};
