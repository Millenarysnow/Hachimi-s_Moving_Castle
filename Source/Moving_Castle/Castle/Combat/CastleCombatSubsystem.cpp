#include "Castle/Combat/CastleCombatSubsystem.h"

#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Combat/CastleDifficultyCurveDataAsset.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Units/Mass/CastleMassEnemySpawner.h"
#include "Units/Mass/CastleMassUnitSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "Moving_Castle.h"

namespace CastleCombat
{
	/** 明确定义四个整数的字节组合，不依赖 FName 索引、全局随机流或 Actor 生成顺序。 */
	int32 MakeSeed(const int32 SpawnSeed, const ECastleBattleKind Kind, const int32 Ordinal, const int32 GroupIndex)
	{
		uint32 Hash = 2166136261u;
		for (const uint32 Value : {static_cast<uint32>(SpawnSeed), static_cast<uint32>(Kind), static_cast<uint32>(Ordinal), static_cast<uint32>(GroupIndex)})
		{
			for (uint32 Byte = 0; Byte < 4; ++Byte) { Hash = (Hash ^ ((Value >> (Byte * 8)) & 255u)) * 16777619u; }
		}
		return static_cast<int32>(Hash);
	}

	const TCHAR* KindName(const ECastleBattleKind Kind) { return Kind == ECastleBattleKind::Normal ? TEXT("普通") : TEXT("精英"); }
	/** 单个出场组在尚未生成前的实体数估算，统一采用上下限均值并与真实生成后差值抵消。 */
	int32 EstimateGroupEntities(const FCastleCombatScheduledGroup& Scheduled)
	{
		const int32 MeanCluster = (Scheduled.Enemy.MinClusterCount + Scheduled.Enemy.MaxClusterCount) / 2;
		return Scheduled.Group.Count * FMath::Max(1, MeanCluster);
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

bool UCastleCombatSubsystem::StartBattle(const ECastleBattleKind Kind, const int32 KindOrdinal, FText& OutFailureReason)
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
	if ((Kind != ECastleBattleKind::Normal && Kind != ECastleBattleKind::Elite) || KindOrdinal < 1)
	{
		return Reject(TEXT("Kind 无效或 KindOrdinal 小于 1。"));
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
	TArray<FCastleCombatSpawnGroup> Groups;
	if (!CombatConfig->DifficultyCurve->BuildBattlePlan(Kind, KindOrdinal, Groups, Failure)) { return Reject(Failure.ToString()); }
	TArray<FCastleCombatScheduledGroup> NewPlan;
	TSet<UMassEntityConfigAsset*> CheckedTemplates;
	for (int32 Index = 0; Index < Groups.Num(); ++Index)
	{
		const auto* Enemy = CombatConfig->Roster.FindByPredicate([&Groups, Index](const auto& Entry) { return Entry.EnemyId == Groups[Index].EnemyId; });
		if (!Enemy || static_cast<int64>(Groups[Index].Count) * Enemy->MaxClusterCount > MAX_int32)
		{
			return Reject(FString::Printf(TEXT("Groups[%d]：名册引用缺失或 Count * MaxClusterCount 溢出。"), Index));
		}
		if (!CheckedTemplates.Contains(Enemy->Configuration))
		{
			if (!Enemy->Configuration->GetMutableConfig().ValidateEntityTemplate(*GetWorld())
				|| !Enemy->Configuration->GetOrCreateEntityTemplate(*GetWorld()).IsValid())
			{
				return Reject(FString::Printf(TEXT("Groups[%d].Configuration %s：Mass 模板校验失败，整场拒绝。"), Index, *GetPathNameSafe(Enemy->Configuration)));
			}
			CheckedTemplates.Add(Enemy->Configuration);
		}
		auto& Scheduled = NewPlan.AddDefaulted_GetRef();
		Scheduled.Enemy = *Enemy;
		Scheduled.Group = Groups[Index];
		Scheduled.Seed = CastleCombat::MakeSeed(CombatConfig->SpawnSeed, Kind, KindOrdinal, Index);
	}
	const int32 FirstNewSpawner = ManagedSpawners.Num();
	Plan = MoveTemp(NewPlan);
	BattleInnerRadius = CombatConfig->SpawnInnerRadius;
	BattleOuterRadius = CombatConfig->SpawnOuterRadius;
	BattleHeightOffset = CombatConfig->SpawnHeightOffset;
	ActiveKind = Kind;
	ActiveOrdinal = KindOrdinal;
	BattleStartGameTime = GetWorld()->GetTimeSeconds();
	BattleTotalEnemyBudget = 0;
	bBattleActive = true;
	for (int32 Index = 0; Index < Plan.Num(); ++Index)
	{
		if (Plan[Index].Group.Delay == 0 && !SpawnGroup(Index, Failure))
		{
			RollbackNewSpawners(FirstNewSpawner);
			Plan.Reset();
			BattleTotalEnemyBudget = 0;
			bBattleActive = false;
			return Reject(Failure.ToString());
		}
	}
	int32 InitialAliveEnemies = 0;
	if (!GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>()->CountEnemyEntities(InitialAliveEnemies))
	{
		RollbackNewSpawners(FirstNewSpawner);
		Plan.Reset();
		BattleTotalEnemyBudget = 0;
		bBattleActive = false;
		return Reject(TEXT("开战后统计初始敌人数失败。"));
	}
	// 开战瞬间总预算 = 当前存活敌人数 + 尚未出场组的估算敌人数，确保开战时已击杀进度严格为 0。
	BattleTotalEnemyBudget = InitialAliveEnemies + EstimatePendingGroupEntities();
	const FString Message = FString::Printf(TEXT("开战：第 %d 场%s战斗，%d 个出场组。"), ActiveOrdinal, CastleCombat::KindName(ActiveKind), Plan.Num());
	UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *Message);
	CastleCombat::Screen(Message, true);
	return true;
}

bool UCastleCombatSubsystem::SpawnGroup(const int32 GroupIndex, FText& OutFailureReason)
{
	const auto& Scheduled = Plan[GroupIndex];
	auto* Spawner = GetWorld()->SpawnActorDeferred<ACastleMassEnemySpawner>(ACastleMassEnemySpawner::StaticClass(),
		FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Spawner)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("Groups[%d] %s：无法创建生成器。"), GroupIndex, *Scheduled.Group.EnemyId.ToString()));
		return false;
	}
	ManagedSpawners.Add(Spawner);
	Spawner->bSpawnOnBeginPlay = false;
	Spawner->Configuration = Scheduled.Enemy.Configuration;
	Spawner->Count = Scheduled.Group.Count;
	Spawner->MinClusterCount = Scheduled.Enemy.MinClusterCount;
	Spawner->MaxClusterCount = Scheduled.Enemy.MaxClusterCount;
	Spawner->ClusterSpreadRadius = Scheduled.Enemy.ClusterSpreadRadius;
	Spawner->InnerRadius = BattleInnerRadius;
	Spawner->OuterRadius = BattleOuterRadius;
	Spawner->HeightOffset = BattleHeightOffset;
	Spawner->RandomSeed = Scheduled.Seed;
	Spawner->FinishSpawning(FTransform::Identity);
	if (!Spawner->SpawnUnits())
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("Groups[%d] %s，配置 %s：生成失败，详见 Mass Error。"),
			GroupIndex, *Scheduled.Group.EnemyId.ToString(), *GetPathNameSafe(Scheduled.Enemy.Configuration)));
		return false;
	}
	Plan[GroupIndex].bSpawned = true;
	// 延迟组出场时用真实实体数替换原先估算值，保持已击杀进度平滑不回跳。
	if (BattleTotalEnemyBudget > 0)
	{
		const int32 Delta = Spawner->SpawnedUnits.Num() - CastleCombat::EstimateGroupEntities(Plan[GroupIndex]);
		BattleTotalEnemyBudget = FMath::Max(1, BattleTotalEnemyBudget + Delta);
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("第 %d 场%s战斗：第 %d 组 %s × %d 出场，实体 %d 只。"),
		ActiveOrdinal, CastleCombat::KindName(ActiveKind), GroupIndex + 1, *Scheduled.Group.EnemyId.ToString(), Scheduled.Group.Count, Spawner->SpawnedUnits.Num());
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

