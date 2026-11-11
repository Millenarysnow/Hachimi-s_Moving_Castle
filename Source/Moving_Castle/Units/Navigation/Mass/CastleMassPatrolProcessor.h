#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CastleMassPatrolProcessor.generated.h"

/** 整批只取一次完整闭环快照；每实体通过距离采样移动，绝不调用 FindPath 或创建 AIController。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassPatrolProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UCastleMassPatrolProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	FMassEntityQuery Query;
};
