#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CastleMassBuildingTriggerProcessor.generated.h"

/** 移动后按 Chunk 并行查进入标记，汇总后仅在游戏线程调用建筑接口。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassBuildingTriggerProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UCastleMassBuildingTriggerProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	FMassEntityQuery Query;
};
