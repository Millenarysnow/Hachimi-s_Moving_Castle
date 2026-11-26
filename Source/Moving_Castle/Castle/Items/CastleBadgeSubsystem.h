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

	/** 不可叠加的效果只检查已入槽的徽章，替换弹窗中的候选项不参与。 */
	bool HasBadge(ECastleBadgeType Type) const;

	/** 与红色空投的局部加成独立相乘，所有我方伤害入口使用同一倍率。 */
	double GetFriendlyDamageMultiplier() const;
	/** 每枚贡献一成；经济账户用整数计算金币取整，避免恰好整除时多发一金。 */
	int32 GetCoinPurseTenths() const;
	int64 GetStartingFundsGold() const;
	int64 GetDoubleDoorPassGold() const;
	int64 GetSpareBowstringBonus() const;
	/** 普通房间买牌价专用；零价保持零，正数报价向下取整且最低一金。 */
	int32 GetDiscountedShopBuyPrice(int32 BasePrice) const;
	int64 GetShopRebateCards() const;
	float GetShiftWhistleSpeedMultiplier() const;
	double GetStringGloveExtraCells() const;
	float GetQuickLadderSpeedMultiplier() const;
	float GetSiphonArrowFraction() const;
	float GetRapidMarchSpeedMultiplier(int32 BuildingLevel) const;
	int64 GetBerserkBeeGold() const;

	/** 仅供自动化和验收命令清理内存状态。 */
	void ClearAllBadgesForTesting();

private:
	double SumMagnitude(ECastleBadgeType Type) const;
	int32 CountBadges(ECastleBadgeType Type) const;

	UPROPERTY(Transient)
	TArray<FCastleBadgeDefinition> Badges;

	/** 待替换项和已持有项都需要让 GC 追踪结构内的图标引用。 */
	UPROPERTY(Transient)
	TOptional<FCastleBadgeDefinition> PendingBadge;
};
