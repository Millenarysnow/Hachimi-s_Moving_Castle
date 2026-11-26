#include "Castle/Combat/CastleCombatSubsystem.h"

#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Combat/CastleDifficultyCurveDataAsset.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Units/Mass/CastleMassEnemySpawner.h"
#include "Units/Mass/CastleMassFragments.h"
#include "Units/Mass/CastleMassUnitSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "Moving_Castle.h"

namespace CastleCombat
{
	/** 显式字节序的种子哈希，独立于 FName 索引、进程随机流及生成器创建顺序。 */
	int32 MakeSeed(std::initializer_list<uint32> Values)
	{
		uint32 Hash = 2166136261u;
		for (const uint32 Value : Values)
		{
			for (uint32 Byte = 0; Byte < 4; ++Byte) { Hash = (Hash ^ ((Value >> (Byte * 8)) & 255u)) * 16777619u; }
		}
		return static_cast<int32>(Hash);
	}

	const TCHAR* KindName(const ECastleBattleKind Kind) { return Kind == ECastleBattleKind::Normal ? TEXT("普通") : TEXT("精英"); }
	/** 开战前验证过总上界，估算使用簇上下界均值，与每批实际出场差值抵消。 */
	int32 EstimateEntities(const FCastleCombatEnemyEntry& Enemy, const int32 Points)
	{
		const int64 MeanCluster = (static_cast<int64>(Enemy.MinClusterCount) + Enemy.MaxClusterCount) / 2;
		return static_cast<int32>(Points * MeanCluster);
	}

	/** 每个出场条目只保存一个批次序列，统计上界时以所有圆环点计算。 */
	bool PrepareScheduledEntry(UWorld& World, const UCastleCombatConfigDataAsset& Config, TSet<UMassEntityConfigAsset*>& CheckedTemplates,
		const FName EnemyId, const int32 Points, const int32 BatchSize, const double FirstDelay, const double Interval,
		const FCastleMassEnemyAttributeOverrides& Overrides, const FString& FieldPath, const int32 Seed,
		FCastleCombatScheduledWave& Wave, int64& MaxEnemyBudget, int32& EstimatedEnemies, FText& OutFailureReason)
	{
		const auto Reject = [&OutFailureReason, &FieldPath](const FString& Reason)
		{
			OutFailureReason = FText::FromString(FieldPath + TEXT("：") + Reason);
			return false;
		};
		const auto* Enemy = Config.Roster.FindByPredicate([EnemyId](const auto& Entry) { return Entry.EnemyId == EnemyId; });
		if (!Enemy) { return Reject(TEXT("EnemyId 在名册中不存在。")); }
		const int64 MaxForEntry = static_cast<int64>(Points) * Enemy->MaxClusterCount;
		if (MaxEnemyBudget + MaxForEntry > MAX_int32)
		{
			return Reject(TEXT("Count × MaxClusterCount 或整场实体预算超出 int32。"));
		}
		if (!CheckedTemplates.Contains(Enemy->Configuration))
		{
			if (!Enemy->Configuration->GetMutableConfig().ValidateEntityTemplate(World))
			{
				return Reject(FString::Printf(TEXT("Configuration %s：Mass 模板校验失败。"), *GetPathNameSafe(Enemy->Configuration)));
			}
			CheckedTemplates.Add(Enemy->Configuration);
		}
		const auto& Template = Enemy->Configuration->GetOrCreateEntityTemplate(World);
		if (!Template.IsValid())
		{
			return Reject(FString::Printf(TEXT("Configuration %s：无法创建有效的 Mass 模板。"), *GetPathNameSafe(Enemy->Configuration)));
		}
		FText OverrideFailure;
		if (!UCastleMassUnitSubsystem::ValidateEnemySpawnOverrides(Template, Overrides, OverrideFailure))
		{
			return Reject(FString::Printf(TEXT("Configuration %s：%s"), *GetPathNameSafe(Enemy->Configuration), *OverrideFailure.ToString()));
		}
		MaxEnemyBudget += MaxForEntry;
		EstimatedEnemies += EstimateEntities(*Enemy, Points);
		Wave.PendingBatches += 1 + (Points - 1) / BatchSize;
		auto& Entry = Wave.Entries.AddDefaulted_GetRef();
		Entry.Enemy = *Enemy;
		Entry.AttributeOverrides = Overrides;
		Entry.FieldPath = FieldPath;
		Entry.RemainingPoints = Points;
		Entry.BatchSize = BatchSize;
		Entry.FirstDelay = FirstDelay;
		Entry.Interval = Interval;
		Entry.Seed = Seed;
		return true;
	}
	void Screen(const FString& Message, const bool bSuccess)
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 8.0f, bSuccess ? FColor::Green : FColor::Red, Message); }
	}
}

void UCastleCombatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UCastleMainTowerSubsystem>();
	Collection.InitializeDependency<UCastleMassUnitSubsystem>();
	Collection.InitializeDependency<UMassEntitySubsystem>();
	Super::Initialize(Collection);
}

bool UCastleCombatSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UCastleCombatSubsystem::IsTickable() const
{
	return IsInitialized() && !bWorldEnding && (bBattleActive || !ManagedSpawners.IsEmpty());
}

TStatId UCastleCombatSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCastleCombatSubsystem, STATGROUP_Tickables);
}

bool UCastleCombatSubsystem::InitializeCombat(UCastleCombatConfigDataAsset* Config, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (!IsInGameThread() || bWorldEnding || bBattleActive || !IsValid(Config))
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("战斗配置 %s：InitializeCombat 需要游戏线程、空闲且未结束的世界及有效配置。"), *GetPathNameSafe(Config)));
	}
	else if (Config->ValidateConfiguration(OutFailureReason))
	{
		CombatConfig = Config;
		return true;
	}
	UE_LOG(LogMoving_Castle, Error, TEXT("InitializeCombat：%s"), *OutFailureReason.ToString());
	return false;
}

bool UCastleCombatSubsystem::BuildScheduledWaves(const ECastleBattleKind Kind, const int32 TableOrdinal,
	const int64 FirstGlobalWaveIndex, const int32 ExistingEnemies, TArray<FCastleCombatScheduledWave>& OutWaves,
	int32& OutEstimatedEnemies, int32& OutPendingBatches, FText& OutFailureReason)
{
	TArray<FCastleCombatWaveDefinition> Waves;
	const auto* Curve = CombatConfig->DifficultyCurve.Get();
	if (!Curve->BuildBattlePlan(Kind, TableOrdinal, Waves, OutFailureReason)) { return false; }
	if (FirstGlobalWaveIndex > MAX_int64 - (Waves.Num() - 1))
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("难度曲线 %s：连续波次序号超出 int64。"), *Curve->GetPathName()));
		return false;
	}
	const bool bNormal = Kind == ECastleBattleKind::Normal;
	const auto& Table = bNormal ? Curve->NormalBattles : Curve->EliteBattles;
	const int32 SourceIndex = FMath::Min(TableOrdinal, Table.Num()) - 1;
	const FString RowPath = FString::Printf(TEXT("%s.%s[%d]"), *Curve->GetPathName(),
		bNormal ? TEXT("NormalBattles") : TEXT("EliteBattles"), SourceIndex);
	// 整关解析在同一个游戏线程调用内完成。后续波次只读此时确定的条目集合。
	const TSet<FName> UnlockSnapshot = UnlockedEnemyTags;
	TSet<UMassEntityConfigAsset*> CheckedTemplates;
	int64 MaxEnemyBudget = ExistingEnemies;
	int32 EstimatedEnemies = 0;
	int32 PlannedBatches = 0;
	TArray<FCastleCombatScheduledWave> NewPlan;
	NewPlan.Reserve(Waves.Num());
	for (int32 WaveIndex = 0; WaveIndex < Waves.Num(); ++WaveIndex)
	{
		const auto& Definition = Waves[WaveIndex];
		const FString WavePath = FString::Printf(TEXT("%s.Waves[%d]"), *RowPath, WaveIndex);
		auto& Wave = NewPlan.AddDefaulted_GetRef();
		Wave.bAdvanceOnClear = Definition.bAdvanceOnClear;
		Wave.Duration = Definition.WaveDuration;
		Wave.GlobalIndex = FirstGlobalWaveIndex + WaveIndex;
		const auto AddEntry = [&](const FName EnemyId, const int32 Points, const int32 BatchSize,
			const double Delay, const double Interval, const FCastleMassEnemyAttributeOverrides& Attributes,
			const FString& FieldPath, const int32 RaidIndex)
		{
			// 原始条目下标和波次参与种子，解锁过滤不会改变其他条目的位置随机流。
			const int32 Seed = CastleCombat::MakeSeed({static_cast<uint32>(CombatConfig->SpawnSeed), static_cast<uint32>(Kind),
				static_cast<uint32>(TableOrdinal), static_cast<uint32>(WaveIndex), static_cast<uint32>(RaidIndex)});
			return CastleCombat::PrepareScheduledEntry(*GetWorld(), *CombatConfig, CheckedTemplates,
				EnemyId, Points, BatchSize, Delay, Interval, Attributes, FieldPath, Seed, Wave,
				MaxEnemyBudget, EstimatedEnemies, OutFailureReason);
		};
		const auto& Bug = Definition.BugCurve;
		if (Bug.bEnabled && !AddEntry(Bug.BugEnemyId, Bug.GetSpawnPointCount(Wave.GlobalIndex), Bug.BatchSize,
			0.0, Bug.SpawnInterval, {}, WavePath + TEXT(".BugCurve"), INDEX_NONE)) { return false; }

		for (int32 RaidIndex = 0; RaidIndex < Definition.RaidEntries.Num(); ++RaidIndex)
		{
			const auto& Raid = Definition.RaidEntries[RaidIndex];
			const FString RaidPath = FString::Printf(TEXT("%s.RaidEntries[%d]"), *WavePath, RaidIndex);
			if (Wave.GlobalIndex < Raid.MinWaveIndex)
			{
				UE_LOG(LogMoving_Castle, Display, TEXT("战斗配置 %s：%s 的 %s 连续波次 %lld 未达到下限 %d，未纳入本场计划。"),
					*CombatConfig->GetPathName(), *RaidPath, *Raid.EnemyId.ToString(), Wave.GlobalIndex, Raid.MinWaveIndex);
				continue;
			}
			if (!Raid.RequiredUnlockTag.IsNone() && !UnlockSnapshot.Contains(Raid.RequiredUnlockTag))
			{
				UE_LOG(LogMoving_Castle, Display, TEXT("战斗配置 %s：%s 的 %s 因未解锁 %s，未纳入本场计划。"),
					*CombatConfig->GetPathName(), *RaidPath, *Raid.EnemyId.ToString(), *Raid.RequiredUnlockTag.ToString());
				continue;
			}
			// 正间隔每批一个圆环点。零间隔用一个批次承载全部点，保留同时生成。
			const int32 BatchSize = Raid.SpawnInterval == 0.0f ? Raid.Count : 1;
			if (!AddEntry(Raid.EnemyId, Raid.Count, BatchSize, Raid.TriggerDelay, Raid.SpawnInterval,
				Raid.AttributeOverrides, RaidPath, RaidIndex)) { return false; }
		}
		if (Wave.Entries.IsEmpty())
		{
			OutFailureReason = FText::FromString(WavePath + TEXT("：筛选后没有可出场的虫群或突袭条目。"));
			return false;
		}
		PlannedBatches += Wave.PendingBatches;
	}
	OutWaves = MoveTemp(NewPlan);
	OutEstimatedEnemies = EstimatedEnemies;
	OutPendingBatches = PlannedBatches;
	return true;
}

