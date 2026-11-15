#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "MassObserverProcessor.h"
#include "CastleMassEnemyLaserVisualizationProcessor.generated.h"

/** 全部伤害结算后提交存活幼骸的光束，生命回收前清除当帧死亡者。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassEnemyLaserVisualizationProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UCastleMassEnemyLaserVisualizationProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
	FMassEntityQuery Query;
};

/** 任意实体删除路径都立即清理该幼骸的光束，而非仅依赖生命处理器。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassEnemyLaserRemovalObserver : public UMassObserverProcessor
{
	GENERATED_BODY()
public:
	UCastleMassEnemyLaserRemovalObserver();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
	FMassEntityQuery Query;
};
