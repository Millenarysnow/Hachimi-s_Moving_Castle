#include "CastleNavigationSubsystem.h"

#include "AssemblyFlightComponent.h"
#include "BuildingMergeComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeRWLock.h"
#include "Moving_Castle.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

bool UCastleNavigationSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UNavigationSystemV1* UCastleNavigationSubsystem::GetNavigation() const
{
	return FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
}

bool UCastleNavigationSubsystem::BeginGeometryChange(UObject* Source)
{
	check(IsInGameThread());
	UNavigationSystemV1* Navigation = GetNavigation();
	// 没有导航系统的纯逻辑世界仍可建造；真正的导航请求会明确报告未配置的错误。
	if (bShuttingDown || !Source || !Navigation || GetWorld()->bIsTearingDown) { return false; }
	const bool bFirstChange = ActiveChangeCount == 0;
	++Changes.FindOrAdd(Source);
	++ActiveChangeCount;
	bRefreshPending = true;
	LastChangeFrame = GFrameCounter;
	if (bFirstChange)
	{
		Navigation->AddNavigationBuildLock(ENavigationBuildLock::Custom);
		LockedNavigation = Navigation;
		if (bBuildingNavigation)
		{
			// 更新期间再次修改地图：旧异步构建不发布，新一轮包含被取消构建的所有变化。
			Navigation->CancelBuild();
			bBuildingNavigation = false;
		}
		InvalidatePath();
	}
	return true;
}

void UCastleNavigationSubsystem::EndGeometryChange(UObject* Source)
{
	check(IsInGameThread());
	int32* Depth = Changes.Find(Source);
	if (!Depth) { return; }
	if (--*Depth == 0) { Changes.Remove(Source); }
	--ActiveChangeCount;
	LastChangeFrame = GFrameCounter;
	// 结束信号可能同步引发后续合并。这里只排队，绝不在信号回调中立即解锁。
}

void UCastleNavigationSubsystem::ObserveAssembly(UAssemblyFlightComponent* Flight)
{
	if (Flight && !Changes.Contains(Flight) && BeginGeometryChange(Flight))
	{
		Flight->OnAssemblyFinishedNative.AddUObject(this, &UCastleNavigationSubsystem::HandleAssemblyFinished);
	}
}

void UCastleNavigationSubsystem::ObserveMerge(UBuildingMergeComponent* Merge)
{
	if (Merge && !Changes.Contains(Merge) && BeginGeometryChange(Merge))
	{
		Merge->OnMergeFinishedNative.AddUObject(this, &UCastleNavigationSubsystem::HandleMergeFinished);
	}
}

void UCastleNavigationSubsystem::HandleAssemblyFinished(UAssemblyFlightComponent* Flight, bool bCompleted)
{
	Flight->OnAssemblyFinishedNative.RemoveAll(this);
	EndGeometryChange(Flight);
}

void UCastleNavigationSubsystem::HandleMergeFinished(UBuildingMergeComponent* Merge, bool bCompleted)
{
	Merge->OnMergeFinishedNative.RemoveAll(this);
	EndGeometryChange(Merge);
}

bool UCastleNavigationSubsystem::ValidateNavigation(UNavigationSystemV1*& OutNavigation, ANavigationData*& OutData, FText& OutFailureReason) const
{
	OutNavigation = GetNavigation();
	OutData = OutNavigation ? OutNavigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
	if (!OutData)
	{
		OutFailureReason = FText::FromString(TEXT("未找到导航数据，请在关卡放置覆盖建造区域的 NavMeshBoundsVolume。"));
		return false;
	}
	if (OutData->GetRuntimeGenerationMode() != ERuntimeGenerationType::Dynamic)
	{
		OutFailureReason = FText::FromString(TEXT("Recast NavMesh 需要 Runtime Generation=Dynamic，以支持运行时新增楼板和楼梯。"));
		return false;
	}
	return true;
}

bool UCastleNavigationSubsystem::RequestNavigationRefresh(FText& OutFailureReason)
{
	check(IsInGameThread());
	OutFailureReason = FText::GetEmpty();
	UNavigationSystemV1* Navigation;
	ANavigationData* Data;
	if (!ValidateNavigation(Navigation, Data, OutFailureReason)) { return false; }
	// 显式刷新供 PCG/关卡初始化使用；多次请求合并到下一次提交。
	bRefreshPending = true;
	LastChangeFrame = GFrameCounter;
	InvalidatePath();
	return true;
}