int64 UCastleCombatSubsystem::ResolveFirstGlobalWaveIndex(const ECastleBattleKind Kind, const int32 TableOrdinal) const
{
	const auto& Curve = *CombatConfig->DifficultyCurve;
	const auto& Table = Kind == ECastleBattleKind::Normal ? Curve.NormalBattles : Curve.EliteBattles;
	const int32 PriorBattles = TableOrdinal - 1;
	const int32 InTable = FMath::Min(PriorBattles, Table.Num());
	// 空岛占用的表项也参与静态累计；超过末项后，每关沿用末项波数。
	int64 Index = 1 + static_cast<int64>(PriorBattles - InTable) * Table.Last().Waves.Num();
	for (int32 Battle = 0; Battle < InTable; ++Battle) { Index += Table[Battle].Waves.Num(); }
	return Index;
}

bool UCastleCombatSubsystem::CanAdvanceCurrentWave(const double WaveElapsed, const int32 EnemyCount) const
{
	const auto& Wave = Plan[ActiveWaveIndex];
	// 两种模式都必须先出完本波所有批次。定时时长从本波实际启动时计算。
	return Wave.PendingBatches == 0 && (Wave.bAdvanceOnClear ? EnemyCount == 0 : WaveElapsed >= Wave.Duration);
}

