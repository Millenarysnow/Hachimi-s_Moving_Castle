#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CastleMassEnemyRangedAttackProcessor.generated.h"

/** 游戏线程决定远程目标并提交延迟创建命令；真实弹道由敌方追踪弹处理器拥有。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassEnemyRangedAttackProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UCastleMassEnemyRangedAttackProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
	FMassEntityQuery Query;
	bool bLoggedMissingTargets = false;
};
