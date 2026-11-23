#include "Items/CastlePendingRoomReward.h"

#include "CastleBackpackComponent.h"
#include "CastleBuildingDefinition.h"
#include "Moving_Castle.h"

namespace
{
	bool RejectClaim(const UCastlePendingRoomReward& Reward, const FText& Reason, FText& OutFailureReason)
	{
		OutFailureReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("房间奖励 %s（%s）领取失败：%s"),
			*Reward.GetDisplayName().ToString(), *Reward.GetPathName(), *Reason.ToString());
		return false;
	}
}

bool UCastlePendingRoomReward::SetClaimTransaction(FAuthorizeClaim InAuthorize, FOnClaimCompleted InCompleted,
	FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (!IsInGameThread() || State != EClaimState::Available || AuthorizeClaim || !InAuthorize || !InCompleted)
	{
		return RejectClaim(*this, INVTEXT("领取事务只能在打开选择前绑定一次，且需要授权和结果处理。"), OutFailureReason);
	}
	AuthorizeClaim = MoveTemp(InAuthorize);
	OnClaimCompleted = MoveTemp(InCompleted);
	return true;
}

bool UCastlePendingRoomReward::TryBeginClaim(FCastleWildcardRoomOptions& OutOptions, FText& OutFailureReason)
{
	OutOptions = {};
	OutFailureReason = FText::GetEmpty();
	if (!IsInGameThread() || State != EClaimState::Available || !Backpack.IsValid()
		|| Definitions.IsEmpty() || Colors.IsEmpty() || Numbers.IsEmpty())
	{
		return RejectClaim(*this, INVTEXT("奖励未处于可获取状态，或缺少有效背包与候选属性。"), OutFailureReason);
	}

	if (DefinitionDraw.IsSet())
	{
		// 抽中的功能属于这张奖励，取消只放弃本次属性选择。再次打开时仍展示已抽到的功能。
		const TObjectPtr<UCastleBuildingDefinition> Chosen = Definitions[DefinitionDraw->RandRange(0, Definitions.Num() - 1)];
		Definitions = { Chosen };
		DefinitionDraw.Reset();
	}
	OutOptions.Definitions = Definitions;
	OutOptions.Colors = Colors;
	OutOptions.Numbers = Numbers;
	State = EClaimState::Choosing;
	UE_LOG(LogMoving_Castle, Display, TEXT("开始选择房间奖励 %s 的属性。"), *DisplayName.ToString());
	return true;
}

bool UCastlePendingRoomReward::TryCancelClaim(FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (!IsInGameThread() || State != EClaimState::Choosing)
	{
		return RejectClaim(*this, INVTEXT("当前奖励没有打开的属性选择。"), OutFailureReason);
	}
	if (OnClaimCompleted)
	{
		// 调用方先撤销自己的弹窗状态，回调返回后同一奖励才允许再次开始。
		State = EClaimState::Committing;
		OnClaimCompleted(EClaimResult::Cancelled);
	}
	State = EClaimState::Available;
	UE_LOG(LogMoving_Castle, Display, TEXT("已取消选择，房间奖励 %s 仍可领取。"), *DisplayName.ToString());
	return true;
}

bool UCastlePendingRoomReward::TryConfirmClaim(UCastleBuildingDefinition* Definition, const ECastleTileColor Color,
	const int32 Number, bool& bOutWentToTempBar, int32& OutIndex, FText& OutFailureReason)
{
	bOutWentToTempBar = false;
	OutIndex = INDEX_NONE;
	OutFailureReason = FText::GetEmpty();
	UCastleBackpackComponent* Target = Backpack.Get();
	if (!IsInGameThread() || State != EClaimState::Choosing || !Target)
	{
		return RejectClaim(*this, INVTEXT("当前奖励没有打开的属性选择，或目标背包已失效。"), OutFailureReason);
	}

	FCastleBuildingDescriptor Descriptor;
	const FCastleWildcardRoomOptions Options{ Definitions, Colors, Numbers };
	if (!Options.TryMakeDescriptor(Definition, Color, Number, Descriptor, OutFailureReason))
	{
		return false;
	}

	// 追加会同步通知界面。通知期间拒绝重复确认或取消同一笔奖励。
	State = EClaimState::Committing;
	if (AuthorizeClaim && !AuthorizeClaim(OutFailureReason))
	{
		State = EClaimState::Choosing;
		return false;
	}
	if (!Target->TryAddItemAllowOverflow(Descriptor, bOutWentToTempBar, OutIndex, OutFailureReason))
	{
		// 先回滚调用方的扣款，再重新开放按钮。回滚回调也不得重入本次领取。
		if (OnClaimCompleted) { OnClaimCompleted(EClaimResult::WriteFailed); }
		State = EClaimState::Choosing;
		return false;
	}
	State = EClaimState::Claimed;
	if (OnClaimCompleted) { OnClaimCompleted(EClaimResult::Granted); }
	UE_LOG(LogMoving_Castle, Display, TEXT("已领取房间奖励 %s，建筑 %s，色值 %d，数字 %d。"),
		*DisplayName.ToString(), *Definition->GetPathName(), static_cast<int32>(Color), Number);
	return true;
}
