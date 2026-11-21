#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Castle/Combat/CastleCombatTypes.h"
#include "CastleCombatSubsystem.generated.h"

class ACastleMassEnemySpawner;
class UCastleCombatConfigDataAsset;

DECLARE_MULTICAST_DELEGATE_TwoParams(FCastleBattleCleared, ECastleBattleKind, int32);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FCastleBattleFailed, ECastleBattleKind, int32, const FText&);

/** 开战时冻结的单组计划；强引用配置防止 GC，复制名册和曲线值避免延迟期间出场参数漂移。 */
USTRUCT()
struct FCastleCombatScheduledGroup
{
	GENERATED_BODY()
	UPROPERTY()
	FCastleCombatEnemyEntry Enemy;
	UPROPERTY()
	FCastleCombatSpawnGroup Group;
	int32 Seed = 0;
	bool bSpawned = false;
};

/** 世界级战斗调度，仅 Game/PIE；不负责导演流程、奖励、主塔失败界面或敌人 AI。 */
UCLASS()
class MOVING_CASTLE_API UCastleCombatSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	/** 整体校验后替换配置；进行中的战斗拒绝重初始化。失败输出原因及 Error。 */
	bool InitializeCombat(UCastleCombatConfigDataAsset* Config, FText& OutFailureReason);
	/** KindOrdinal 是本类战斗从 1 起的序号；整体预检后生成零延迟组，失败原子回滚。 */
	bool StartBattle(ECastleBattleKind Kind, int32 KindOrdinal, FText& OutFailureReason);
	bool IsBattleActive() const { return bBattleActive; }
	bool IsCombatInitialized() const { return CombatConfig != nullptr; }
	/** 空闲或计数失败返回 false，调用者不得将失败的零输出当作清场。 */
	bool GetBattleProgress(int32& OutEnemyEntities, int32& OutPendingGroups) const;
	/** 读取本场战斗的已击杀进度（已击杀数与本场总预算数）；空闲或计数失败时返回 false。 */
	bool GetBattleDefeatedProgress(int32& OutDefeatedCount, int32& OutTotalCount) const;

	FCastleBattleCleared OnBattleCleared;
	FCastleBattleFailed OnBattleFailed;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual bool IsTickable() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	/** 生成失败回滚与正常清场分开：回滚直接回收，不能走伤害而触发死亡分裂。 */
	bool SpawnGroup(int32 GroupIndex, FText& OutFailureReason);
	void RollbackNewSpawners(int32 FirstNewSpawner);
	bool DestroyClearedSpawners();
	void FailBattle(const FText& Reason);
	void CancelForWorldEnd();
	int32 CountPendingGroups() const;
	/** 估算所有尚未出场组的总实体数；每组按簇范围均值估算，实际出场后按真实数量修正总预算。 */
	int32 EstimatePendingGroupEntities() const;

	UPROPERTY(Transient)
	TObjectPtr<UCastleCombatConfigDataAsset> CombatConfig;
	UPROPERTY(Transient)
	TArray<FCastleCombatScheduledGroup> Plan;
	/** 包括延迟失败后仍有敌人的旧生成器，空闲时也 Tick 负责其生命周期，不遗留无人管理 Actor。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACastleMassEnemySpawner>> ManagedSpawners;

	bool bBattleActive = false;
	bool bWorldEnding = false;
	ECastleBattleKind ActiveKind = ECastleBattleKind::Normal;
	int32 ActiveOrdinal = 0;
	/** 本场战斗用于计算骷髅击杀进度的总敌人数预算；随延迟组真实出场数与分裂增量同步修正。 */
	int32 BattleTotalEnemyBudget = 0;
	double BattleStartGameTime = 0;
	float BattleInnerRadius = 0;
	float BattleOuterRadius = 0;
	float BattleHeightOffset = 0;
};