int32 UCastleCombatSubsystem::CountPendingGroups() const
{
	int32 Pending = 0;
	for (const auto& Scheduled : Plan) { if (!Scheduled.bSpawned) { ++Pending; } }
	return Pending;
}

int32 UCastleCombatSubsystem::EstimatePendingGroupEntities() const
{
	int32 PendingEntities = 0;
	for (const auto& Scheduled : Plan)
	{
		if (!Scheduled.bSpawned)
		{
			PendingEntities += CastleCombat::EstimateGroupEntities(Scheduled);
		}
	}
	return PendingEntities;
}

bool UCastleCombatSubsystem::GetBattleProgress(int32& OutEnemyEntities, int32& OutPendingGroups) const
{
	OutEnemyEntities = 0;
	OutPendingGroups = 0;
	if (!bBattleActive) { return false; }
	if (!GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>()->CountEnemyEntities(OutEnemyEntities)) { return false; }
	OutPendingGroups = CountPendingGroups();
	return true;
}

bool UCastleCombatSubsystem::GetBattleDefeatedProgress(int32& OutDefeatedCount, int32& OutTotalCount) const
{
	OutDefeatedCount = 0;
	OutTotalCount = 0;
	if (!bBattleActive) { return false; }
	int32 AliveEnemies = 0;
	if (!GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>()->CountEnemyEntities(AliveEnemies)) { return false; }
	const int32 RemainingEnemies = AliveEnemies + EstimatePendingGroupEntities();
	const int32 TotalBudget = FMath::Max(BattleTotalEnemyBudget, RemainingEnemies);
	if (TotalBudget <= 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("战斗配置 %s：活跃战斗的总敌人数预算无效（Total=%d）。"),
			*GetPathNameSafe(CombatConfig), TotalBudget);
		return false;
	}
	OutTotalCount = TotalBudget;
	OutDefeatedCount = FMath::Max(0, TotalBudget - RemainingEnemies);
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
	if (bBattleActive)
	{
		const double Elapsed = GetWorld()->GetTimeSeconds() - BattleStartGameTime;
		for (int32 Index = 0; Index < Plan.Num(); ++Index)
		{
			if (!Plan[Index].bSpawned && Elapsed >= Plan[Index].Group.Delay)
			{
				FText Failure;
				if (!SpawnGroup(Index, Failure)) { FailBattle(Failure); return; }
			}
		}
	}
	int32 Count = 0;
	if (!GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>()->CountEnemyEntities(Count))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("战斗配置 %s：本帧敌人计数失败，不判定清场。"), *GetPathNameSafe(CombatConfig));
		return;
	}
	if (Count != 0)
	{
		// 分裂子体或调试刷兵导致剩余数上升时同步抬高总预算，防止已击杀数倒扣成负数。
		if (bBattleActive)
		{
			BattleTotalEnemyBudget = FMath::Max(BattleTotalEnemyBudget, Count + EstimatePendingGroupEntities());
		}
		return;
	}
	if (!DestroyClearedSpawners()) { return; }
	if (bBattleActive && CountPendingGroups() == 0)
	{
		// 先结束旧状态再通知，委托中允许立即初始化或开始下一场，且不会被旧 Tick 再清理。
		const auto Kind = ActiveKind;
		const int32 Ordinal = ActiveOrdinal;
		bBattleActive = false;
		BattleTotalEnemyBudget = 0;
		Plan.Reset();
		const FString Message = FString::Printf(TEXT("清场：第 %d 场%s战斗。"), Ordinal, CastleCombat::KindName(Kind));
		UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *Message);
		CastleCombat::Screen(Message, true);
		OnBattleCleared.Broadcast(Kind, Ordinal);
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
	const int32 Ordinal = ActiveOrdinal;
	bBattleActive = false;
	BattleTotalEnemyBudget = 0;
	Plan.Reset();
	UE_LOG(LogMoving_Castle, Error, TEXT("战斗失败：配置 %s，第 %d 场%s战斗：%s"),
		*GetPathNameSafe(CombatConfig), Ordinal, CastleCombat::KindName(Kind), *Reason.ToString());
	CastleCombat::Screen(FString::Printf(TEXT("战斗失败：%s"), *Reason.ToString()), false);
	// 已出场敌人保留；ManagedSpawners 在空闲 Tick 中继续管理直到全部回收，绝不接着广播清场。
	OnBattleFailed.Broadcast(Kind, Ordinal, Reason);
}

void UCastleCombatSubsystem::CancelForWorldEnd()
{
	bWorldEnding = true;
	bBattleActive = false;
	BattleTotalEnemyBudget = 0;
	Plan.Reset();
	ManagedSpawners.Reset();
	CombatConfig = nullptr;
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
