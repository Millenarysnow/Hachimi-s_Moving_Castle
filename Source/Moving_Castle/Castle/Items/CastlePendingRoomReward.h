#pragma once

#include "Items/CastleItemTypes.h"
#include "UObject/Object.h"
#include "CastlePendingRoomReward.generated.h"

class UCastleBackpackComponent;

/** 奖励卡的共享句柄。复制指针仍指向同一领取状态，避免同一奖励进入库存两次。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastlePendingRoomReward : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "移动城堡|道具")
	FText GetDisplayName() const { return DisplayName; }

	UFUNCTION(BlueprintPure, Category = "移动城堡|道具")
	bool IsClaimed() const { return State == EClaimState::Claimed; }

	/** 只能由奖励卡的「获取」动作调用。成功后才提供本次弹窗的属性选项。 */
	bool TryBeginClaim(FCastleWildcardRoomOptions& OutOptions, FText& OutFailureReason);

	/** 从已打开的领取界面取消，保留待领取奖励。 */
	bool TryCancelClaim(FText& OutFailureReason);

	/** 校验全部选项并向指定背包提交，失败时允许用户留在领取界面修正。 */
	bool TryConfirmClaim(UCastleBuildingDefinition* Definition, ECastleTileColor Color, int32 Number,
		bool& bOutWentToTempBar, int32& OutIndex, FText& OutFailureReason);

private:
	friend class UCastleItemGrantLibrary;

	enum class EClaimState : uint8
	{
		Available,
		Choosing,
		Committing,
		Claimed
	};

	/** 奖励卡强引用已验证的配置候选，直到 UI 释放此奖励。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCastleBuildingDefinition>> Definitions;

	UPROPERTY(Transient)
	FText DisplayName;

	TArray<ECastleTileColor> Colors;
	TArray<int32> Numbers;
	/** 此模板第一次点击「获取」时才抽取并锁定一个功能塔。 */
	TOptional<FRandomStream> DefinitionDraw;
	TWeakObjectPtr<UCastleBackpackComponent> Backpack;
	EClaimState State = EClaimState::Available;
};
