#include "CastleNavigationSubsystem.h"

#include "Algo/BinarySearch.h"

#include "AssemblyFlightComponent.h"
#include "BuildingMergeComponent.h"
#include "CastleLadderLinkComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeRWLock.h"
#include "Moving_Castle.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "Surface/CastleGridSurfacePath.h"

void FCastleSharedNavigationPath::SamplePatrol(double Distance, FVector& OutLocation, FVector& OutDirection) const
{
	check(!PatrolPoints.IsEmpty() && PatrolPoints.Num() == PatrolDistances.Num());
	OutLocation = PatrolPoints[0];
	OutDirection = FVector::ZeroVector;
	if (PatrolPoints.Num() == 1) { return; }
	// 二分找距离右侧的采样点；只沿发布时已验证的折线插值，不在运行时再次平滑。
	int32 Low = 1;
	int32 High = PatrolDistances.Num() - 1;
	while (Low < High)
	{
		const int32 Middle = (Low + High) / 2;
		if (PatrolDistances[Middle] <= Distance) { Low = Middle + 1; }
		else { High = Middle; }
	}
	const double Span = PatrolDistances[Low] - PatrolDistances[Low - 1];
	const double Alpha = Span > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp((Distance - PatrolDistances[Low - 1]) / Span, 0.0, 1.0) : 0.0;
	OutLocation = FMath::Lerp(PatrolPoints[Low - 1], PatrolPoints[Low], Alpha);
	OutDirection = (PatrolPoints[Low] - PatrolPoints[Low - 1]).GetSafeNormal();
}

int32 FCastleSharedNavigationPath::FindPatrolLeg(double Distance) const
{
	int32 Leg = 0;
	while (Leg + 2 < WaypointDistances.Num() && Distance >= WaypointDistances[Leg + 1]) { ++Leg; }
	return Leg;
}

bool FCastleSharedNavigationPath::FindLadderRange(const double Distance, double& OutEndDist, FVector& OutFacingDirection) const
{
	// 区间按集中采样顺序递增，单位只二分共享数值，不随梯子数量线性扫描整个闭环。
	const int32 Next = Algo::UpperBoundBy(LadderRanges, Distance, &FCastleLadderPathRange::StartDist);
	if (Next > 0)
	{
		const FCastleLadderPathRange& Range = LadderRanges[Next - 1];
		if (Distance >= Range.StartDist && Distance < Range.EndDist)
		{
			OutEndDist = Range.EndDist;
			OutFacingDirection = Range.FacingDirection;
			return true;
		}
	}
	OutEndDist = LadderRanges.IsValidIndex(Next) ? LadderRanges[Next].StartDist : Length;
	OutFacingDirection = FVector::ZeroVector;
	return false;
}

bool FCastleSharedNavigationPath::FindClosestPatrolDistance(FVector Location, int32 Leg, double& OutDistance, double& OutSeparation) const
{
	if (PatrolPoints.IsEmpty()) { return false; }
	const double Begin = Leg >= 0 && WaypointDistances.IsValidIndex(Leg) ? WaypointDistances[Leg] : 0.0;
	const double End = Leg >= 0 && WaypointDistances.IsValidIndex(Leg + 1) ? WaypointDistances[Leg + 1] : Length;
	OutDistance = 0.0;
	double BestSquared = TNumericLimits<double>::Max();
	if (PatrolPoints.Num() == 1)
	{
		OutSeparation = FVector::Distance(Location, PatrolPoints[0]);
		return true;
	}
	for (int32 Index = 1; Index < PatrolPoints.Num(); ++Index)
	{
		if (PatrolDistances[Index] < Begin || PatrolDistances[Index - 1] > End) { continue; }
		const FVector Candidate = FMath::ClosestPointOnSegment(Location, PatrolPoints[Index - 1], PatrolPoints[Index]);
		const double Squared = FVector::DistSquared(Location, Candidate);
		if (Squared < BestSquared)
		{
			BestSquared = Squared;
			OutDistance = PatrolDistances[Index - 1] + FVector::Distance(PatrolPoints[Index - 1], Candidate);
		}
	}
	OutSeparation = FMath::Sqrt(BestSquared);
	return BestSquared < TNumericLimits<double>::Max();
}

