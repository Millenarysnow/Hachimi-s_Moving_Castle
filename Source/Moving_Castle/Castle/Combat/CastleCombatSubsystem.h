#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Castle/Combat/CastleCombatTypes.h"
#include "CastleCombatSubsystem.generated.h"

class ACastleMassEnemySpawner;
class UCastleCombatConfigDataAsset;

/** 终态带回开战请求的编号，正式对局为关号，独立战斗为指定表项序号。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCastleBattleCleared, ECastleBattleKind, int32);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FCastleBattleFailed, ECastleBattleKind, int32, const FText&);
/** 当前战斗内从 1 开始的波次与总波数，开始事务成功后广播。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCastleWaveStarted, int32, int32);

/** 开战时冻结的批次序列，按已生成批次数推导时刻，避免为每只定时突袭怪复制条目。 */
USTRUCT()
struct FCastleCombatScheduledEntry
{
	GENERATED_BODY()
	UPROPERTY()
	FCastleCombatEnemyEntry Enemy;
	FCastleMassEnemyAttributeOverrides AttributeOverrides;
	FString FieldPath;
	int32 RemainingPoints = 0;
	int32 BatchSize = 1;
	int32 SpawnedBatches = 0;
	double FirstDelay = 0.0;
	double Interval = 0.0;
	int32 Seed = 0;

	int32 NextBatchSize() const { return FMath::Min(RemainingPoints, BatchSize); }
	double NextBatchDelay() const { return FirstDelay + SpawnedBatches * Interval; }
};

/** 冻结整波设置并强引用出场模板。进入下一波以前只从当前波序列取到期批次。 */
USTRUCT()
struct FCastleCombatScheduledWave
{
	GENERATED_BODY()
	UPROPERTY()
	TArray<FCastleCombatScheduledEntry> Entries;
	int64 GlobalIndex = 0;
	int32 PendingBatches = 0;
	bool bAdvanceOnClear = true;
	double Duration = 0.0;
};

/** 世界级战斗调度，仅 Game/PIE；不负责导演流程、奖励、主塔失败界面或敌人 AI。 */
UCLASS()
class MOVING_CASTLE_API UCastleCombatSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	/** 整体校验后替换配置；进行中的战斗拒绝重初始化。失败输出原因及 Error。 */
	bool InitializeCombat(UCastleCombatConfigDataAsset* Config, FText& OutFailureReason);
	/** 正式开战沿用导演的关号，按精英关间隔推导难度表项并校验所选节点类型。 */
	bool StartStageBattle(ECastleBattleKind Kind, int32 Stage, int32 EliteInterval, FText& OutFailureReason);
	/** 教程与调试等独立战斗显式指定从 1 起的表项序号。 */
	bool StartBattle(ECastleBattleKind Kind, int32 TableOrdinal, FText& OutFailureReason);
	bool IsBattleActive() const { return bBattleActive; }
	bool IsCombatInitialized() const { return CombatConfig != nullptr; }
	/** 空闲或计数失败返回 false，调用者不得将失败的零输出当作清场。 */
	bool GetBattleProgress(int32& OutEnemyEntities, int32& OutPendingGroups) const;
	/** 读取本场战斗的已击杀进度（已击杀数与本场总预算数）；空闲或计数失败时返回 false。 */
	bool GetBattleDefeatedProgress(int32& OutDefeatedCount, int32& OutTotalCount) const;
	/** 返回关内从 1 开始的当前波次及总波数，空闲时返回 false。 */
	bool GetCurrentWaveProgress(int32& OutCurrentWaveIndex, int32& OutTotalWaves) const;
	/** 本局敌人门槛状态，开战时快照；空标签是非法调用。 */
	bool UnlockEnemyTag(FName Tag);
	bool HasUnlockedEnemyTag(FName Tag) const;
	void ResetUnlockedEnemyTags();

	FCastleBattleCleared OnBattleCleared;
	FCastleBattleFailed OnBattleFailed;
	FCastleWaveStarted OnWaveStarted;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual bool IsTickable() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	/** 整体预检后生成零延迟组，失败时原子回滚。表项用于出怪计划，编号用于日志与结果通知。 */
	bool StartConfiguredBattle(ECastleBattleKind Kind, int32 TableOrdinal, int32 BattleNumber, FText& OutFailureReason);
	int64 ResolveFirstGlobalWaveIndex(ECastleBattleKind Kind, int32 TableOrdinal) const;
	bool CanAdvanceCurrentWave(double WaveElapsed, int32 EnemyCount) const;
	/** 冻结各波并预检有效条目，构建成功后才允许更改本场状态。 */
	bool BuildScheduledWaves(ECastleBattleKind Kind, int32 TableOrdinal, int64 FirstGlobalWaveIndex,
		int32 ExistingEnemies, TArray<FCastleCombatScheduledWave>& OutWaves,
		int32& OutEstimatedEnemies, int32& OutPendingBatches, FText& OutFailureReason);
	bool SpawnDueBatches(double WaveElapsed, FText& OutFailureReason);
	bool SpawnScheduledBatch(FCastleCombatScheduledEntry& Entry, FText& OutFailureReason);
	void BeginWave(int32 WaveIndex, double GameTime);
	/** 回滚直接回收，避免伤害链路触发死亡分裂。 */
	void RollbackNewSpawners(int32 FirstNewSpawner);
	bool DestroyClearedSpawners();
	void FailBattle(const FText& Reason);
	void CancelForWorldEnd();
	void ResetBattlePlan();

	UPROPERTY(Transient)
	TObjectPtr<UCastleCombatConfigDataAsset> CombatConfig;
	UPROPERTY(Transient)
	TArray<FCastleCombatScheduledWave> Plan;
	/** 包括延迟失败后仍有敌人的旧生成器，空闲时也 Tick 负责其生命周期，不遗留无人管理 Actor。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACastleMassEnemySpawner>> ManagedSpawners;

	TSet<FName> UnlockedEnemyTags;
	bool bBattleActive = false;
	bool bWorldEnding = false;
	ECastleBattleKind ActiveKind = ECastleBattleKind::Normal;
	int32 ActiveBattleNumber = 0;
	int32 ActiveWaveIndex = INDEX_NONE;
	int32 PendingBatches = 0;
	int32 PendingEstimatedEnemies = 0;
	/** 本场战斗用于计算骷髅击杀进度的总敌人数预算；随延迟组真实出场数与分裂增量同步修正。 */
	int64 BattleTotalEnemyBudget = 0;
	double WaveStartGameTime = 0.0;
	float BattleInnerRadius = 0;
	float BattleOuterRadius = 0;
	float BattleHeightOffset = 0;
};
