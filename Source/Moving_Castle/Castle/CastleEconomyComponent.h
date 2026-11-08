// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastleEconomyComponent.generated.h"

/** 金币提交成功后发送旧值和新值；失败的收支不会发送通知。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCastleGoldChanged, int32, PreviousGold, int32, CurrentGold);

/**
 * 独立的单局金币账户。商店、奖励、农田只通过此组件收支，不直接改余额。
 * 当前挂在自有玩家控制器上，每次开始游戏创建一个账户，不依赖背包或棋盘。
 */
UCLASS(ClassGroup = (Castle), meta = (BlueprintSpawnableComponent))
class MOVING_CASTLE_API UCastleEconomyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 账户没有逐帧逻辑，余额从零开始，由游戏入口明确发放初始金币。 */
	UCastleEconomyComponent();

	/** 只读余额；负数金额、余额不足或整数溢出都直接拒绝，绝不截断或透支。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|经济")
	int32 GetGold() const { return Gold; }

	/** 检查一笔收入是否有效且不会超过 int32 的余额上限，不修改账户。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|经济")
	bool CanAddGold(int32 Amount, FText& OutFailureReason) const;

	/** 检查一笔支出是否有效且余额充足，零金额表示免费操作。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|经济")
	bool CanSpendGold(int32 Amount, FText& OutFailureReason) const;

	/** 增加金币。成功且余额发生变化时通知 UI；失败时返回可展示的中文原因。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|经济")
	bool TryAddGold(int32 Amount, FText& OutFailureReason);

	/** 扣除金币。余额不足时不产生任何修改，调用方不需要事后修正余额。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|经济")
	bool TrySpendGold(int32 Amount, FText& OutFailureReason);

	/** UI 或后续任务系统可以订阅余额变化，无需耦合具体收支来源。 */
	UPROPERTY(BlueprintAssignable, Category = "移动城堡|经济")
	FCastleGoldChanged OnGoldChanged;

private:
	/** 金币只能经过明确的收支接口修改，编辑器和蓝图均只能读取。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "经济", meta = (AllowPrivateAccess = "true"))
	int32 Gold = 0;
};
