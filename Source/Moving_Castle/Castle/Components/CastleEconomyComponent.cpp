// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleEconomyComponent.h"
#include "Items/CastleBadgeSubsystem.h"
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
	if (OutFinalAmount) { *OutFinalAmount = 0; }
	int32 Amount = 0;
	if (!CalculateGameplayGold(BaseAmount, Amount, OutFailureReason) || !TryAddGold(Amount, OutFailureReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("金币账户 %s 拒绝玩法收入 %d：%s"),
			*GetPathName(), BaseAmount, *OutFailureReason.ToString());
		return false;
	}
	if (OutFinalAmount) { *OutFinalAmount = Amount; }
	return true;
}

bool UCastleEconomyComponent::CanAddGameplayGold(const int32 BaseAmount, FText& OutFailureReason) const
{
	int32 Amount = 0;
	return CalculateGameplayGold(BaseAmount, Amount, OutFailureReason) && CanAddGold(Amount, OutFailureReason);
}

bool UCastleEconomyComponent::CalculateGameplayGold(const int32 BaseAmount, int32& OutAmount, FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (BaseAmount < 0)
	{
		OutFailureReason = INVTEXT("玩法奖励金币不能为负数。");
		return false;
	}
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	const int32 Tenths = Badges ? Badges->GetCoinPurseTenths() : 0;
	// 加成以十进制整数计算。任何正数奖励只要有加成就至少多一金；零仍为零。
	const int64 Base = BaseAmount;
	const int64 Amount = Base + (Base * Tenths + 9) / 10;
	if (Amount > MAX_int32)
	{
		OutFailureReason = INVTEXT("应用管家的零钱袋后，玩法奖励超过金币整数上限。");
		return false;
	}
	OutAmount = static_cast<int32>(Amount);
	return true;
}

bool UCastleEconomyComponent::TrySpendShopGold(const int32 Amount, TFunctionRef<bool()> CommitTrade, FText& OutFailureReason)
{
	if (!CanSpendGold(Amount, OutFailureReason)) { return false; }
	const int32 PreviousGold = Gold;
	Gold -= Amount;
	// 此回调提交背包及商店内部状态；返回失败意味着背包奖励也未发布，可以恢复扣款。
	if (!CommitTrade())
	{
		Gold = PreviousGold;
		return false;
	}
	if (Amount > 0) { OnGoldChanged.Broadcast(PreviousGold, Gold); }
	return true;
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
