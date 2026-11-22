#include "Surface/CastleGridSurfacePath.h"

#include "Algo/Reverse.h"
#include "Castle/Actors/CastleBoard.h"
#include "EngineUtils.h"
#include "NavMesh/RecastNavMesh.h"

namespace CastleGridSurfacePath
{
	// 只吸附高度。横向容差只处理边界浮点误差，不能把被家具挡住的候选挪到空洞边缘。
	constexpr double XYTolerance = 0.05;
	constexpr double FlatTolerance = 2.0;
	constexpr double PreferredTrim = 35.0;
	constexpr double Infinity = TNumericLimits<double>::Max();

	struct FVisit
	{
		TOptional<FIntVector> Cell;
		TArray<FVector> Native;
	};

	TOptional<FIntVector> Classify(const FGrid& Grid, const FVector& Position)
	{
		const FVector Local = Grid.Transform.InverseTransformPosition(Position);
		const double X = FMath::FloorToDouble(Local.X / Grid.CellSize + 0.5) + Grid.Origin.X;
		const double Y = FMath::FloorToDouble(Local.Y / Grid.CellSize + 0.5) + Grid.Origin.Y;
		const double Z = (Local.Z - Grid.FirstFloor) / Grid.LayerHeight;
		const double Layer = FMath::RoundToDouble(Z);
		if (X < Grid.Min.X || X > Grid.Max.X || Y < Grid.Min.Y || Y > Grid.Max.Y || Layer < 0 || Layer >= Grid.Layers
			|| FMath::Abs(Z - Layer) > 0.25)
		{
			return {};
		}
		return FIntVector(static_cast<int32>(X), static_cast<int32>(Y), Grid.Origin.Z + static_cast<int32>(Layer));
	}

	FVector AtCellCenter(const FGrid& Grid, FIntVector Cell, double WorldHeight)
	{
		const FIntVector Offset = Cell - Grid.Origin;
		FVector Center = Grid.Transform.TransformPosition(FVector(Offset.X * Grid.CellSize, Offset.Y * Grid.CellSize, Grid.FirstFloor));
		Center.Z = WorldHeight;
		return Center;
	}

