// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleCardPricing.h"

#include "CastleBuildingBase.h"

bool UCastleSimpleCardPricing::TryCalculatePrice_Implementation(const FCastleBuildingDescriptor& Descriptor, ECastleCardPriceKind Kind, int32& OutPrice, FText& OutFailureReason) const
{
	OutPrice = 0;
	OutFailureReason = FText::GetEmpty();
	// 报价前使用共享定义校验，防止无效等级或丢失资产的卡牌进入交易。
	if (!Descriptor.Validate(OutFailureReason))
	{
		return false;
	}
	const bool bRoad = Descriptor.GetBuildingType() == ECastleBuildingType::Road;
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
