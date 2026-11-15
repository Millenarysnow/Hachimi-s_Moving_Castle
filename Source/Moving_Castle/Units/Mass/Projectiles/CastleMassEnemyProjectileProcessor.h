#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CastleMassEnemyProjectileProcessor.generated.h"

/** 游戏线程批量追踪敌方投射物并结算护盾/主塔命中；不用逐发 Actor 碰撞。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassEnemyProjectileProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UCastleMassEnemyProjectileProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
	FMassEntityQuery Query;
};
