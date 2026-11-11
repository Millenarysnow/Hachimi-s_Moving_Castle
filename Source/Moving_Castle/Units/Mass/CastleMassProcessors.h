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

/**
 * 敌人直线追踪与主塔攻击处理器。
 *
 * 处理器在游戏线程运行，因为它需要通过世界级主塔子系统访问 ActorComponent；
 * 实体数据仍然按 Mass Chunk 查询，不为每个敌人创建 AIController 或 Actor。
 */
UCLASS()
class MOVING_CASTLE_API UCastleMassEnemyAttackProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UCastleMassEnemyAttackProcessor();

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
