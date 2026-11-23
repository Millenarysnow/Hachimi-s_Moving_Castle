#include "Items/CastleBadgeSubsystem.h"

#include "Moving_Castle.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
	bool RejectBadge(const UCastleBadgeSubsystem& Subsystem, const FCastleBadgeDefinition& Badge,
		const FText& Reason, FText& OutFailureReason)
	{
		OutFailureReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章子系统 %s 拒绝徽章 %s（类型 %u，数值 %.17g）：%s"),
			*Subsystem.GetPathName(), *Badge.GetDisplayName().ToString(), static_cast<uint32>(Badge.Type),
			Badge.Magnitude, *Reason.ToString());
		return false;
	}
}

UCastleBadgeSubsystem* UCastleBadgeSubsystem::GetForWorld(const UWorld* World)
{
	// 徽章归本局所属 GameInstance；编辑器预览与无 GameInstance 的测试世界没有玩家徽章。
	const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UCastleBadgeSubsystem>() : nullptr;
}

bool UCastleBadgeSubsystem::TryAcquireBadge(const FCastleBadgeDefinition& Badge, bool& bOutNeedsReplacement,
	FText& OutFailureReason)
{
	bOutNeedsReplacement = false;
	OutFailureReason = FText::GetEmpty();
	if (!IsInGameThread())
	{
		return RejectBadge(*this, Badge, INVTEXT("徽章只能在游戏线程领取。"), OutFailureReason);
	}
	FText ValidationReason;
	if (!Badge.Validate(ValidationReason))
	{
		return RejectBadge(*this, Badge, ValidationReason, OutFailureReason);
	}
	if (PendingBadge.IsSet())
	{
		return RejectBadge(*this, Badge, INVTEXT("请先处理当前待替换徽章。"), OutFailureReason);
	}

	// 满额时只记录候选奖励；只有玩家确认替换，已持有槽位才会改变。
	bOutNeedsReplacement = Badges.Num() == MaxBadges;
	if (bOutNeedsReplacement)
	{
		PendingBadge = Badge;
		UE_LOG(LogMoving_Castle, Display, TEXT("徽章栏已满，等待处理新徽章 %s。"), *Badge.GetDisplayName().ToString());
	}
	else
	{
		Badges.Add(Badge);
		UE_LOG(LogMoving_Castle, Display, TEXT("已领取徽章 %s，占用 %d/%d 槽。"),
			*Badge.GetDisplayName().ToString(), Badges.Num(), MaxBadges);
	}
	OnBadgesChanged.Broadcast();
	return true;
}

bool UCastleBadgeSubsystem::TryReplaceBadgeAt(const int32 ReplaceSlotIndex, const FCastleBadgeDefinition& NewBadge,
	FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (!IsInGameThread())
	{
		return RejectBadge(*this, NewBadge, INVTEXT("徽章只能在游戏线程替换。"), OutFailureReason);
	}
	if (!PendingBadge.IsSet() || PendingBadge->Type != NewBadge.Type || PendingBadge->Magnitude != NewBadge.Magnitude)
	{
		return RejectBadge(*this, NewBadge, INVTEXT("指定徽章与当前待替换徽章不一致。"), OutFailureReason);
	}
	if (!Badges.IsValidIndex(ReplaceSlotIndex))
	{
		return RejectBadge(*this, NewBadge, INVTEXT("要替换的徽章槽位无效。"), OutFailureReason);
	}

	Badges[ReplaceSlotIndex] = PendingBadge.GetValue();
	PendingBadge.Reset();
	UE_LOG(LogMoving_Castle, Display, TEXT("徽章槽 %d 已替换为 %s。"),
		ReplaceSlotIndex, *Badges[ReplaceSlotIndex].GetDisplayName().ToString());
	OnBadgesChanged.Broadcast();
	return true;
}

void UCastleBadgeSubsystem::DiscardPendingBadge()
{
	if (!ensureMsgf(IsInGameThread(), TEXT("只能在游戏线程舍弃待替换徽章。")))
	{
		return;
	}
	if (PendingBadge.IsSet())
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("已舍弃待替换徽章 %s。"), *PendingBadge->GetDisplayName().ToString());
		PendingBadge.Reset();
		OnBadgesChanged.Broadcast();
	}
}

bool UCastleBadgeSubsystem::TryGetPendingBadge(FCastleBadgeDefinition& OutBadge) const
{
	if (!PendingBadge.IsSet())
	{
		return false;
	}
	OutBadge = PendingBadge.GetValue();
	return true;
}

void UCastleBadgeSubsystem::ClearAllBadgesForTesting()
{
	if (!ensureMsgf(IsInGameThread(), TEXT("只能在游戏线程清理徽章。")))
	{
		return;
	}
	Badges.Reset();
	PendingBadge.Reset();
	OnBadgesChanged.Broadcast();
}

double UCastleBadgeSubsystem::SumMagnitude(const ECastleBadgeType Type) const
{
	double Sum = 0.0;
	for (const FCastleBadgeDefinition& Badge : Badges)
	{
		if (Badge.Type == Type)
		{
			Sum += Badge.Magnitude;
		}
	}
	return Sum;
}

bool UCastleBadgeSubsystem::HasBadge(const ECastleBadgeType Type) const
{
	return Badges.ContainsByPredicate([Type](const FCastleBadgeDefinition& Badge) { return Badge.Type == Type; });
}

double UCastleBadgeSubsystem::GetGambleLuckBonus() const
{
	return SumMagnitude(ECastleBadgeType::GambleLuck);
}

int64 UCastleBadgeSubsystem::GetBlueRoomNumberBonus() const
{
	return static_cast<int64>(SumMagnitude(ECastleBadgeType::BlueRoomNumber));
}

int64 UCastleBadgeSubsystem::GetUpgradeBonusRoomCards() const
{
	return static_cast<int64>(SumMagnitude(ECastleBadgeType::UpgradeRoomCards));
}

double UCastleBadgeSubsystem::GetMainTowerHealthMultiplier() const
{
	return 1.0 + SumMagnitude(ECastleBadgeType::MainTowerHealth);
}

double UCastleBadgeSubsystem::GetArcherFireRateMultiplier() const
{
	return 1.0 + SumMagnitude(ECastleBadgeType::ArcherFireRate);
}

float UCastleBadgeSubsystem::GetGlobalTowerFireRateMultiplier(const bool bFirstDamageTriggered) const
{
	return bFirstDamageTriggered && HasBadge(ECastleBadgeType::FirstMainTowerDamage) ? 2.0f : 1.0f;
}

float UCastleBadgeSubsystem::GetSummonSpeedMultiplier(const bool bFirstDamageTriggered) const
{
	// 两项收益共用同一触发和倍率，避免分别处理同一枚徽章。
	return GetGlobalTowerFireRateMultiplier(bFirstDamageTriggered);
}

float UCastleBadgeSubsystem::GetEnemyAttackSpeedMultiplier(const float CurrentHealthFraction) const
{
	return CurrentHealthFraction <= 0.20f && HasBadge(ECastleBadgeType::LowMainTowerHealth) ? 0.5f : 1.0f;
}
