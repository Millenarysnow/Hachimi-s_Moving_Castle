#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CastleMassMeleeAttackProcessor.generated.h"

/**
 * 只处理带近战能力的敌人：优先吸引圈内护盾，否则主塔；进入攻击范围后停速并按冷却攻击。
 * 护盾反伤与主塔组件在游戏线程结算，实体仍按 Chunk 批量处理，不创建逐敌人 Actor。
 */
UCLASS()
class MOVING_CASTLE_API UCastleMassMeleeAttackProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	/** 在普通移动后结算本帧攻击并设置下一帧追踪目标。 */
	UCastleMassMeleeAttackProcessor();

protected:
	/** 近战 Fragment 是必要条件，只有敌人基类身份的实体不会进入查询。 */
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;

	/** 逐实体重新查询最近激活护盾，前一攻击破盾后后续实体立即可转火主塔。 */
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	/** 位置只读，生命可写以声明即时反伤权限；并写近战状态、移动目标与速度。 */
	FMassEntityQuery Query;

	/** 两类目标均不可用时每次缺失期间只报一次，避免逐敌人逐帧刷屏。 */
	bool bLoggedMissingTargets = false;
};
