#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CastleMassDeathSplitProcessor.generated.h"

/** 在全部伤害结算之后、生命回收之前检查死亡；包含护盾反伤造成的死亡。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassDeathSplitProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UCastleMassDeathSplitProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
	FMassEntityQuery Query;
};