bool UCastleCombatSubsystem::StartStageBattle(const ECastleBattleKind Kind, const int32 Stage,
	const int32 EliteInterval, FText& OutFailureReason)
{
	if (Stage < 1 || EliteInterval < 2 || Kind != CastleCombat::GetBattleKindForStage(Stage, EliteInterval))
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("战斗配置 %s：关号 %d、节点战斗类型 %d 或精英关间隔 %d 无效。"),
			*GetPathNameSafe(CombatConfig), Stage, static_cast<int32>(Kind), EliteInterval));
		UE_LOG(LogMoving_Castle, Error, TEXT("StartStageBattle：%s"), *OutFailureReason.ToString());
		return false;
	}
	// 普通表按关号扣除精英关。空岛占用所在的普通关位置，同一关号始终取同一表项。
	const int32 EliteStages = Stage / EliteInterval;
	const int32 TableOrdinal = Kind == ECastleBattleKind::Elite ? EliteStages : Stage - EliteStages;
	return StartConfiguredBattle(Kind, TableOrdinal, Stage, OutFailureReason);
}

bool UCastleCombatSubsystem::StartBattle(const ECastleBattleKind Kind, const int32 TableOrdinal, FText& OutFailureReason)
{
	// 独立战斗以调用方指定的表项为编号，使用同一套波次计划与清场事务。
	return StartConfiguredBattle(Kind, TableOrdinal, TableOrdinal, OutFailureReason);
}

