#include "Items/CastleItemGrantLibrary.h"

#include "CastleBackpackComponent.h"
#include "CastleBuildingDefinition.h"
#include "Items/CastleBadgeDataAsset.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Items/CastlePendingRoomReward.h"
#include "Items/CastleRoomGroupDataAsset.h"
#include "Moving_Castle.h"

namespace
{
	bool RejectGrant(const UObject* Asset, const FText& Reason, FText& OutFailureReason)
	{
		OutFailureReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("道具资产 %s 发放失败：%s"), *GetPathNameSafe(Asset), *Reason.ToString());
		return false;
	}

}

UCastlePendingRoomReward* UCastleItemGrantLibrary::CreateReward(FText DisplayName, UCastleBackpackComponent& Backpack,
	FCastleWildcardRoomOptions&& Options)
{
	UCastlePendingRoomReward* Reward = NewObject<UCastlePendingRoomReward>(&Backpack);
	Reward->DisplayName = MoveTemp(DisplayName);
	Reward->Backpack = &Backpack;
	Reward->Definitions = MoveTemp(Options.Definitions);
	Reward->Colors = MoveTemp(Options.Colors);
	Reward->Numbers = MoveTemp(Options.Numbers);
	return Reward;
}

bool UCastleItemGrantLibrary::TryCreateWildcardReward(FText DisplayName, const FCastleWildcardRoomSpec& Spec,
	UCastleBackpackComponent* Backpack, const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool,
	UCastlePendingRoomReward*& OutPendingReward, FText& OutFailureReason)
{
	OutPendingReward = nullptr;
	if (!IsInGameThread() || !IsValid(Backpack) || DisplayName.IsEmpty())
	{
		return RejectGrant(Backpack, INVTEXT("创建万能卡奖励需要在游戏线程提供有效背包与奖励名称。"), OutFailureReason);
	}
	FCastleWildcardRoomOptions Options;
	if (!Spec.TryResolveOptions(DefinitionPool, Options, OutFailureReason))
	{
		return false;
	}
	OutPendingReward = CreateReward(MoveTemp(DisplayName), *Backpack, MoveTemp(Options));
	return true;
}

bool UCastleItemGrantLibrary::TryGrantRoomGroup(const UCastleRoomGroupDataAsset* Group, UCastleBackpackComponent* Backpack,
	const ACastleBoard* Board, const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool,
	FRandomStream& Random, UCastlePendingRoomReward*& OutPendingReward, FText& OutFailureReason)
{
	// 发放事务独占一个候选随机流，背包或待领取奖励未提交时保留调用方的原种子。
	OutPendingReward = nullptr;
	FRandomStream CandidateRandom = Random;
	FCastlePreparedRoomGroup Prepared;
	if (!TryPrepareRoomGroup(Group, Backpack, Board, DefinitionPool, CandidateRandom, Prepared, OutFailureReason))
	{
		return false;
	}
	if (!Prepared.PendingReward && !TryCommitPreparedRoomGroup(Prepared, Backpack, OutFailureReason))
	{
		return false;
	}
	OutPendingReward = Prepared.PendingReward;
	Random = CandidateRandom;
	return true;
}

bool UCastleItemGrantLibrary::TryPrepareRoomGroup(const UCastleRoomGroupDataAsset* Group, UCastleBackpackComponent* Backpack,
	const ACastleBoard* Board, const TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool,
	FRandomStream& Random, FCastlePreparedRoomGroup& OutPrepared, FText& OutFailureReason)
{
	// Phase 0 仍能报告配置错误；任何合法配置也必须等到规则执行完成后才能进入扣款和背包提交。
	OutPrepared = {};
	if (!IsInGameThread() || !IsValid(Group) || !IsValid(Backpack) || Group->DisplayName.IsEmpty())
	{
		return RejectGrant(Group, INVTEXT("准备房间组需要游戏线程、有效资产、名称和目标背包。"), OutFailureReason);
	}
	if (!Group->ValidateRarity(OutFailureReason))
	{
		return false;
	}
	if (Group->ShopPrice < 0)
	{
		return RejectGrant(Group, INVTEXT("房间组必须配置非负售价。"), OutFailureReason);
	}
	return RejectGrant(Group, INVTEXT("房间组规则引擎尚未实现（Phase 0）。"), OutFailureReason);
}

bool UCastleItemGrantLibrary::TryCommitPreparedRoomGroup(const FCastlePreparedRoomGroup& Prepared,
	UCastleBackpackComponent* Backpack, FText& OutFailureReason)
{
	// 开始规则引擎实现前不接受任何准备结果，背包与临时栏不会发生部分写入。
	OutFailureReason = INVTEXT("房间组原子提交尚未实现（Phase 0）。");
	UE_LOG(LogMoving_Castle, Error, TEXT("道具资产 %s 无法提交到背包 %s：%s"),
		*GetPathNameSafe(Prepared.Group), *GetPathNameSafe(Backpack), *OutFailureReason.ToString());
	return false;
}

bool UCastleItemGrantLibrary::TryGrantBadge(const UCastleBadgeDataAsset* Asset, UCastleBadgeSubsystem* Badges,
	bool& bOutNeedsReplacement, FText& OutFailureReason)
{
	bOutNeedsReplacement = false;
	if (!IsValid(Asset) || !IsValid(Badges))
	{
		return RejectGrant(Asset, INVTEXT("需要有效的徽章资产和 GameInstance 徽章子系统。"), OutFailureReason);
	}
	if (!Badges->TryAcquireBadge(Asset->Badge, bOutNeedsReplacement, OutFailureReason))
	{
		const FText Reason = OutFailureReason;
		return RejectGrant(Asset, Reason, OutFailureReason);
	}
	return true;
}
