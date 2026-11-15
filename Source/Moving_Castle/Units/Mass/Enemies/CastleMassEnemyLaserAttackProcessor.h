#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CastleMassEnemyLaserAttackProcessor.generated.h"

/** 批量管理熔岩幼骸的持续激光索敌、停步、结算与光束显示。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassEnemyLaserAttackProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UCastleMassEnemyLaserAttackProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
	FMassEntityQuery Query;
	bool bLoggedMissingTargets = false;
};