bool UCastleCombatSubsystem::StartConfiguredBattle(const ECastleBattleKind Kind, const int32 TableOrdinal,
	const int32 BattleNumber, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	const auto Reject = [this, &OutFailureReason](const FString& Reason)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("战斗配置 %s：%s"), *GetPathNameSafe(CombatConfig), *Reason));
		UE_LOG(LogMoving_Castle, Error, TEXT("StartBattle：%s"), *OutFailureReason.ToString());
		return false;
	};
	if (!IsInGameThread() || bWorldEnding) { return Reject(TEXT("只能在游戏线程且世界未结束时开战。")); }
	if (!IsValid(CombatConfig)) { return Reject(TEXT("战斗系统未初始化。")); }
	if (bBattleActive) { return Reject(TEXT("已在战斗中，拒绝重复开战。")); }
	if ((Kind != ECastleBattleKind::Normal && Kind != ECastleBattleKind::Elite) || TableOrdinal < 1)
	{
		return Reject(TEXT("战斗类型无效或表项序号小于 1。"));
	}
	auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!Mass || Mass->GetEntityManager().IsProcessing()) { return Reject(TEXT("Mass 不可用或正在处理，不能开战。")); }
	FVector TowerLocation;
	if (!GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>()->GetMainTowerTargetLocation(TowerLocation))
	{
		return Reject(TEXT("当前世界没有存活主塔。"));
	}
	// 每次开战重新校验，防止初始化后编辑器修改 DA 绕过 D2；本场计划随后冻结。
	FText Failure;
	if (!CombatConfig->ValidateConfiguration(Failure)) { return Reject(Failure.ToString()); }
	const auto* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
	int32 InitialAliveEnemies = 0;
	if (!Units->CountEnemyEntities(InitialAliveEnemies)) { return Reject(TEXT("开战前统计敌人数失败。")); }
	const int64 FirstGlobalWaveIndex = ResolveFirstGlobalWaveIndex(Kind, TableOrdinal);
	TArray<FCastleCombatScheduledWave> NewPlan;
	int32 NewEstimatedEnemies = 0, NewPendingBatches = 0;
	if (!BuildScheduledWaves(Kind, TableOrdinal, FirstGlobalWaveIndex, InitialAliveEnemies, NewPlan,
		NewEstimatedEnemies, NewPendingBatches, Failure)) { return Reject(Failure.ToString()); }

	const int32 FirstNewSpawner = ManagedSpawners.Num();
	Plan = MoveTemp(NewPlan);
	BattleInnerRadius = CombatConfig->SpawnInnerRadius;
	BattleOuterRadius = CombatConfig->SpawnOuterRadius;
	BattleHeightOffset = CombatConfig->SpawnHeightOffset;
	ActiveKind = Kind;
	ActiveBattleNumber = BattleNumber;
	PendingBatches = NewPendingBatches;
	PendingEstimatedEnemies = NewEstimatedEnemies;
	BattleTotalEnemyBudget = static_cast<int64>(InitialAliveEnemies) + PendingEstimatedEnemies;
	bBattleActive = true;
	BeginWave(0, GetWorld()->GetTimeSeconds());
	if (!SpawnDueBatches(0.0, Failure))
	{
		RollbackNewSpawners(FirstNewSpawner);
		ResetBattlePlan();
		return Reject(Failure.ToString());
	}
	if (!Units->CountEnemyEntities(InitialAliveEnemies) || static_cast<int64>(InitialAliveEnemies) + PendingEstimatedEnemies > MAX_int32)
	{
		RollbackNewSpawners(FirstNewSpawner);
		ResetBattlePlan();
		return Reject(TEXT("开战后统计初始敌人数失败或总预算超出 int32。"));
	}
	// 首批按真实数量计入，其他批次按估算计入；开战时进度为零。
	BattleTotalEnemyBudget = static_cast<int64>(InitialAliveEnemies) + PendingEstimatedEnemies;
	const FString Message = FString::Printf(TEXT("开战：%s战斗（编号 %d），共 %d 波。"), CastleCombat::KindName(ActiveKind), ActiveBattleNumber, Plan.Num());
	UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *Message);
	CastleCombat::Screen(Message, true);
	return true;
}

void UCastleCombatSubsystem::BeginWave(const int32 WaveIndex, const double GameTime)
{
	ActiveWaveIndex = WaveIndex;
	WaveStartGameTime = GameTime;
	const FString Message = FString::Printf(TEXT("%s战斗（编号 %d）：第 %d/%d 波启动，连续波次 %lld。"),
		CastleCombat::KindName(ActiveKind), ActiveBattleNumber, ActiveWaveIndex + 1, Plan.Num(), Plan[WaveIndex].GlobalIndex);
	UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *Message);
	CastleCombat::Screen(Message, true);
}

bool UCastleCombatSubsystem::SpawnDueBatches(const double WaveElapsed, FText& OutFailureReason)
{
	auto& Wave = Plan[ActiveWaveIndex];
	while (Wave.PendingBatches > 0)
	{
		// 一帧跨过多个时刻时按最早到期批次出场。同一时刻沿用冻结数组顺序。
		FCastleCombatScheduledEntry* Due = nullptr;
		for (auto& Entry : Wave.Entries)
		{
			if (Entry.RemainingPoints > 0 && Entry.NextBatchDelay() <= WaveElapsed
				&& (!Due || Entry.NextBatchDelay() < Due->NextBatchDelay()))
			{
				Due = &Entry;
			}
		}
		if (!Due) { break; }
		if (!SpawnScheduledBatch(*Due, OutFailureReason)) { return false; }
		--Wave.PendingBatches;
		--PendingBatches;
	}
	return true;
}