bool UCastleNavigationSubsystem::RequestSharedPath(FVector Start, FVector Goal, FText& OutFailureReason)
{
	check(IsInGameThread());
	OutFailureReason = FText::GetEmpty();
	UNavigationSystemV1* Navigation;
	ANavigationData* Data;
	if (Start.ContainsNaN() || Goal.ContainsNaN() || ProjectionExtent.ContainsNaN() || ProjectionExtent.GetMin() <= 0.0)
	{
		OutFailureReason = FText::FromString(TEXT("共享路线的起终点和导航投影范围必须有效。"));
		return false;
	}
	if (!ValidateNavigation(Navigation, Data, OutFailureReason)) { return false; }
	if (bHasPathRequest && RequestedStart.Equals(Start) && RequestedGoal.Equals(Goal)) { return true; }
	RequestedStart = Start;
	RequestedGoal = Goal;
	bHasPathRequest = true;
	bPathPending = true;
	LastFailure = FText::GetEmpty();
	InvalidatePath();
	if (IsNavigationReady()) { CalculateSharedPath(); }
	else if (NavigationRevision == 0 && !bRefreshPending && !bBuildingNavigation)
	{
		RequestNavigationRefresh(OutFailureReason);
	}
	return true;
}

bool UCastleNavigationSubsystem::IsNavigationReady() const
{
	UNavigationSystemV1* Navigation = GetNavigation();
	return NavigationRevision > 0 && ActiveChangeCount == 0 && !bRefreshPending && !bBuildingNavigation
		&& Navigation && !Navigation->HasDirtyAreasQueued() && !Navigation->IsNavigationDirty()
		&& !Navigation->IsNavigationBuildInProgress();
}

void UCastleNavigationSubsystem::InvalidatePath()
{
	const auto Current = AcquireSharedPath();
	if (Current && Current->bValid)
	{
		FCastleSharedNavigationPath Empty;
		Empty.NavigationRevision = NavigationRevision;
		PublishPath(MoveTemp(Empty));
	}
	bPathPending = bHasPathRequest;
}

void UCastleNavigationSubsystem::PublishPath(FCastleSharedNavigationPath&& Path)
{
	// 失效快照也具有独立版本，消费者不会因版本未变而继续沿用旧路线。
	Path.PathRevision = ++PathRevision;
	{
		FWriteScopeLock Lock(PathLock);
		PublishedPath = MakeShared<FCastleSharedNavigationPath, ESPMode::ThreadSafe>(MoveTemp(Path));
	}
	OnSharedPathChanged.Broadcast();
}

TSharedPtr<const FCastleSharedNavigationPath, ESPMode::ThreadSafe> UCastleNavigationSubsystem::AcquireSharedPath() const
{
	FReadScopeLock Lock(PathLock);
	return PublishedPath;
}

FCastleSharedNavigationPath UCastleNavigationSubsystem::GetSharedPath() const
{
	const auto Path = AcquireSharedPath();
	return Path ? *Path : FCastleSharedNavigationPath();
}

void UCastleNavigationSubsystem::CalculateSharedPath()
{
	UNavigationSystemV1* Navigation;
	ANavigationData* Data;
	FText Failure;
	bPathPending = false;
	FCastleSharedNavigationPath Path;
	Path.NavigationRevision = NavigationRevision;
	if (!ValidateNavigation(Navigation, Data, Failure)) { FailNavigation(Failure); return; }
	FNavLocation Start;
	FNavLocation Goal;
	if (!Navigation->ProjectPointToNavigation(RequestedStart, Start, ProjectionExtent, Data)
		|| !Navigation->ProjectPointToNavigation(RequestedGoal, Goal, ProjectionExtent, Data))
	{
		LastFailure = FText::FromString(TEXT("共享路线起点或终点附近没有可行走 NavMesh，请检查楼板、楼梯碰撞及 Agent 尺寸。"));
	}
	else
	{
		FPathFindingQuery Query(this, *Data, Start.Location, Goal.Location);
		Query.SetAllowPartialPaths(false);
		++PathQueryCount;
		const FPathFindingResult Result = Navigation->FindPathSync(Query);
		if (Result.IsSuccessful() && Result.Path.IsValid() && Result.Path->IsValid() && !Result.Path->IsPartial())
		{
			// 重算由本子系统统一触发，不让引擎给每个路线消费者另建自动重算任务。
			Result.Path->EnableRecalculationOnInvalidation(false);
			for (const FNavPathPoint& Point : Result.Path->GetPathPoints())
			{
				Path.Points.Add(Point.Location);
				Path.PointFlags.Add(static_cast<int32>(Point.Flags));
			}
			Path.Length = Result.Path->GetLength();
			Path.bValid = true;
			LastFailure = FText::GetEmpty();
		}
		else
		{
			LastFailure = FText::FromString(TEXT("共享路线不可达：起点与终点之间没有完整导航路径，不发布部分路线。"));
		}
	}
	if (!Path.bValid) { UE_LOG(LogMoving_Castle, Warning, TEXT("NPC 导航：%s"), *LastFailure.ToString()); }
	else { UE_LOG(LogMoving_Castle, Display, TEXT("NPC 共享路线发布：导航版本 %d，查询累计 %d 次，%d 个转折点，长度 %.1f cm。"), NavigationRevision, PathQueryCount, Path.Points.Num(), Path.Length); }
	PublishPath(MoveTemp(Path));
}

