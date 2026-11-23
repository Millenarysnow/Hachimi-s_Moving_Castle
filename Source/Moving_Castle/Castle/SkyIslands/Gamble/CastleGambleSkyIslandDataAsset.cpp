#include "CastleGambleSkyIslandDataAsset.h"

#include "CastleBuildingDefinition.h"
#include "Items/CastleBadgeDataAsset.h"
#include "Items/CastleRoomGroupDataAsset.h"
#include "Moving_Castle.h"

namespace
{
	/** 抽中一段后保留段内位置，使同色条纹在该段内等概率。 */
	bool TrySelectWeight(TConstArrayView<double> Weights, const double Sample, int32& OutIndex, double& OutFraction)
	{
		if (!FMath::IsFinite(Sample) || Sample < 0.0 || Sample >= 1.0) { return false; }
		double Total = 0.0;
		for (const double Weight : Weights)
		{
			if (Weight < 0.0 || !FMath::IsFinite(Weight)) { return false; }
			Total += Weight;
		}
		if (Total <= 0.0 || !FMath::IsFinite(Total)) { return false; }

		// 用归一化区间比较，使最后一段上界严格为 1，避免接近 1 的样本因舍入落在表外。
		double Cumulative = 0.0;
		for (int32 Index = 0; Index < Weights.Num(); ++Index)
		{
			const double Start = Cumulative / Total;
			Cumulative += Weights[Index];
			const double End = Cumulative / Total;
			if (Sample < End)
			{
				OutIndex = Index;
				OutFraction = (Sample - Start) / (End - Start);
				return true;
			}
		}
		return false;
	}
}

bool UCastleGambleSkyIslandDataAsset::Validate(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	const auto Reject = [this, &OutReason](const FText& Reason)
	{
		OutReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("赌博空岛配置 %s：%s"), *GetPathName(), *Reason.ToString());
		return false;
	};
	const int64 RouletteCount = static_cast<int64>(RouletteBlackSlots) + RouletteRedSlots + RouletteGreenSlots;
	if (RouletteBlackSlots < 1 || RouletteRedSlots < 1 || RouletteGreenSlots < 1 || RouletteCount > MAX_int32
		|| RouletteRedBlackMultiplier < 2 || RouletteGreenMultiplier < 2)
	{
		return Reject(INVTEXT("轮盘三色格数必须为正整数且总格数可索引，返还倍率至少为 2。"));
	}
	if (!FMath::IsFinite(MiniBoardCellScale) || MiniBoardCellScale <= 0.02f || MiniBoardCellScale > 0.5f)
	{
		return Reject(INVTEXT("微缩格比例必须大于 0.02 且不超过 0.5。"));
	}
	if (SlotMachineCost < 1 || BalloonRecycleGold < 1)
	{
		return Reject(INVTEXT("老虎机费用和气球回收单价必须为正整数。"));
	}
	const int32 Weights[] = {SlotWeights.Miss, SlotWeights.DoubleGold, SlotWeights.Jackpot, SlotWeights.RoomGroup, SlotWeights.Badge};
	int64 Total = 0;
	for (const int32 Weight : Weights)
	{
		if (Weight < 0) { return Reject(INVTEXT("老虎机各档权重必须非负。")); }
		Total += Weight;
	}
	if (Total == 0) { return Reject(INVTEXT("老虎机总权重必须大于 0。")); }
	if ((SlotWeights.DoubleGold > 0 && SlotMachineCost > MAX_int32 / 2)
		|| (SlotWeights.Jackpot > 0 && SlotMachineCost > MAX_int32 / 5))
	{
		return Reject(INVTEXT("老虎机金币奖品超过账户可表示的整数范围。"));
	}
	if ((SlotWeights.RoomGroup > 0 && (RoomGroups.IsEmpty() || RoomDefinitions.IsEmpty())) || (SlotWeights.Badge > 0 && Badges.IsEmpty()))
	{
		return Reject(INVTEXT("启用的房间组和徽章奖项必须配置奖励池。"));
	}
	for (const UCastleRoomGroupDataAsset* Group : RoomGroups)
	{
		if (!IsValid(Group) || Group->DisplayName.IsEmpty() || !StaticEnum<ECastleRoomGroupType>()->IsValidEnumValue(static_cast<int64>(Group->GroupType)))
		{
			return Reject(FText::Format(INVTEXT("房间组 {0} 缺失、没有名称或类型无效。"), FText::FromString(GetPathNameSafe(Group))));
		}
	}
	TSet<const UCastleBuildingDefinition*> SeenDefinitions;
	for (const UCastleBuildingDefinition* Definition : RoomDefinitions)
	{
		FText DefinitionReason;
		if (!IsValid(Definition) || SeenDefinitions.Contains(Definition) || !Definition->ValidateConfiguration(DefinitionReason))
		{
			return Reject(FText::Format(INVTEXT("房间奖励定义 {0} 缺失、重复或无效。{1}"),
				FText::FromString(GetPathNameSafe(Definition)), DefinitionReason));
		}
		SeenDefinitions.Add(Definition);
	}
	for (const UCastleBadgeDataAsset* Asset : Badges)
	{
		FText BadgeReason;
		if (!IsValid(Asset) || !Asset->Badge.Validate(BadgeReason))
		{
			return Reject(FText::Format(INVTEXT("徽章 {0} 无效。{1}"), FText::FromString(GetPathNameSafe(Asset)), BadgeReason));
		}
	}
	return true;
}

bool UCastleGambleSkyIslandDataAsset::TryRollRoulette(const ECastleRouletteColor Bet, const double LuckBonus,
	const double Sample, ECastleRouletteColor& OutColor, int32& OutColorSlot) const
{
	const int32 Counts[] = {RouletteBlackSlots, RouletteRedSlots, RouletteGreenSlots};
	const int32 BetIndex = static_cast<int32>(Bet);
	if (BetIndex >= UE_ARRAY_COUNT(Counts) || !FMath::IsFinite(LuckBonus) || LuckBonus < 0.0) { return false; }
	double Weights[] = {static_cast<double>(Counts[0]), static_cast<double>(Counts[1]), static_cast<double>(Counts[2])};
	Weights[BetIndex] *= 1.0 + LuckBonus;
	int32 Index = 0;
	double Fraction = 0.0;
	if (!TrySelectWeight(Weights, Sample, Index, Fraction)) { return false; }
	// 利用段内小数映射到可见格。通过长整型容纳浮点舍入恰达段末时的中间结果。
	const int64 Slot = FMath::FloorToInt64(Fraction * Counts[Index]);
	OutColorSlot = static_cast<int32>(FMath::Min<int64>(Slot, Counts[Index] - 1));
	OutColor = static_cast<ECastleRouletteColor>(Index);
	return true;
}

bool UCastleGambleSkyIslandDataAsset::TryRollSlot(const double LuckBonus, const double Sample, ECastleSlotPrize& OutPrize) const
{
	if (!FMath::IsFinite(LuckBonus) || LuckBonus < 0.0) { return false; }
	const double Factor = 1.0 + LuckBonus;
	// 未中奖权重固定。中奖权重增加后，未中奖的归一化概率随之下降。
	const double Weights[] = {static_cast<double>(SlotWeights.Miss), SlotWeights.DoubleGold * Factor,
		SlotWeights.Jackpot * Factor, SlotWeights.RoomGroup * Factor, SlotWeights.Badge * Factor};
	int32 Index = 0;
	double Fraction = 0.0;
	if (!TrySelectWeight(Weights, Sample, Index, Fraction)) { return false; }
	OutPrize = static_cast<ECastleSlotPrize>(Index);
	return true;
}