bool UCastleCombatSubsystem::SpawnScheduledBatch(FCastleCombatScheduledEntry& Entry, FText& OutFailureReason)
{
	const int32 Points = Entry.NextBatchSize();
	auto* Spawner = GetWorld()->SpawnActorDeferred<ACastleMassEnemySpawner>(ACastleMassEnemySpawner::StaticClass(),
		FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Spawner)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("%s %s：无法创建生成器。"), *Entry.FieldPath, *Entry.Enemy.EnemyId.ToString()));
		return false;
	}
	ManagedSpawners.Add(Spawner);
	Spawner->bSpawnOnBeginPlay = false;
	Spawner->Configuration = Entry.Enemy.Configuration;
	Spawner->AttributeOverrides = Entry.AttributeOverrides;
	Spawner->Count = Points;
	Spawner->MinClusterCount = Entry.Enemy.MinClusterCount;
	Spawner->MaxClusterCount = Entry.Enemy.MaxClusterCount;
	Spawner->ClusterSpreadRadius = Entry.Enemy.ClusterSpreadRadius;
	Spawner->InnerRadius = BattleInnerRadius;
	Spawner->OuterRadius = BattleOuterRadius;
	Spawner->HeightOffset = BattleHeightOffset;
	Spawner->RandomSeed = CastleCombat::MakeSeed({static_cast<uint32>(Entry.Seed), static_cast<uint32>(Entry.SpawnedBatches)});
	Spawner->FinishSpawning(FTransform::Identity);
	if (!Spawner->SpawnUnits() || Spawner->SpawnedUnits.IsEmpty())
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("%s %s，配置 %s：生成失败，详见 Mass Error。"),
			*Entry.FieldPath, *Entry.Enemy.EnemyId.ToString(), *GetPathNameSafe(Entry.Enemy.Configuration)));
		return false;
	}
	// 用本批的真实实体数替换原预算；未出场的其他波次仍保留在总预算中。
	const int32 Estimated = CastleCombat::EstimateEntities(Entry.Enemy, Points);
	PendingEstimatedEnemies -= Estimated;
	BattleTotalEnemyBudget += Spawner->SpawnedUnits.Num() - Estimated;
	Entry.RemainingPoints -= Points;
	++Entry.SpawnedBatches;
	const auto& Manager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetEntityManager();
	const float Health = Manager.GetFragmentDataChecked<FCastleMassUnitFragment>(Spawner->SpawnedUnits[0].Entity).Health;
	UE_LOG(LogMoving_Castle, Display, TEXT("%s战斗（编号 %d），第 %d 波：%s 第 %d 批 %s × %d 点，实体 %d 只，首只血量 %g（覆写倍率 %g）。"),
		CastleCombat::KindName(ActiveKind), ActiveBattleNumber, ActiveWaveIndex + 1, *Entry.FieldPath, Entry.SpawnedBatches,
		*Entry.Enemy.EnemyId.ToString(), Points, Spawner->SpawnedUnits.Num(), Health, Entry.AttributeOverrides.HealthMultiplier);
	return true;
}

void UCastleCombatSubsystem::RollbackNewSpawners(const int32 FirstNewSpawner)
{
	auto& Manager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	// 本函数仅在同一次 StartBattle 的安全边界内执行，尚未让 Mass 跑过死亡或分裂链路。
	for (int32 Index = ManagedSpawners.Num() - 1; Index >= FirstNewSpawner; --Index)
	{
		auto* Spawner = ManagedSpawners[Index].Get();
		for (const auto& Handle : Spawner->SpawnedUnits) { Manager.DestroyEntity(Handle.Entity); }
		// 清空后再 Destroy，防止 EndPlay 的正常 DespawnUnits 再提交死亡伤害。
		Spawner->SpawnedUnits.Reset();
		Spawner->Destroy();
		ManagedSpawners.RemoveAt(Index);
	}
}

