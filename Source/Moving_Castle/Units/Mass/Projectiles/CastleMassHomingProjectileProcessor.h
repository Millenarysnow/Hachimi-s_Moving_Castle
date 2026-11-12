#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CastleMassHomingProjectileProcessor.generated.h"

/**
 * 普通敌人位移后批量追踪并连续检测命中；目标生命直接进入现有 Mass 生命/死亡回收链。
 * 两个查询显式声明敌人生命的写访问，避免调度器把目标健康读取/写入误当成并行安全。
 */
UCLASS()
class MOVING_CASTLE_API UCastleMassHomingProjectileProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	/** 在 Movement 组内、普通单位移动之后运行，生命周期在该组之后统一回收。 */
	UCastleMassHomingProjectileProcessor();

protected:
	/** 追踪弹和敌人的组成分开声明；没有追踪 Fragment 的普通子弹不进入此处理器。 */
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;

	/** 同帧快照目标位置，并按顺序结算伤害，保证后续子弹能看到前一发造成的死亡。 */
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** 追踪弹拥有自己的位移、速度、伤害和生命写权限。 */
	FMassEntityQuery ProjectileQuery;

	/** 目标位置/半径只读，目标生命可写；快照仅在当前 Execute 内持有 Fragment 指针。 */
	FMassEntityQuery EnemyQuery;
};