namespace CastlePatrolNavigation
{
	/** 追加一个实际要走的点；零长度线段不产生重复累计距离，供巡逻二分采样。 */
	void AppendPoint(const FVector& Point, FCastleSharedNavigationPath& OutPath)
	{
		const double Span = FVector::Distance(OutPath.PatrolPoints.Last(), Point);
		if (Span > UE_DOUBLE_SMALL_NUMBER)
		{
			OutPath.PatrolPoints.Add(Point);
			OutPath.PatrolDistances.Add(OutPath.PatrolDistances.Last() + Span);
		}
	}

	/** 梯子折线按真实三维距离采样，并只将梯底至梯顶的部分记录为攀爬区间。 */
	bool AppendLadderPolyline(const TArray<FVector>& Points, const FVector& FacingDirection, const double Spacing,
		FCastleSharedNavigationPath& OutPath, FText& OutFailure)
	{
		if (Points.Num() != 4)
		{
			OutFailure = FText::FromString(TEXT("合法梯子的三维通行折线必须包含楼板入口、梯底、梯顶和楼板出口四点。"));
			return false;
		}
		const FVector HorizontalFacing = FacingDirection.GetSafeNormal2D();
		if (HorizontalFacing.IsNearlyZero())
		{
			OutFailure = FText::FromString(TEXT("梯子组件必须具有可用的水平朝向，才能让单位直立面向梯面。"));
			return false;
		}
		for (int32 Segment = 1; Segment < Points.Num(); ++Segment)
		{
			const FVector& Start = Points[Segment - 1];
			const FVector& End = Points[Segment];
			const double SegmentLength = FVector::Distance(Start, End);
			if (!FMath::IsFinite(SegmentLength) || SegmentLength <= UE_DOUBLE_SMALL_NUMBER)
			{
				OutFailure = FText::FromString(TEXT("梯子通行折线存在零长度或非法三维线段。"));
				return false;
			}
			const double RangeStart = OutPath.PatrolDistances.Last();
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(SegmentLength / Spacing));
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				const FVector Sample = FMath::Lerp(Start, End, static_cast<double>(Step) / Steps);
				AppendPoint(Sample, OutPath);
			}
			if (Segment == 2)
			{
				FCastleLadderPathRange& Range = OutPath.LadderRanges.AddDefaulted_GetRef();
				Range.StartDist = RangeStart;
				Range.EndDist = OutPath.PatrolDistances.Last();
				Range.FacingDirection = HorizontalFacing;
			}
		}
		return true;
	}

	bool AppendSurfaceSamples(const ARecastNavMesh& Mesh, UNavigationSystemV1& Navigation, const UObject& Owner,
		const TOptional<CastleGridSurfacePath::FGrid>& Grid, const FNavMeshPath& Leg, double Spacing,
		FCastleSharedNavigationPath& OutPath, FText& OutFailure, bool& bOutGridFailure)
	{
		TArray<FVector> Surface{OutPath.PatrolPoints.Last()};
		const auto FlushSurface = [&]()
		{
			// 一次处理两个硬锚点之间的整段步行，确保同一格的进出和转角不被原生拐点拆散。
			TArray<FVector> Shaped;
			if (Grid.IsSet() && !CastleGridSurfacePath::Build(Grid.GetValue(), Mesh, Owner, Surface, Spacing, Shaped, OutFailure))
			{
				bOutGridFailure = true;
				return false;
			}
			const TArray<FVector>& Points = Grid.IsSet() ? Shaped : Surface;
			for (int32 I = 1; I < Points.Num(); ++I) { AppendPoint(Points[I], OutPath); }
			return true;
		};

		const TArray<FNavPathPoint>& Corners = Leg.GetPathPoints();
		int32 CorridorCursor = 0;
		for (int32 Corner = 1; Corner < Corners.Num(); ++Corner)
		{
			const FVector Start = Corners[Corner - 1].Location;
			const FVector End = Corners[Corner].Location;
			const FNavPathPoint& SegmentStart = Corners[Corner - 1];
			FNavLinkId LinkId = SegmentStart.CustomNavLinkId;
			// 当查询点正落在接驳锚点时，Recast 简化后的首/末角点可能不带 CustomNavLinkId。
			// 连接身份仍存在于本次求解的走廊多边形中；只承认走廊证据及精确两端匹配，
			// 不从世界里找附近梯子，也不能把普通跨层直线误当成有效连接。
			for (const NavNodeRef Poly : Leg.PathCorridor)
			{
				const FNavLinkId CorridorLinkId = Mesh.GetNavLinkUserId(Poly);
				if (!CorridorLinkId.IsValid()) { continue; }
				FVector A, B;
				if (!Mesh.GetLinkEndPoints(Poly, A, B)) { continue; }
				auto MatchesAnchor = [](const FVector& Point, const FVector& Anchor)
				{
					return FVector::DistSquared2D(Point, Anchor) < 1.0 && FMath::Abs(Point.Z - Anchor.Z) <= 20.0;
				};
				if ((MatchesAnchor(Start, A) && MatchesAnchor(End, B)) || (MatchesAnchor(Start, B) && MatchesAnchor(End, A)))
				{
					LinkId = CorridorLinkId;
					break;
				}
			}
			const bool bIsNavLink = Leg.IsPathSegmentANavLink(Corner - 1) || LinkId.IsValid();
			if (bIsNavLink)
			{
				INavLinkCustomInterface* RegisteredLink = LinkId.IsValid() ? Navigation.GetCustomLink(LinkId) : nullptr;
				UCastleLadderLinkComponent* Ladder = Cast<UCastleLadderLinkComponent>(RegisteredLink);
				if (!Ladder || Ladder->GetWorld() != Navigation.GetWorld() || !Ladder->IsRegistered()
					|| !Ladder->bLadderLinkEnabled || Ladder->GetId() != LinkId)
				{
					OutFailure = FText::FromString(TEXT("共享巡逻遇到未登记、已禁用或非梯子类型的 NavLink，拒绝将其当作普通直线路段。"));
					return false;
				}
				TArray<FVector> Traversal;
				if (!Ladder->GetTraversalPolyline(End.Z > Start.Z, Traversal))
				{
					OutFailure = FText::FromString(FString::Printf(TEXT("梯子导航组件 %s 无法提供有效三维通行折线。"), *Ladder->GetPathName()));
					return false;
				}
				// 上下行都面向同一梯面：下行折线已逆序，仍取原下层入口指向梯底的水平向量。
				const FVector FacingDirection = End.Z > Start.Z ? Traversal[1] - Traversal[0] : Traversal[2] - Traversal[3];
				if (!FlushSurface() || !AppendLadderPolyline(Traversal, FacingDirection, Spacing, OutPath, OutFailure)) { return false; }
				Surface.Reset();
				Surface.Add(OutPath.PatrolPoints.Last());
			}
			else
			{
				const int32 EndPoly = Leg.GetNodeRefIndex(Corners[Corner].NodeRef);
				if (EndPoly < CorridorCursor) { OutFailure = FText::FromString(TEXT("巡逻路径走廊顺序无效，无法生成共享贴地采样。")); return false; }
				const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(Start, End) / Spacing));
				for (int32 Step = 1; Step <= Steps; ++Step)
				{
					const FVector Sample = FMath::Lerp(Start, End, static_cast<double>(Step) / Steps);
					double BestScore = TNumericLimits<double>::Max();
					FVector BestPoint = FVector::ZeroVector;
					int32 BestPoly = INDEX_NONE;
					for (int32 Poly = CorridorCursor; Poly <= EndPoly; ++Poly)
					{
						FVector OnSurface;
						if (!Mesh.GetClosestPointOnPoly(Leg.PathCorridor[Poly], Sample, OnSurface)) { continue; }
						const double HorizontalSquared = FVector::DistSquared2D(Sample, OnSurface);
						// 限制在求解器选中的折线上；高度以最近已走表面连续选择，防止吸到重叠楼层。
						if (HorizontalSquared > 1.0) { continue; }
						const double Score = HorizontalSquared * 1000000.0 + FMath::Square(OnSurface.Z - Surface.Last().Z);
						if (Score < BestScore) { BestScore = Score; BestPoint = OnSurface; BestPoly = Poly; }
					}
					if (BestPoly == INDEX_NONE)
					{
						OutFailure = FText::FromString(TEXT("无法沿最短路走廊生成共享表面采样；未发布悬空或跨楼层直线路线。"));
						return false;
					}
					CorridorCursor = BestPoly;
					if (FVector::DistSquared(Surface.Last(), BestPoint) > FMath::Square(UE_DOUBLE_SMALL_NUMBER)) { Surface.Add(BestPoint); }
				}
			}
		}
		return FlushSurface();
	}
}

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
	return RequestRoute({Start, Goal}, false, OutFailureReason);
}

