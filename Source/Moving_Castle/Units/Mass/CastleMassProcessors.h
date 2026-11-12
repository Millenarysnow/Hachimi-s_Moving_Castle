#pragma once

#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "CastleMassProcessors.generated.h"

UCLASS()
class MOVING_CASTLE_API UCastleMassMovementProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UCastleMassMovementProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	FMassEntityQuery Query;
};

UCLASS()
class MOVING_CASTLE_API UCastleMassProjectileProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UCastleMassProjectileProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	FMassEntityQuery Query;
};

UCLASS()
class MOVING_CASTLE_API UCastleMassAnimationProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UCastleMassAnimationProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	FMassEntityQuery Query;
};

UCLASS()
class MOVING_CASTLE_API UCastleMassLifetimeProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	UCastleMassLifetimeProcessor();
protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	FMassEntityQuery Query;
};
