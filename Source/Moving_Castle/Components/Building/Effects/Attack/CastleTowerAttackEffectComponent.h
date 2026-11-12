// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Castle/Data/CastleTowerAttackDataAsset.h"
#include "CastleTowerAttackEffectComponent.generated.h"

/**
 * 通用塔攻击效果组件。每次有效建筑触发只向本实例待射队列增加一发，
 * 后续由游戏线程 Tick 按攻击数据依次寻找敌人并生成追踪子弹。
 */
UCLASS(ClassGroup=(Castle), BlueprintType)
class MOVING_CASTLE_API UCastleTowerAttackEffectComponent : public UCastleBuildingEffectComponent
{
	GENERATED_BODY()

public:
	UCastleTowerAttackEffectComponent();

	/** 尚未结算的触发次数；无敌人时对应一发会明确警告并作废。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="建筑|塔攻击")
	int64 PendingShotCount = 0;

protected:
	/** 记录触发后只排队，不因射速间隔或前一发仍在等待而吞掉新触发。 */
	virtual void ExecuteEffect(const FCastleBuildingTriggerContext& Context) override;

	/** 只在队列非空且没有配置或 Mass 失败等待时推进射速计时。 */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 建筑销毁或合成替换时清空实例队列，避免旧建筑状态残留。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	/** 仅自动化测试直接推进组件计时；生产逻辑仍只由正常组件 Tick 驱动。 */
	friend class FCastleTowerAttackQueueTest;
#endif

	/** 解析所属建筑、唯一攻击资产和精确等级；任何缺失都会报错并拒绝发射。 */
	bool ResolveAttackConfiguration(const UCastleTowerAttackDataAsset*& OutDataAsset,
		const FCastleTowerAttackLevelData*& OutLevelData) const;

	/** 查找当前最近敌人并请求生成一发；返回 false 表示错误且该待射项仍保留。 */
	bool FireOneShot(const UCastleTowerAttackDataAsset& DataAsset,
		const FCastleTowerAttackLevelData& LevelData);

	/** 清理待射状态并关闭 Tick，供队列耗尽和 EndPlay 共用。 */
	void ClearPendingShots();

	/** 从首发开始到下一发的剩余时间；负值表示尚未启动当前队列计时。 */
	float SecondsUntilNextShot = -1.0f;

	/** 配置或 Mass 接口失败后保留待射项，等待新触发再次尝试，避免逐帧重复报错。 */
	bool bWaitingForExternalRetry = false;
};
