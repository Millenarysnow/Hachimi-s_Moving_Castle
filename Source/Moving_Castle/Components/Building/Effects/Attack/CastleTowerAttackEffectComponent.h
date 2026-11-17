// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Castle/Data/CastleTowerAttackDataAsset.h"
#include "CastleTowerAttackEffectComponent.generated.h"

/**
 * 箭塔攻击效果及攻击队列基类。箭塔按数字、下层建筑与 NPC 农田增益装填多发，
 * 后续由游戏线程 Tick 按攻击数据依次寻找敌人并生成追踪子弹。
 */
UCLASS(ClassGroup=(Castle), BlueprintType)
class MOVING_CASTLE_API UCastleTowerAttackEffectComponent : public UCastleBuildingEffectComponent
{
	GENERATED_BODY()

public:
	UCastleTowerAttackEffectComponent();

	/** 尚未发射的箭数；无敌人时对应一发会明确警告并作废。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="建筑|塔攻击")
	int64 PendingShotCount = 0;

protected:
	/** 记录触发后只排队，不因射速间隔或前一发仍在等待而吞掉新触发。 */
	virtual bool ExecuteEffect(const FCastleBuildingTriggerContext& Context) override;

	/** 将本次装填数量一次性入队；溢出或无效世界时间明确失败，不截断或吞掉新触发。 */
	bool QueueShots(int64 ShotCount);

	/** 在 NPC 进入的同一游戏线程时刻同步读取增益，主动触发的增益严格为零。 */
	bool GetTriggerFarmBonus(const FCastleBuildingTriggerContext& Context, float& OutBonus) const;

	/** 只在队列非空且没有配置或 Mass 失败等待时推进射速计时。 */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 建筑销毁或合成替换时清空实例队列，避免旧建筑状态残留。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 解析所属建筑、唯一攻击资产和精确等级；任何缺失都会报错并拒绝发射。 */
	bool ResolveAttackConfiguration(const UCastleTowerAttackDataAsset*& OutDataAsset,
		const FCastleTowerAttackLevelData*& OutLevelData) const;

	/** 查找当前最近敌人并请求生成一发；返回 false 表示错误且该待射项仍保留。 */
	virtual bool FireOneShot(const UCastleTowerAttackDataAsset& DataAsset,
		const FCastleTowerAttackLevelData& LevelData);

	/** 箭塔传单发伤害、炮塔传入队时保存的全额伤害，索敌及 Mass 安全边界只有这一份实现。 */
	bool FireProjectile(const UCastleTowerAttackDataAsset& DataAsset, const FCastleTowerAttackLevelData& LevelData, float Damage);

private:
#if WITH_DEV_AUTOMATION_TESTS
	/** 仅自动化测试直接推进组件计时；生产逻辑仍只由正常组件 Tick 驱动。 */
	friend class FCastleTowerAttackQueueTest;
#endif

	/** 建筑结束生命周期时清理待射状态和冷却时刻。 */
	void ClearPendingShots();

	/** 下一发允许发射的世界游戏时间；负值表示该塔还没有发射过。队列清空时保留该时刻。 */
	double NextShotWorldTime = -1.0;
	/** 下一发冷却使用的射速倍率；变更时按未完成比例换算，不能重新开始整轮冷却。 */
	float CooldownRateMultiplier = 1.0f;

	/** 配置或 Mass 接口失败后保留待射项，等待新触发再次尝试，避免逐帧重复报错。 */
	bool bWaitingForExternalRetry = false;
};
