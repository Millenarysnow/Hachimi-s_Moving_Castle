#pragma once

#include "Items/CastleBadgeTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CastleBadgeSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCastleBadgesChanged);

/** 跨地图保留徽章槽位与尚未处理的替换选择。效果查询不修改库存和战斗状态。 */
UCLASS()
class MOVING_CASTLE_API UCastleBadgeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 从当前世界读取跨地图徽章；没有 GameInstance 的隔离世界没有已持有徽章。 */
	static UCastleBadgeSubsystem* GetForWorld(const UWorld* World);
	static constexpr int32 MaxBadges = 6;

	/** 库存或待替换选择改变后通知界面，此时所有字段均已提交。 */
	UPROPERTY(BlueprintAssignable, Category = "移动城堡|徽章")
	FCastleBadgesChanged OnBadgesChanged;

	/** 满额时保留新徽章等待玩家选择，成功返回并标明需要替换。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|徽章")
	bool TryAcquireBadge(const FCastleBadgeDefinition& Badge, bool& bOutNeedsReplacement, FText& OutFailureReason);

	/** 仅允许用当前待替换徽章替换已有槽位，避免失效按钮修改另一笔奖励。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|徽章")
	bool TryReplaceBadgeAt(int32 ReplaceSlotIndex, const FCastleBadgeDefinition& NewBadge, FText& OutFailureReason);

	UFUNCTION(BlueprintCallable, Category = "移动城堡|徽章")
	void DiscardPendingBadge();

	/** 返回按入槽顺序排列的已持有徽章，重复种类分别保留。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|徽章")
	TArray<FCastleBadgeDefinition> GetBadges() const { return Badges; }

	/** 界面每次展示时读取当前待替换徽章。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|徽章")
	bool TryGetPendingBadge(FCastleBadgeDefinition& OutBadge) const;

	UFUNCTION(BlueprintPure, Category = "移动城堡|徽章")
	double GetGambleLuckBonus() const;

	UFUNCTION(BlueprintPure, Category = "移动城堡|徽章")
	int64 GetBlueRoomNumberBonus() const;

	UFUNCTION(BlueprintPure, Category = "移动城堡|徽章")
	int64 GetUpgradeBonusRoomCards() const;

	UFUNCTION(BlueprintPure, Category = "移动城堡|徽章")
	double GetMainTowerHealthMultiplier() const;

	UFUNCTION(BlueprintPure, Category = "移动城堡|徽章")
	double GetArcherFireRateMultiplier() const;

	/** 首次受伤状态由本场战斗的调用方维护。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|徽章")
	float GetGlobalTowerFireRateMultiplier(bool bFirstDamageTriggered) const;

	UFUNCTION(BlueprintPure, Category = "移动城堡|徽章")
	float GetSummonSpeedMultiplier(bool bFirstDamageTriggered) const;

	UFUNCTION(BlueprintPure, Category = "移动城堡|徽章")
	float GetEnemyAttackSpeedMultiplier(float CurrentHealthFraction) const;

	/** 仅供自动化和验收命令清理内存状态。 */
	void ClearAllBadgesForTesting();

private:
	double SumMagnitude(ECastleBadgeType Type) const;
	bool HasBadge(ECastleBadgeType Type) const;

	UPROPERTY(Transient)
	TArray<FCastleBadgeDefinition> Badges;

	TOptional<FCastleBadgeDefinition> PendingBadge;
};