bool UCastleCombatSubsystem::GetCurrentWaveProgress(int32& OutCurrentWaveIndex, int32& OutTotalWaves) const
{
	OutCurrentWaveIndex = 0;
	OutTotalWaves = 0;
	if (!bBattleActive) { return false; }
	OutCurrentWaveIndex = ActiveWaveIndex + 1;
	OutTotalWaves = Plan.Num();
	return true;
}

bool UCastleCombatSubsystem::UnlockEnemyTag(const FName Tag)
{
	if (!IsInGameThread() || bWorldEnding || Tag.IsNone())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("UnlockEnemyTag：世界 %s 需要在游戏线程且世界未结束时传入非空标签。"), *GetPathNameSafe(GetWorld()));
		return false;
	}
	UnlockedEnemyTags.Add(Tag);
	return true;
}

bool UCastleCombatSubsystem::HasUnlockedEnemyTag(const FName Tag) const
{
	if (!IsInGameThread() || Tag.IsNone())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("HasUnlockedEnemyTag：世界 %s 需要在游戏线程查询非空标签。"), *GetPathNameSafe(GetWorld()));
		return false;
	}
	return UnlockedEnemyTags.Contains(Tag);
}

void UCastleCombatSubsystem::ResetUnlockedEnemyTags()
{
	if (!IsInGameThread())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("ResetUnlockedEnemyTags：世界 %s 需要在游戏线程重置标签。"), *GetPathNameSafe(GetWorld()));
		return;
	}
	UnlockedEnemyTags.Reset();
}

bool UCastleCombatSubsystem::GetBattleProgress(int32& OutEnemyEntities, int32& OutPendingGroups) const
{
	OutEnemyEntities = 0;
	OutPendingGroups = 0;
	if (!bBattleActive) { return false; }
	if (!GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>()->CountEnemyEntities(OutEnemyEntities)) { return false; }
	OutPendingGroups = PendingBatches;
	return true;
}

bool UCastleCombatSubsystem::GetBattleDefeatedProgress(int32& OutDefeatedCount, int32& OutTotalCount) const
{
	OutDefeatedCount = 0;
	OutTotalCount = 0;
	if (!bBattleActive) { return false; }
	int32 AliveEnemies = 0;
	if (!GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>()->CountEnemyEntities(AliveEnemies)) { return false; }
	const int64 RemainingEnemies = static_cast<int64>(AliveEnemies) + PendingEstimatedEnemies;
	const int64 TotalBudget = FMath::Max(BattleTotalEnemyBudget, RemainingEnemies);
	if (TotalBudget <= 0 || TotalBudget > MAX_int32)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("战斗配置 %s：活跃战斗的总敌人数预算无效（Total=%lld）。"),
			*GetPathNameSafe(CombatConfig), TotalBudget);
		return false;
	}
	OutTotalCount = static_cast<int32>(TotalBudget);
	OutDefeatedCount = static_cast<int32>(TotalBudget - RemainingEnemies);
	return true;
}

void UCastleCombatSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bWorldEnding || GetWorld()->IsPaused()) { return; }
	auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!IsInGameThread() || !Mass || Mass->GetEntityManager().IsProcessing())
	{
		// 当前项目在 PostPhysics 后 Tick，正常不会进入这里；显式拒绝非法边界，不能触发生成失败或误清场。
		UE_LOG(LogMoving_Castle, Error, TEXT("战斗配置 %s：Tick 需要游戏线程且 Mass 未处理，本帧不调度、不判定清场。"), *GetPathNameSafe(CombatConfig));
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const double Elapsed = Now - WaveStartGameTime;
	FText Failure;
	if (bBattleActive && !SpawnDueBatches(Elapsed, Failure)) { FailBattle(Failure); return; }
	int32 Count = 0;
	if (!GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>()->CountEnemyEntities(Count))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("战斗配置 %s：本帧敌人计数失败，不判定清场。"), *GetPathNameSafe(CombatConfig));
		return;
	}
	if (Count == 0 && !DestroyClearedSpawners()) { return; }
	if (!bBattleActive) { return; }
	// 保留分裂和外部刷兵引入的实体，所有未来批次仍计入预算。
	BattleTotalEnemyBudget = FMath::Max(BattleTotalEnemyBudget, static_cast<int64>(Count) + PendingEstimatedEnemies);
	if (ActiveWaveIndex + 1 < Plan.Num())
	{
		if (CanAdvanceCurrentWave(Elapsed, Count))
		{
			BeginWave(ActiveWaveIndex + 1, Now);
			if (!SpawnDueBatches(0.0, Failure)) { FailBattle(Failure); }
		}
		return;
	}
	if (Count == 0 && PendingBatches == 0)
	{
		// 先结束旧状态再通知，委托中允许立即初始化或开始下一场，且不会被旧 Tick 再清理。
		const auto Kind = ActiveKind;
		const int32 BattleNumber = ActiveBattleNumber;
		ResetBattlePlan();
		const FString Message = FString::Printf(TEXT("清场：%s战斗（编号 %d）。"), CastleCombat::KindName(Kind), BattleNumber);
		UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *Message);
		CastleCombat::Screen(Message, true);
		OnBattleCleared.Broadcast(Kind, BattleNumber);
	}
}

bool UCastleCombatSubsystem::DestroyClearedSpawners()
{
	// 生成器没有自动销毁寿命，战斗系统应始终持有它们；异常引用不能静默过滤后冒充清场成功。
	for (int32 Index = 0; Index < ManagedSpawners.Num(); ++Index)
	{
		if (!IsValid(ManagedSpawners[Index]))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("战斗配置 %s：ManagedSpawners[%d] 已失效，停止清理与清场判定。"), *GetPathNameSafe(CombatConfig), Index);
			return false;
		}
	}
	// 全世界 EnemyTag 已归零，旧句柄仅是历史记录。清空再销毁避免 EndPlay 提交无意义的伤害。
	for (const auto& Entry : ManagedSpawners)
	{
		auto* Spawner = Entry.Get();
		Spawner->SpawnedUnits.Reset();
		Spawner->Destroy();
	}
	ManagedSpawners.Reset();
	return true;
}

void UCastleCombatSubsystem::FailBattle(const FText& Reason)
{
	const auto Kind = ActiveKind;
	const int32 BattleNumber = ActiveBattleNumber;
	ResetBattlePlan();
	UE_LOG(LogMoving_Castle, Error, TEXT("战斗失败：配置 %s，%s战斗（编号 %d）：%s"),
		*GetPathNameSafe(CombatConfig), CastleCombat::KindName(Kind), BattleNumber, *Reason.ToString());
	CastleCombat::Screen(FString::Printf(TEXT("战斗失败：%s"), *Reason.ToString()), false);
	// 已出场敌人保留；ManagedSpawners 在空闲 Tick 中继续管理直到全部回收，绝不接着广播清场。
	OnBattleFailed.Broadcast(Kind, BattleNumber, Reason);
}

void UCastleCombatSubsystem::CancelForWorldEnd()
{
	bWorldEnding = true;
	ResetBattlePlan();
	UnlockedEnemyTags.Reset();
	ManagedSpawners.Reset();
	CombatConfig = nullptr;
}

void UCastleCombatSubsystem::ResetBattlePlan()
{
	bBattleActive = false;
	ActiveWaveIndex = INDEX_NONE;
	PendingBatches = 0;
	PendingEstimatedEnemies = 0;
	BattleTotalEnemyBudget = 0;
	Plan.Reset();
}

void UCastleCombatSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	CancelForWorldEnd();
	Super::OnWorldEndPlay(InWorld);
}

void UCastleCombatSubsystem::Deinitialize()
{
	CancelForWorldEnd();
	OnBattleCleared.Clear();
	OnBattleFailed.Clear();
	Super::Deinitialize();
}
