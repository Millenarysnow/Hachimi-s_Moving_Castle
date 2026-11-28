// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastleSkyIslandTypes.h"
#include "CastleSkyIslandWeightsDataAsset.generated.h"

/** 奖励节点和房间传送门各自维护分布，零权重表示该入口永远抽不到此类型。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleSkyIslandWeightsDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "奖励节点") int32 RewardShopWeight = 2;
	UPROPERTY(EditAnywhere, Category = "奖励节点") int32 RewardNPCWeight = 1;
	UPROPERTY(EditAnywhere, Category = "奖励节点") int32 RewardGambleWeight = 1;

	UPROPERTY(EditAnywhere, Category = "房间传送门") int32 BonusShopWeight = 2;
	UPROPERTY(EditAnywhere, Category = "房间传送门") int32 BonusNPCWeight = 1;
	UPROPERTY(EditAnywhere, Category = "房间传送门") int32 BonusGambleWeight = 1;
	UPROPERTY(EditAnywhere, Category = "房间传送门") int32 BonusRestWeight = 1;

	/** 两组都要求非负且总权重为正。非法配置不消耗随机流。 */
	bool ValidateConfiguration(FText& OutFailureReason) const;
	bool TrySelectRewardSkyIsland(FRandomStream& Random, ECastleSkyIslandType& OutType, FText& OutFailureReason) const;
	bool TrySelectBonusSkyIsland(FRandomStream& Random, ECastleSkyIslandType& OutType, FText& OutFailureReason) const;

private:
	/** 固定按 Shop、NPC、Gamble、Rest 的顺序累计，奖励节点使用前三项。 */
	bool TrySelect(FRandomStream& Random, TConstArrayView<int32> Weights, ECastleSkyIslandType& OutType, FText& OutFailureReason) const;
};
