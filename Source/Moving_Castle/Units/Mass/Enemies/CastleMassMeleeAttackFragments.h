#pragma once

#include "MassEntityTypes.h"
#include "CastleMassMeleeAttackFragments.generated.h"

/** 仅添加近战能力的敌人才拥有此状态；敌人基类不占用攻击数据，也不会被近战处理器查询到。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassMeleeAttackFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 每次有效近战攻击的伤害；0 仍计为一次可观测的攻击。 */
	UPROPERTY()
	float AttackDamage = 0.0f;

	/** 两次近战攻击的间隔，单位为秒。 */
	UPROPERTY()
	float AttackInterval = 1.0f;

	/** 到主塔目标点的三维攻击距离，单位为厘米。 */
	UPROPERTY()
	float AttackRange = 150.0f;

	/** 当前剩余攻击冷却；每个实体独立保存，不能写入共享配置。 */
	UPROPERTY()
	float AttackCooldown = 0.0f;

	/** 是否已经进入攻击距离，用于快照诊断和后续表现。 */
	UPROPERTY()
	bool bInAttackRange = false;

	/** 已成功执行的攻击次数，零伤害测试兵也能据此验证攻击链路。 */
	UPROPERTY()
	int32 AttackCount = 0;
};