bool UCastleNavigationSubsystem::RequestSharedPatrolRoute(const TArray<FVector>& Waypoints, FText& OutFailureReason)
{
	return RequestRoute(Waypoints, true, OutFailureReason);
}

void UCastleNavigationSubsystem::ClearSharedRoute()
{
	check(IsInGameThread());
	RequestedWaypoints.Reset();
	bHasPathRequest = bPathPending = false;
	++RouteRevision;
	LastFailure = FText::GetEmpty();
	FCastleSharedNavigationPath Empty;
	Empty.NavigationRevision = NavigationRevision;
	Empty.RouteRevision = RouteRevision;
	PublishPath(MoveTemp(Empty));
}

bool UCastleNavigationSubsystem::RequestRoute(const TArray<FVector>& Waypoints, bool bClosedLoop, FText& OutFailureReason)
{
	check(IsInGameThread());
	OutFailureReason = FText::GetEmpty();
	UNavigationSystemV1* Navigation;
	ANavigationData* Data;
	if (Waypoints.IsEmpty() || ProjectionExtent.ContainsNaN() || ProjectionExtent.GetMin() <= 0.0
		|| !FMath::IsFinite(PatrolSampleSpacing) || PatrolSampleSpacing < 1.0)
	{
		OutFailureReason = FText::FromString(TEXT("共享路线的起终点和导航投影范围必须有效。"));
		return false;
	}
	for (int32 Index = 0; Index < Waypoints.Num(); ++Index)
	{
		if (Waypoints[Index].ContainsNaN()
			|| (Waypoints.Num() > 1 && Waypoints[Index].Equals(Waypoints[(Index + 1) % Waypoints.Num()], 1.0)))
		{
			OutFailureReason = FText::FromString(TEXT("路径点必须是有限坐标，相邻点（含末点与首点）不能重合。"));
			return false;
		}
	}
	if (!ValidateNavigation(Navigation, Data, OutFailureReason)) { return false; }
	if (bHasPathRequest && bRequestedClosedLoop == bClosedLoop && RequestedWaypoints == Waypoints) { return true; }
	RequestedWaypoints = Waypoints;
	bRequestedClosedLoop = bClosedLoop;
	++RouteRevision;
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

bool UCastleNavigationSubsystem::BuildTemporaryPath(TConstArrayView<FVector> Waypoints,
	FCastleSharedNavigationPath& OutPath, FText& OutReason)
{
	OutPath = FCastleSharedNavigationPath();
	OutReason = FText::GetEmpty();
	UNavigationSystemV1* Navigation = nullptr;
	ANavigationData* Data = nullptr;
	if (!IsInGameThread() || Waypoints.Num() < 2 || !IsNavigationReady())
	{
		OutReason = FText::FromString(TEXT("临时调动需要游戏线程、至少两个路径点及稳定的 NavMesh。"));
		return false;
	}
	if (!ValidateNavigation(Navigation, Data, OutReason)) { return false; }
	const ARecastNavMesh* Mesh = Cast<ARecastNavMesh>(Data);
	if (!Mesh)
	{
		OutReason = FText::FromString(TEXT("临时调动缺少 Recast 走廊，不能使用直线。"));
		return false;
	}
	TOptional<CastleGridSurfacePath::FGrid> Grid;
	if (!CastleGridSurfacePath::ReadGrid(*GetWorld(), Grid, OutReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("NPC 格心导航：%s"), *OutReason.ToString());
		return false;
	}
	FCastleSharedNavigationPath Candidate;
	Candidate.NavigationRevision = NavigationRevision;
	for (const FVector& Waypoint : Waypoints)
	{
		FNavLocation Projected;
		// 棋盘落点和接驳点只贴高度；无棋盘关卡保留临时路线原有的 5 cm 横向投影校验。
		if (Waypoint.ContainsNaN() || !Navigation->ProjectPointToNavigation(Waypoint, Projected, ProjectionExtent, Data)
			|| (Grid.IsSet() ? !CastleGridSurfacePath::IsExactAnchor(*Grid, Waypoint, Projected.Location, ProjectionExtent.Z)
				: FVector::DistSquared2D(Waypoint, Projected.Location) > FMath::Square(5.0)))
		{
			OutReason = FText::FromString(FString::Printf(TEXT("临时调动点 %s 不在可行走表面。"), *Waypoint.ToCompactString()));
			return false;
		}
		Candidate.Waypoints.Add(Projected.Location);
	}
	Candidate.PatrolPoints.Add(Candidate.Waypoints[0]);
	Candidate.PatrolDistances.Add(0.0);
	Candidate.WaypointDistances.Add(0.0);
	for (int32 Index = 1; Index < Candidate.Waypoints.Num(); ++Index)
	{
		const FVector Start = Candidate.Waypoints[Index - 1];
		const FVector End = Candidate.Waypoints[Index];
		// 已在落点时本段是一段零距离真实行程；没有理由要求 NPC 先离开落点再回来。
		if (Start.Equals(End, 0.01)) { Candidate.WaypointDistances.Add(Candidate.PatrolDistances.Last()); continue; }
		FPathFindingQuery Query(this, *Data, Start, End);
		Query.SetAllowPartialPaths(false);
		++PathQueryCount;
		const FPathFindingResult Result = Navigation->FindPathSync(Query);
		const FNavMeshPath* Corridor = Result.IsSuccessful() && Result.Path.IsValid() && !Result.Path->IsPartial()
			? Result.Path->CastPath<FNavMeshPath>() : nullptr;
		bool bGridFailure = false;
		if (!Corridor || !CastlePatrolNavigation::AppendSurfaceSamples(*Mesh, *Navigation, *this, Grid, *Corridor,
			PatrolSampleSpacing, Candidate, OutReason, bGridFailure))
		{
			if (OutReason.IsEmpty()) { OutReason = FText::FromString(TEXT("没有完整可走的集结或接回路线。")); }
			if (bGridFailure) { UE_LOG(LogMoving_Castle, Error, TEXT("NPC 格心导航：%s"), *OutReason.ToString()); }
			return false;
		}
		Candidate.WaypointDistances.Add(Candidate.PatrolDistances.Last());
	}
	Candidate.Length = Candidate.PatrolDistances.Last();
	Candidate.bValid = true;
	OutPath = MoveTemp(Candidate);
	return true;
}

void UCastleNavigationSubsystem::CalculateSharedPath()
{
	UNavigationSystemV1* Navigation;
	ANavigationData* Data;
	FText Failure;
	bPathPending = false;
	FCastleSharedNavigationPath Path;
	Path.NavigationRevision = NavigationRevision;
	Path.RouteRevision = RouteRevision;
	Path.bClosedLoop = bRequestedClosedLoop;
	if (!ValidateNavigation(Navigation, Data, Failure)) { FailNavigation(Failure); return; }
	const ARecastNavMesh* Mesh = Cast<ARecastNavMesh>(Data);
	LastFailure = FText::GetEmpty();
	TOptional<CastleGridSurfacePath::FGrid> Grid;
	// 新增的格内失败以 Error 报告；保留已有原生不可达、NavLink 失败的日志边界。
	bool bGridFailure = bRequestedClosedLoop && !CastleGridSurfacePath::ReadGrid(*GetWorld(), Grid, LastFailure);
	// 所有路径点先统一投影；任一点无效时不开始各路段的 A*，也不发布上轮旧路线。
	for (int32 Index = 0; Index < RequestedWaypoints.Num() && LastFailure.IsEmpty(); ++Index)
	{
		FNavLocation Projected;
		if (!Navigation->ProjectPointToNavigation(RequestedWaypoints[Index], Projected, ProjectionExtent, Data))
		{
			LastFailure = FText::FromString(FString::Printf(TEXT("共享路线起点或终点附近没有可行走 NavMesh（路径点 %d），请检查楼板、楼梯碰撞及 Agent 尺寸。"), Index + 1));
			break;
		}
		// 格心硬点不能被移动到障碍旁；没有棋盘语义的路线保留原来的路径点投影契约。
		if (Grid.IsSet() && !CastleGridSurfacePath::IsExactAnchor(*Grid, RequestedWaypoints[Index], Projected.Location, ProjectionExtent.Z))
		{
			LastFailure = FText::FromString(FString::Printf(TEXT("%s 的显式巡逻路径点 %d（%s）没有原位可行走表面，不能移到附近位置或其他楼层。"),
				*Grid->BoardName, Index + 1, *RequestedWaypoints[Index].ToCompactString()));
			bGridFailure = true;
			break;
		}
		Path.Waypoints.Add(Projected.Location);
	}
	if (LastFailure.IsEmpty() && bRequestedClosedLoop && !Mesh)
	{
		LastFailure = FText::FromString(TEXT("Mass 共享巡逻需要 Recast NavMesh 提供表面走廊。"));
	}
	if (LastFailure.IsEmpty() && bRequestedClosedLoop)
	{
		Path.PatrolPoints.Add(Path.Waypoints[0]);
		Path.PatrolDistances.Add(0.0);
		Path.WaypointDistances.Add(0.0);
	}
	const int32 LegCount = bRequestedClosedLoop ? (RequestedWaypoints.Num() > 1 ? RequestedWaypoints.Num() : 0) : 1;
	// 一次调用完整求解 1→2、2→3、…、N→1。循环内部只有本地临时结果，绝不逐段发布。
	for (int32 LegIndex = 0; LegIndex < LegCount && LastFailure.IsEmpty(); ++LegIndex)
	{
		const int32 Next = (LegIndex + 1) % Path.Waypoints.Num();
		if (Path.Waypoints[LegIndex].Equals(Path.Waypoints[Next], 1.0))
		{
			LastFailure = FText::FromString(FString::Printf(TEXT("路径点 %d 与 %d 投影后重合，请增大间距。"), LegIndex + 1, Next + 1));
			break;
		}
		FPathFindingQuery Query(this, *Data, Path.Waypoints[LegIndex], Path.Waypoints[Next]);
		Query.SetAllowPartialPaths(false);
		++PathQueryCount;
		const FPathFindingResult Result = Navigation->FindPathSync(Query);
		if (Result.IsSuccessful() && Result.Path.IsValid() && Result.Path->IsValid() && !Result.Path->IsPartial())
		{
			// 原生路径对象只活在集中预处理中，关闭其自动重算，所有 NPC 只读最终数值快照。
			Result.Path->EnableRecalculationOnInvalidation(false);
			for (const FNavPathPoint& Point : Result.Path->GetPathPoints())
			{
				if (!Path.Points.IsEmpty() && Path.Points.Last().Equals(Point.Location)) { continue; }
				Path.Points.Add(Point.Location);
				Path.PointFlags.Add(static_cast<int32>(Point.Flags));
			}
			Path.Length += Result.Path->GetLength();
			if (bRequestedClosedLoop)
			{
				const FNavMeshPath* Corridor = Result.Path->CastPath<FNavMeshPath>();
				if (LastFailure.IsEmpty() && (!Corridor || !CastlePatrolNavigation::AppendSurfaceSamples(*Mesh, *Navigation, *this, Grid,
					*Corridor, PatrolSampleSpacing, Path, LastFailure, bGridFailure)))
				{
					if (LastFailure.IsEmpty()) { LastFailure = FText::FromString(TEXT("共享巡逻缺少 Recast 路径走廊。")); }
				}
				if (LastFailure.IsEmpty()) { Path.WaypointDistances.Add(Path.PatrolDistances.Last()); }
			}
		}
		else
		{
			LastFailure = FText::FromString(FString::Printf(TEXT("共享路线不可达：路径点 %d→%d 之间没有完整最短路，整条路线不发布。"), LegIndex + 1, Next + 1));
		}
	}
	Path.bValid = LastFailure.IsEmpty();
	if (Path.bValid && bRequestedClosedLoop)
	{
		Path.Length = Path.PatrolDistances.Last();
		if (LegCount == 0) { Path.Points = Path.Waypoints; Path.PointFlags.Add(0); Path.WaypointDistances.Add(0.0); }
	}
	// 失败快照必须为空，界面和消费者都不能将临时已完成的前几段误当成可用路线。
	if (!Path.bValid)
	{
		Path.Points.Reset(); Path.PointFlags.Reset(); Path.PatrolPoints.Reset();
		Path.PatrolDistances.Reset(); Path.WaypointDistances.Reset(); Path.LadderRanges.Reset(); Path.Length = 0.0;
	}
	if (bGridFailure) { UE_LOG(LogMoving_Castle, Error, TEXT("NPC 格心导航：%s"), *LastFailure.ToString()); }
	else if (!Path.bValid) { UE_LOG(LogMoving_Castle, Warning, TEXT("NPC 导航：%s"), *LastFailure.ToString()); }
	else { UE_LOG(LogMoving_Castle, Display, TEXT("NPC 共享路线发布：导航版本 %d，查询累计 %d 次，完整 %d 段，%d 个转折点，%d 个共享采样，长度 %.1f cm。"), NavigationRevision, PathQueryCount, LegCount, Path.Points.Num(), Path.PatrolPoints.Num(), Path.Length); }
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