	/** 沿原生折线精确切格，不用采样点计数判断是否进入；只接触角点不会制造一格访问。 */
	TArray<FVisit> SplitVisits(const FGrid& Grid, TConstArrayView<FVector> Surface)
	{
		TArray<FVisit> Visits;
		for (int32 I = 1; I < Surface.Num(); ++I)
		{
			const FVector A = Surface[I - 1];
			const FVector B = Surface[I];
			const FVector LocalA = Grid.Transform.InverseTransformPosition(A);
			const FVector LocalB = Grid.Transform.InverseTransformPosition(B);
			TArray<double, TInlineAllocator<8>> Cuts{0.0, 1.0};
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				const double Delta = LocalB[Axis] - LocalA[Axis];
				if (FMath::Abs(Delta) <= UE_DOUBLE_SMALL_NUMBER) { continue; }
				const int32 Low = (Axis == 0 ? Grid.Min.X : Grid.Min.Y) - Grid.Origin[Axis];
				const int32 High = (Axis == 0 ? Grid.Max.X : Grid.Max.Y) - Grid.Origin[Axis];
				for (int32 Face = Low; Face <= High + 1; ++Face)
				{
					const double T = ((Face - 0.5) * Grid.CellSize - LocalA[Axis]) / Delta;
					if (T > 0.0 && T < 1.0) { Cuts.Add(T); }
				}
			}
			Cuts.Sort();
			for (int32 J = 1; J < Cuts.Num(); ++J)
			{
				if (Cuts[J] - Cuts[J - 1] <= UE_DOUBLE_SMALL_NUMBER) { continue; }
				const FVector Begin = FMath::Lerp(A, B, Cuts[J - 1]);
				const FVector End = FMath::Lerp(A, B, Cuts[J]);
				const auto Cell = FMath::Abs(B.Z - A.Z) <= FlatTolerance ? Classify(Grid, (Begin + End) * 0.5) : TOptional<FIntVector>{};
				// 格内只重构平段。台阶的表面采样和跨层有高度变化的部分仍由原生产生者拥有。
				if (Visits.IsEmpty() || Visits.Last().Cell != Cell
					|| (Cell.IsSet() && FMath::Abs(End.Z - Visits.Last().Native[0].Z) > FlatTolerance))
				{
					FVisit& Visit = Visits.AddDefaulted_GetRef();
					Visit.Cell = Cell;
					Visit.Native.Add(Begin);
				}
				Visits.Last().Native.Add(End);
			}
		}
		return Visits;
	}

	/** 所有候选段均用同一 Recast 数据验通。NavMesh 已经体现当前 Agent 净空。 */
	struct FSurfaceQuery
	{
		const ARecastNavMesh& Mesh;
		const UObject& Owner;
		double Height;

		bool Project(const FVector& Position, FNavLocation& Out) const
		{
			return Mesh.ProjectPoint(Position, Out, FVector(1, 1, FlatTolerance), nullptr, &Owner)
				&& FVector::DistSquared2D(Position, Out.Location) <= FMath::Square(XYTolerance)
				&& FMath::Abs(Out.Location.Z - Height) <= FlatTolerance;
		}

		bool Chord(const FNavLocation& A, const FNavLocation& B) const
		{
			if (A.NodeRef == B.NodeRef && A.Location.Equals(B.Location, UE_DOUBLE_SMALL_NUMBER)) { return true; }
			const auto IsClear = [this](const FNavLocation& From, const FNavLocation& To)
			{
				FVector Hit;
				FNavigationRaycastAdditionalResults Result;
				return !ARecastNavMesh::NavMeshRaycast(&Mesh, From.NodeRef, From.Location, To.Location, Hit, &Result, nullptr, &Owner)
					&& Result.bIsRayEndInCorridor;
			};
			// 共边端点可能属于任一相邻多边形；验证任一方向的连续地面连接，不用最近点连两段。
			return IsClear(A, B) || IsClear(B, A);
		}

		bool SampleChord(const FNavLocation& A, const FNavLocation& B, double Spacing, TArray<FVector>* Out = nullptr) const
		{
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(A.Location, B.Location) / Spacing));
			FNavLocation Prev = A;
			for (int32 I = 1; I <= Steps; ++I)
			{
				FNavLocation Next = B;
				if ((I < Steps && !Project(FMath::Lerp(A.Location, B.Location, static_cast<double>(I) / Steps), Next))
					|| !Chord(Prev, Next)) { return false; }
				if (Out) { Out->Add(Next.Location); }
				Prev = Next;
			}
			return true;
		}
	};

	/** 圆角以相切的入、出线段构造；短弦同样验通，不能靠样点投影掩盖切入障碍。 */
	bool Fillet(const FSurfaceQuery& Query, const FNavLocation& A, const FNavLocation& B, const FNavLocation& C,
		TArray<FNavLocation, TInlineAllocator<16>>& Out)
	{
		const FVector Incoming = (B.Location - A.Location).GetSafeNormal2D();
		const FVector Outgoing = (C.Location - B.Location).GetSafeNormal2D();
		const double Dot = FVector::DotProduct(Incoming, Outgoing);
		if (Dot > 0.999999) { Out = {B}; return true; }
		if (Dot < -0.999999 || Incoming.IsNearlyZero() || Outgoing.IsNearlyZero()) { return false; }
		const double SignedAngle = FMath::Atan2(FVector::CrossProduct(Incoming, Outgoing).Z, Dot);
		const double HalfAngleTangent = FMath::Tan(FMath::Abs(SignedAngle) * 0.5);
		// 每角最多各用邻边三分之一；同一线段两侧的圆角不会相交。小半径也是求解域，不发布尖角替代物。
		const double MaxTrim = FMath::Min(PreferredTrim, FMath::Min(FVector::Dist2D(A.Location, B.Location), FVector::Dist2D(B.Location, C.Location)) / 3.0);
		for (const double Fraction : {1.0, 0.5, 0.25})
		{
			const double Trim = MaxTrim * Fraction;
			const FVector Entry = B.Location - Incoming * Trim;
			const FVector Center = Entry + FVector(-Incoming.Y, Incoming.X, 0) * (Trim / HalfAngleTangent * FMath::Sign(SignedAngle));
			const FVector Radial = Entry - Center;
			const int32 Steps = FMath::CeilToInt(FMath::Abs(SignedAngle) / FMath::DegreesToRadians(12.0));
			Out.Reset();
			for (int32 I = 0; I <= Steps; ++I)
			{
				const FVector Point = Center + Radial.RotateAngleAxis(FMath::RadiansToDegrees(SignedAngle * I / Steps), FVector::UpVector);
				FNavLocation OnSurface;
				if (!Query.Project(Point, OnSurface) || (!Out.IsEmpty() && !Query.Chord(Out.Last(), OnSurface))) { break; }
				Out.Add(OnSurface);
			}
			if (Out.Num() == Steps + 1) { return true; }
		}
		return false;
	}

	bool Bake(const FSurfaceQuery& Query, TConstArrayView<FNavLocation> Controls, double Spacing, TArray<FVector>& Out)
	{
		Out = {Controls[0].Location};
		FNavLocation Prev = Controls[0];
		for (int32 I = 1; I + 1 < Controls.Num(); ++I)
		{
			TArray<FNavLocation, TInlineAllocator<16>> Arc;
			if (!Fillet(Query, Controls[I - 1], Controls[I], Controls[I + 1], Arc)) { return false; }
			for (const FNavLocation& Knot : Arc)
			{
				if (!Query.SampleChord(Prev, Knot, Spacing, &Out)) { return false; }
				Prev = Knot;
			}
		}
		return Query.SampleChord(Prev, Controls.Last(), Spacing, &Out);
	}

	double EdgeCost(FVector A, FVector B, FVector Start, FVector Center, FVector End, double Scale)
	{
		// 用沿边的偏离量评分，而不是只惩罚顶点。否则长对角线可以从格角抄过而不付代价。
		A.Z = B.Z = Start.Z = Center.Z = End.Z = 0.0;
		double Deviation = 0.0;
		for (int32 I = 1; I <= 5; ++I)
		{
			const FVector P = FMath::Lerp(A, B, (I - 0.5) / 5.0);
			Deviation += FMath::Min(FVector::Distance(P, FMath::ClosestPointOnSegment(P, Start, Center)),
				FVector::Distance(P, FMath::ClosestPointOnSegment(P, Center, End)));
		}
		return FVector::Distance(A, B) * (1.0 + Deviation / Scale);
	}

	/** 只在一个格内搜索。状态包含来向，不能分别挑入边和出边后再指望尖角可以抹圆。 */
	bool SolveVisit(const FGrid& Grid, const FVisit& Visit, const FSurfaceQuery& Query, FVector Start, FVector End,
		double Spacing, TArray<FVector>& Out, double& OutCost)
	{
		const FVector CellCenter = AtCellCenter(Grid, Visit.Cell.GetValue(), Query.Height);
		// 两个硬锚点位于格心同侧时不强迫 NPC 掉头去打卡；短程接驳优先直接走。
		const bool bThroughCenter = FVector::DotProduct((CellCenter - Start).GetSafeNormal2D(), (End - CellCenter).GetSafeNormal2D()) >= -0.5;
		const FVector Reference = bThroughCenter ? CellCenter : (Start + End) * 0.5;
		const double Scale = FMath::Min(Grid.Transform.GetScale3D().X, Grid.Transform.GetScale3D().Y) * Grid.CellSize;
		FNavLocation From, To, Middle;
		if (!Query.Project(Start, From) || !Query.Project(End, To)) { return false; }
		// 固定原生锚点的坐标；投影只取得多边形身份，不能让相邻格各自移动公共边界。
		From.Location = Start;
		To.Location = End;
		const auto Finish = [&]()
		{
			OutCost = 0.0;
			for (int32 I = 1; I < Out.Num(); ++I) { OutCost += EdgeCost(Out[I - 1], Out[I], Start, Reference, End, Scale); }
			return true;
		};
		const FNavLocation Direct[] = {From, To};
		if ((!bThroughCenter || FVector::DistSquared2D(Reference, FMath::ClosestPointOnSegment(Reference, Start, End)) <= FMath::Square(XYTolerance))
			&& Bake(Query, Direct, Spacing, Out)) { return Finish(); }
		if (Query.Project(Reference, Middle) && !Middle.Location.Equals(Start, XYTolerance) && !Middle.Location.Equals(End, XYTolerance))
		{
			const FNavLocation Ideal[] = {From, Middle, To};
			if (Bake(Query, Ideal, Spacing, Out)) { return Finish(); }
		}

		TArray<FNavLocation> Nodes{From, To};
		const auto AddCandidate = [&](FVector Candidate)
		{
			FNavLocation Point;
			if (Query.Project(Candidate, Point) && !Nodes.ContainsByPredicate([&](const FNavLocation& Other)
				{ return Other.Location.Equals(Point.Location, XYTolerance); })) { Nodes.Add(Point); }
		};
		// 固定格内采样负责找到障碍两侧及外拐空间；原生折线的实质转折也参与竞争，而不直接发布。
		constexpr double Offsets[] = {-0.4, -0.25, -0.15, 0.0, 0.15, 0.25, 0.4};
		for (const double X : Offsets)
		{
			for (const double Y : Offsets)
			{
				AddCandidate(CellCenter + Grid.Transform.TransformVector(FVector(X * Grid.CellSize, Y * Grid.CellSize, 0)));
			}
		}
		for (int32 I = 1; I + 1 < Visit.Native.Num(); ++I)
		{
			const FVector& P = Visit.Native[I];
			if (FVector::DotProduct((P - Visit.Native[I - 1]).GetSafeNormal2D(), (Visit.Native[I + 1] - P).GetSafeNormal2D()) < 0.999)
			{
				AddCandidate(P);
			}
		}

		const int32 Count = Nodes.Num();
		TArray<double> Edges;
		Edges.Init(Infinity, Count * Count);
		for (int32 A = 0; A < Count; ++A)
		{
			for (int32 B = A + 1; B < Count; ++B)
			{
				// 边先验整条射线，随后核对平面高度及细分。未验证的边从不进入搜索图。
				if (Query.Chord(Nodes[A], Nodes[B]) && Query.SampleChord(Nodes[A], Nodes[B], Spacing))
				{
					Edges[A * Count + B] = Edges[B * Count + A] = EdgeCost(Nodes[A].Location, Nodes[B].Location, Start, Reference, End, Scale);
				}
			}
		}
		struct FState { double Cost = Infinity; int32 Previous = INDEX_NONE; };
		struct FQueueItem { int32 Edge; double Estimate; };
		const auto Earlier = [](const FQueueItem& A, const FQueueItem& B)
		{
			return A.Estimate < B.Estimate || (A.Estimate == B.Estimate && A.Edge < B.Edge);
		};
		TArray<FState> States;
		States.SetNum(Count * Count);
		TArray<FQueueItem> Queue;
		const auto Enqueue = [&](int32 Edge, double Cost, int32 Previous)
		{
			if (Cost >= States[Edge].Cost) { return; }
			States[Edge] = {Cost, Previous};
			Queue.HeapPush(FQueueItem{Edge, Cost + FVector::Dist2D(Nodes[Edge % Count].Location, End)}, Earlier);
		};
		for (int32 N = 1; N < Count; ++N) { Enqueue(N, Edges[N], INDEX_NONE); }
		while (!Queue.IsEmpty())
		{
			FQueueItem Item;
			Queue.HeapPop(Item, Earlier, EAllowShrinking::No);
			const int32 A = Item.Edge / Count;
			const int32 B = Item.Edge % Count;
			const double Cost = States[Item.Edge].Cost;
			if (Item.Estimate > Cost + FVector::Dist2D(Nodes[B].Location, End)) { continue; }
			if (B == 1)
			{
				TArray<FNavLocation> Controls;
				for (int32 Edge = Item.Edge; Edge != INDEX_NONE; Edge = States[Edge].Previous) { Controls.Add(Nodes[Edge % Count]); }
				Controls.Add(From);
				Algo::Reverse(Controls);
				if (!Bake(Query, Controls, Spacing, Out)) { return false; }
				return Finish();
			}
			for (int32 C = 1; C < Count; ++C)
			{
				const int32 Edge = B * Count + C;
				if (C == A || Edges[Edge] == Infinity || Cost + Edges[Edge] >= States[Edge].Cost) { continue; }
				TArray<FNavLocation, TInlineAllocator<16>> Arc;
				if (Fillet(Query, Nodes[A], Nodes[B], Nodes[C], Arc)) { Enqueue(Edge, Cost + Edges[Edge], Item.Edge); }
			}
		}
		return false;
	}

	bool ReadGrid(UWorld& World, TOptional<FGrid>& OutGrid, FText& OutFailure)
	{
		OutGrid.Reset();
		for (TActorIterator<ACastleBoard> It(&World); It; ++It)
		{
			const ACastleBoard& Board = **It;
			const FTransform Transform = Board.GetActorTransform();
			if (OutGrid.IsSet() || !Transform.IsValid() || Transform.GetScale3D().GetMin() <= UE_DOUBLE_SMALL_NUMBER
				|| !FMath::IsFinite(Board.CellSize) || Board.CellSize <= 0.0 || !FMath::IsFinite(Board.LayerHeight) || Board.LayerHeight <= 0.0
				|| !FMath::IsFinite(Board.WorldBaseHeight) || Board.GridSize.GetMin() <= 0 || Board.MaxOverhangCells < 0)
			{
				OutFailure = FText::FromString(FString::Printf(TEXT("棋盘 %s 无法为共享路线提供唯一有效的格心坐标系。"), *Board.GetPathName()));
				return false;
			}
			FIntPoint Min, Max;
			Board.GetBoardExtent(Min, Max);
			const FIntPoint Overhang(Board.MaxOverhangCells, Board.MaxOverhangCells);
			OutGrid = FGrid{Board.GetPathName(), Transform, Board.GridOrigin, Min - Overhang, Max + Overhang,
				Board.GetUnlockedLayerCount(), Board.CellSize, Board.LayerHeight, Board.WorldBaseHeight - Board.LayerHeight * 0.5};
		}
		return true;
	}

	bool IsExactAnchor(const FGrid& Grid, FVector Requested, FVector Projected, double MaxHeightError)
	{
		if (FVector::DistSquared2D(Requested, Projected) > FMath::Square(XYTolerance)
			|| FMath::Abs(Requested.Z - Projected.Z) > MaxHeightError) { return false; }
		// 与层高比较使用棋盘空间，避免缩放后的上下层同时落在通常的世界高度投影窗里。
		const double LocalHeightDelta = FMath::Abs(Grid.Transform.InverseTransformPosition(Requested).Z
			- Grid.Transform.InverseTransformPosition(Projected).Z);
		return LocalHeightDelta < Grid.LayerHeight * 0.5;
	}

	bool Build(const FGrid& Grid, const ARecastNavMesh& Mesh, const UObject& Owner, TConstArrayView<FVector> Surface,
		double Spacing, TArray<FVector>& OutPoints, FText& OutFailure)
	{
		OutPoints.Reset();
		// 倾斜棋盘不具备本模块的水平圆弧平面；非平面行走仍消费原生表面采样。
		if (Surface.Num() < 2 || !Grid.Transform.GetUnitAxis(EAxis::Z).Equals(FVector::UpVector, 0.0001))
		{
			OutPoints.Append(Surface.GetData(), Surface.Num());
			return true;
		}
		const TArray<FVisit> Visits = SplitVisits(Grid, Surface);
		struct FGate { FVector Position; double Cost; };
		TArray<TArray<FGate, TInlineAllocator<2>>> Gates;
		Gates.SetNum(Visits.Num() + 1);
		Gates[0].Add({Surface[0], 0.0});
		Gates.Last().Add({Surface.Last(), 0.0});
		for (int32 I = 1; I < Visits.Num(); ++I)
		{
			const FVector Original = Visits[I].Native[0];
			const auto& Prev = Visits[I - 1].Cell;
			const auto& Next = Visits[I].Cell;
			// 公共格面只选一次，两侧路径共用。对角点接触、垂直过渡以及算路边界不横移。
			if (Prev.IsSet() && Next.IsSet() && Prev->Z == Next->Z
				&& FMath::Abs(Prev->X - Next->X) + FMath::Abs(Prev->Y - Next->Y) == 1)
			{
				const FVector Ideal = (AtCellCenter(Grid, *Prev, Original.Z) + AtCellCenter(Grid, *Next, Original.Z)) * 0.5;
				FNavLocation Centered;
				Gates[I].Add({Original, 3.0 * FVector::Dist2D(Ideal, Original)});
				if (!Original.Equals(Ideal, XYTolerance) && FSurfaceQuery{Mesh, Owner, Original.Z}.Project(Ideal, Centered))
				{
					Centered.Location.X = Ideal.X;
					Centered.Location.Y = Ideal.Y;
					Gates[I].Add({Centered.Location, 0.0});
				}
			}
			else { Gates[I].Add({Original, 0.0}); }
		}

		// 按格序做小型动态规划：一个格的局部最优出口若接不进下一格，不会破坏其他合法组合。
		struct FChoice { double Cost = Infinity; int32 Previous = INDEX_NONE; TArray<FVector> Points; };
		TArray<TArray<FChoice, TInlineAllocator<2>>> Choices;
		Choices.SetNum(Gates.Num());
		Choices[0].AddDefaulted_GetRef().Cost = 0.0;
		for (int32 I = 0; I < Visits.Num(); ++I)
		{
			const FVisit& Visit = Visits[I];
			Choices[I + 1].SetNum(Gates[I + 1].Num());
			bool bHasPassage = false;
			for (int32 A = 0; A < Gates[I].Num(); ++A)
			{
				if (Choices[I][A].Cost == Infinity) { continue; }
				for (int32 B = 0; B < Gates[I + 1].Num(); ++B)
				{
					TArray<FVector> Points;
					double LocalCost = 0.0;
					if (!Visit.Cell.IsSet()) { Points = Visit.Native; }
					else if (!SolveVisit(Grid, Visit, FSurfaceQuery{Mesh, Owner, Visit.Native[0].Z},
						Gates[I][A].Position, Gates[I + 1][B].Position, Spacing, Points, LocalCost)) { continue; }
					bHasPassage = true;
					const double Cost = Choices[I][A].Cost + Gates[I + 1][B].Cost + LocalCost;
					if (Cost < Choices[I + 1][B].Cost) { Choices[I + 1][B] = {Cost, A, MoveTemp(Points)}; }
				}
			}
			if (!bHasPassage)
			{
				OutFailure = FText::FromString(FString::Printf(TEXT("棋盘 %s 的格 %s 无法在同层验证入格至出格的轨迹（%s → %s）；未发布本次路线。"),
					*Grid.BoardName, *Visit.Cell.GetValue().ToString(), *Visit.Native[0].ToCompactString(), *Visit.Native.Last().ToCompactString()));
				return false;
			}
		}
		TArray<int32> Selected;
		Selected.SetNum(Visits.Num());
		int32 Gate = 0;
		for (int32 I = Visits.Num() - 1; I >= 0; --I)
		{
			Selected[I] = Gate;
			Gate = Choices[I + 1][Gate].Previous;
		}
		OutPoints.Add(Surface[0]);
		for (int32 I = 0; I < Visits.Num(); ++I)
		{
			const TArray<FVector>& Points = Choices[I + 1][Selected[I]].Points;
			OutPoints.Append(Points.GetData() + 1, Points.Num() - 1);
		}
		return true;
	}
}
