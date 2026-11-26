// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleEconomyComponent.h"
#include "Moving_Castle.h"

UCastleEconomyComponent::UCastleEconomyComponent()
{
	// 收支由玩法事件驱动，不随帧数变化。
	PrimaryComponentTick.bCanEverTick = false;
}

bool UCastleEconomyComponent::CanAddGold(int32 Amount, FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (Amount < 0)
	{
		OutFailureReason = FText::FromString(TEXT("增加金币的金额不能为负数。"));
		return false;
	}
	if (static_cast<int64>(Gold) + Amount > MAX_int32)
	{
		OutFailureReason = FText::FromString(TEXT("增加金币失败：余额将超出整数上限。"));
		return false;
	}
	return true;
}

bool UCastleEconomyComponent::CanSpendGold(int32 Amount, FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (Amount < 0)
	{
		OutFailureReason = FText::FromString(TEXT("扣除金币的金额不能为负数。"));
		return false;
	}
	if (Gold < Amount)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("金币不足：需要 %d，当前只有 %d。"), Amount, Gold));
		return false;
	}
	return true;
}

bool UCastleEconomyComponent::TryAddGold(int32 Amount, FText& OutFailureReason)
{
	if (!CanAddGold(Amount, OutFailureReason))
	{
		return false;
	}
	if (Amount > 0)
	{
		const int32 PreviousGold = Gold;
		Gold += Amount;
		OnGoldChanged.Broadcast(PreviousGold, Gold);
	}
	return true;
}

bool UCastleEconomyComponent::TryAddGameplayGold(const int32 BaseAmount, FText& OutFailureReason, int32* OutFinalAmount)
{
	// 奖励倍率和余额提交由 Phase 1B 接入；此入口在此前不调整余额或通知 UI。
	OutFailureReason = INVTEXT("玩法金币入口尚未实现（Phase 0）。");
	UE_LOG(LogMoving_Castle, Error, TEXT("金币账户 %s 拒绝玩法收入 %d：%s"),
		*GetPathName(), BaseAmount, *OutFailureReason.ToString());
	return false;
}

bool UCastleEconomyComponent::TrySpendGold(int32 Amount, FText& OutFailureReason)
{
	if (!CanSpendGold(Amount, OutFailureReason))
	{
		return false;
	}
	if (Amount > 0)
	{
		const int32 PreviousGold = Gold;
		Gold -= Amount;
		OnGoldChanged.Broadcast(PreviousGold, Gold);
	}
	return true;
}
