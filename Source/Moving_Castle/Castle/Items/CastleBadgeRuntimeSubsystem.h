#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "CastleBadgeRuntimeSubsystem.generated.h"

/** 单局与单波徽章状态的归属。Phase 1B 在游戏线程接入事件并向 Mass 提供数值快照。 */
UCLASS()
class MOVING_CASTLE_API UCastleBadgeRuntimeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 查询下一笔手动刷新。只有成功刷新才扣配额并推进环路采购签计数。 */
	int32 GetRemainingFreeShopRefreshes() const;
	bool IsMatchingShopRefreshDue() const;
	void RecordShopRefresh(bool bUsedFreeRefresh);
	/** 查询下一笔普通商店购买的赠牌数，与实际事务分开，允许配置失败时原地重试。 */
	int64 GetNextShopPurchaseRebate() const;
	void RecordShopPurchase();

protected:
	/** 仅游戏和 PIE 世界具备独立的单局事件与结算状态。 */
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	void HandleRunStarted();
	void HandleEditingPhaseEntered();

	FDelegateHandle RunStartedHandle;
	FDelegateHandle EditingEnteredHandle;
	/** 每局的两个商店计数只保存十次周期中的进度，长期游玩也不会溢出。 */
	int32 ShopPurchases = 0;
	int32 ShopRefreshes = 0;
	int32 FreeShopRefreshes = 0;
};
