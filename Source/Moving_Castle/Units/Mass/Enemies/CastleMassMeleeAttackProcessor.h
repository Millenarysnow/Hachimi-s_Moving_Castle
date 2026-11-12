#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CastleMassMeleeAttackProcessor.generated.h"

/**
 * 只处理带近战能力的敌人：向主塔下达直线移动目标，进入范围后停速并按冷却攻击。
 * 主塔组件在游戏线程访问，实体仍按 Mass Chunk 批量处理，不生成逐敌人 Actor 或控制器。
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

	/** 主塔每帧只解析一次，各近战实体保存各自的范围和冷却状态。 */
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	/** 只读公共生命和位置，写入近战状态、移动目标与速度。 */
	FMassEntityQuery Query;
};