void UCastleNavigationSubsystem::FailNavigation(const FText& Reason)
{
	LastFailure = Reason;
	bRefreshPending = bBuildingNavigation = bPathPending = false;
	if (UNavigationSystemV1* Navigation = LockedNavigation.Get())
	{
		Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::Custom, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	}
	LockedNavigation.Reset();
	UE_LOG(LogMoving_Castle, Error, TEXT("NPC 导航更新失败：%s"), *Reason.ToString());
}

bool UCastleNavigationSubsystem::IsTickable() const
{
	return !IsTemplate() && !bShuttingDown && ActiveChangeCount == 0 && (bRefreshPending || bBuildingNavigation || bPathPending);
}

void UCastleNavigationSubsystem::Tick(float DeltaTime)
{
	UNavigationSystemV1* Navigation = GetNavigation();
	if (!Navigation || ActiveChangeCount != 0) { return; }
	if (bRefreshPending && GFrameCounter > LastChangeFrame)
	{
		ANavigationData* Data = Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		if (!Data)
		{
			// 没有 Bounds 的关卡可以只使用建筑逻辑；不为它创建一份空导航或持续轮询。
			if (bHasPathRequest) { FailNavigation(FText::FromString(TEXT("导航数据在构建前被移除。"))); }
			else
			{
				Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::Custom, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
				LockedNavigation.Reset();
				bRefreshPending = false;
			}
			return;
		}
		FText Failure;
		if (!ValidateNavigation(Navigation, Data, Failure)) { FailNavigation(Failure); return; }
		// 先将最终碰撞同步到导航八叉树。整轮只重建一次全局网格，避免依赖动画期间
		// 中间几何的脏区是否仍被保留，也不会按零件归位次数重复触发生成。
		Navigation->FlushPendingOperations();
		Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::Custom, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
		LockedNavigation.Reset();
		Data->RebuildAll();
		bRefreshPending = false;
		bBuildingNavigation = true;
		CommitFrame = GFrameCounter;
		BuildStartTime = FPlatformTime::Seconds();
		++RefreshCount;
		UE_LOG(LogMoving_Castle, Display, TEXT("NPC 导航开始第 %d 轮更新：所有建造/合并变化已结束。"), RefreshCount);
		return;
	}
	if (bBuildingNavigation)
	{
		// 至少跨过一次导航 Tick，让异步 Tile 生成器有机会接收本轮请求。
		if (GFrameCounter <= CommitFrame + 1 || Navigation->HasDirtyAreasQueued()
			|| Navigation->IsNavigationDirty() || Navigation->IsNavigationBuildInProgress())
		{
			if (FPlatformTime::Seconds() - BuildStartTime > 60.0)
			{
				FailNavigation(FText::FromString(TEXT("导航构建超过 60 秒仍未完成，请检查导航锁与 Recast 构建日志。")));
			}
			return;
		}
		bBuildingNavigation = false;
		++NavigationRevision;
		UE_LOG(LogMoving_Castle, Display, TEXT("NPC 导航更新完成：版本 %d，累计刷新 %d 次。"), NavigationRevision, RefreshCount);
		bPathPending = bHasPathRequest;
	}
	if (bPathPending && IsNavigationReady()) { CalculateSharedPath(); }
}

TStatId UCastleNavigationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCastleNavigationSubsystem, STATGROUP_Tickables);
}

void UCastleNavigationSubsystem::Deinitialize()
{
	bShuttingDown = true;
	for (const auto& Pair : Changes)
	{
		if (auto* Flight = Cast<UAssemblyFlightComponent>(Pair.Key.Get())) { Flight->OnAssemblyFinishedNative.RemoveAll(this); }
		if (auto* Merge = Cast<UBuildingMergeComponent>(Pair.Key.Get())) { Merge->OnMergeFinishedNative.RemoveAll(this); }
	}
	Changes.Reset();
	ActiveChangeCount = 0;
	if (UNavigationSystemV1* Navigation = LockedNavigation.Get())
	{
		Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::Custom, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	}
	LockedNavigation.Reset();
	Super::Deinitialize();
}

FScopedCastleNavigationChange::FScopedCastleNavigationChange(UWorld* World, UObject* InSource) : Source(InSource)
{
	if (World)
	{
		UCastleNavigationSubsystem* Subsystem = World->GetSubsystem<UCastleNavigationSubsystem>();
		if (Subsystem && Subsystem->BeginGeometryChange(Source)) { Navigation = Subsystem; }
	}
}

FScopedCastleNavigationChange::~FScopedCastleNavigationChange()
{
	if (Navigation) { Navigation->EndGeometryChange(Source); }
}
