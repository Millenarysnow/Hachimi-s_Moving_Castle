// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/DragDropOperation.h"
#include "CastleGameplayTypes.h"
#include "CoreMinimal.h"
#include "CastleShopSellDragDropOperation.generated.h"

/**
 * 备战席拖拽出售操作的轻量载荷，只保存槽位索引、只读建筑描述与已报价的出售收益。
 * 实际出售仍必须经由 UCastleShopComponent::TrySellBackpackItem 校验和提交。
 */
UCLASS()
class MOVING_CASTLE_API UCastleShopSellDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	/** 被拖动的背包槽位索引（0～9）；无效槽位由商店组件拒绝。 */
	UPROPERTY(BlueprintReadOnly, Category = "移动城堡|商店")
	int32 BackpackSlotIndex = INDEX_NONE;

	/** 被拖动卡牌的只读建筑描述，供拖拽预览和出售区展示。 */
	UPROPERTY(BlueprintReadOnly, Category = "移动城堡|商店")
	FCastleBuildingDescriptor Descriptor;

	/** 起拖时从商店定价策略获取的出售收益展示值；实际入账仍以提交时商店校验为准。 */
	UPROPERTY(BlueprintReadOnly, Category = "移动城堡|商店")
	int32 QuotedSellPrice = 0;
};
