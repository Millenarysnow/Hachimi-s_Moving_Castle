// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleCardPricing.h"

#include "CastleBuildingBase.h"

bool UCastleSimpleCardPricing::TryCalculatePrice_Implementation(const FCastleBuildingDescriptor& Descriptor, ECastleCardPriceKind Kind, int32& OutPrice, FText& OutFailureReason) const
{
	OutPrice = 0;
	OutFailureReason = FText::GetEmpty();
	if (Descriptor.BuildingClass == nullptr || Descriptor.BuildingClass->HasAnyClassFlags(CLASS_Abstract))
	{
		OutFailureReason = FText::FromString(TEXT("定价失败：卡牌必须指定可生成的具体建筑类。"));
		return false;
	}
	const ACastleBuildingBase* Defaults = Descriptor.BuildingClass->GetDefaultObject<ACastleBuildingBase>();
	if (Defaults->GetBuildingType() != Descriptor.BuildingType || Defaults->GetBuildingLevel() != Descriptor.Level || Descriptor.Level < 1 || Descriptor.Level > 5)
	{
		OutFailureReason = FText::FromString(TEXT("定价失败：卡牌类别或等级与建筑类不一致。"));
		return false;
	}
	const bool bRoad = Descriptor.BuildingType == ECastleBuildingType::Road;
	const bool bValidColor = Descriptor.Color >= ECastleTileColor::Red && Descriptor.Color <= ECastleTileColor::Purple;
	if ((bRoad && (Descriptor.Color != ECastleTileColor::Gray || Descriptor.Number != 0))
		|| (!bRoad && (!bValidColor || Descriptor.Number < 1 || Descriptor.Number > 9)))
	{
		OutFailureReason = FText::FromString(TEXT("定价失败：道路必须是灰色 0 号，功能卡必须是有效颜色的 1 至 9 号。"));
		return false;
	}
	if (Kind != ECastleCardPriceKind::Buy && Kind != ECastleCardPriceKind::Sell)
	{
		OutFailureReason = FText::FromString(TEXT("定价失败：未知的交易类型。"));
		return false;
	}

	// 先计算基础购买价，再按交易类型得到最终报价；数值只存在于这个测试策略中。
	const int32 BuyPrice = bRoad ? 2 : 5 + Descriptor.Number * 2 + (Descriptor.Level - 1) * 10;
	OutPrice = Kind == ECastleCardPriceKind::Buy ? BuyPrice : BuyPrice / 2;
	return true;
}
