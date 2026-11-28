// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleSkyIslandWeightsDataAsset.h"
#include "Moving_Castle.h"
#include "Algo/AnyOf.h"

namespace
{
	/** 所有 int32 权重求和必须先扩位，四个合法大权重也能完整参与抽样。 */
	int64 SumWeights(const TConstArrayView<int32> Weights)
	{
		int64 Total = 0;
		for (const int32 Weight : Weights) { Total += Weight; }
		return Total;
	}
}

bool UCastleSkyIslandWeightsDataAsset::ValidateConfiguration(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	const int32 RewardWeights[] = {RewardShopWeight, RewardNPCWeight, RewardGambleWeight};
	const int32 BonusWeights[] = {BonusShopWeight, BonusNPCWeight, BonusGambleWeight, BonusRestWeight};
	const auto Validate = [&](const TCHAR* Entry, const TConstArrayView<int32> Weights)
	{
		if (SumWeights(Weights) > 0 && !Algo::AnyOf(Weights, [](int32 Weight) { return Weight < 0; })) { return true; }
		OutFailureReason = FText::FromString(FString::Printf(TEXT("空岛概率资产 %s 的%s权重无效，所有项须非负且总权重大于零。"),
			*GetPathName(), Entry));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
		return false;
	};
	return Validate(TEXT("奖励节点"), RewardWeights) && Validate(TEXT("房间传送门"), BonusWeights);
}

bool UCastleSkyIslandWeightsDataAsset::TrySelectRewardSkyIsland(FRandomStream& Random, ECastleSkyIslandType& OutType, FText& OutFailureReason) const
{
	const int32 Weights[] = {RewardShopWeight, RewardNPCWeight, RewardGambleWeight};
	return TrySelect(Random, Weights, OutType, OutFailureReason);
}

bool UCastleSkyIslandWeightsDataAsset::TrySelectBonusSkyIsland(FRandomStream& Random, ECastleSkyIslandType& OutType, FText& OutFailureReason) const
{
	const int32 Weights[] = {BonusShopWeight, BonusNPCWeight, BonusGambleWeight, BonusRestWeight};
	return TrySelect(Random, Weights, OutType, OutFailureReason);
}

bool UCastleSkyIslandWeightsDataAsset::TrySelect(FRandomStream& Random, const TConstArrayView<int32> Weights,
	ECastleSkyIslandType& OutType, FText& OutFailureReason) const
{
	if (!ValidateConfiguration(OutFailureReason)) { return false; }
	static constexpr ECastleSkyIslandType Types[] = {
		ECastleSkyIslandType::Shop, ECastleSkyIslandType::NPC, ECastleSkyIslandType::Gamble, ECastleSkyIslandType::Rest
	};
	// 用一个 32 位种子更新将样本映射到累计权重区间，整数算术保证平台间一致。
	// 总权重可超过 32 位，分解乘积以避免 64 位溢出。
	const uint64 Total = SumWeights(Weights);
	const uint64 Sample = Random.GetUnsignedInt();
	const uint64 Draw = Sample * (Total >> 32) + ((Sample * (Total & MAX_uint32)) >> 32);
	uint64 Prefix = 0;
	for (int32 Index = 0; Index < Weights.Num(); ++Index)
	{
		Prefix += Weights[Index];
		if (Draw < Prefix)
		{
			OutType = Types[Index];
			return true;
		}
	}
	OutFailureReason = FText::FromString(FString::Printf(TEXT("空岛概率资产 %s 的抽样结果超出累计权重。"), *GetPathName()));
	UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
	return false;
}
