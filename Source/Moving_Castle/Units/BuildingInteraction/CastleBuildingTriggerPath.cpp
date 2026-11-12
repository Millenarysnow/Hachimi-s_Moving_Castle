#include "Units/BuildingInteraction/CastleBuildingTriggerPath.h"

#include "Algo/BinarySearch.h"
#include "CastleNavigationSubsystem.h"

namespace CastleBuildingTriggerPath
{
	/** 一个沿路线的连续停留区间；重叠或接触的区间必须合并成一次停留。 */
	struct FInterval
	{
		double Start;
		double End;
	};

	/**
	 * Slab 裁剪直接求线段与盒子的连续交集，不以采样间距决定触发精度。
	 * 平行轴按 [Min,Max) 判定，排除在相邻楼层公共上表面和外侧面上的整段误归属。
	 */
	bool ClipSegment(const FVector& Start, const FVector& End, const FBox& Box, double& OutStart, double& OutEnd)
	{
		OutStart = 0.0;
		OutEnd = 1.0;
		const FVector Delta = End - Start;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(Delta[Axis]) <= UE_DOUBLE_SMALL_NUMBER)
			{
				if (Start[Axis] < Box.Min[Axis] || Start[Axis] >= Box.Max[Axis]) { return false; }
				continue;
			}
			const double First = (Box.Min[Axis] - Start[Axis]) / Delta[Axis];
			const double Second = (Box.Max[Axis] - Start[Axis]) / Delta[Axis];
			OutStart = FMath::Max(OutStart, FMath::Min(First, Second));
			OutEnd = FMath::Min(OutEnd, FMath::Max(First, Second));
			if (OutEnd - OutStart <= UE_DOUBLE_SMALL_NUMBER) { return false; }
		}
		// 只有点接触的切线不形成停留区间，也就不产生进入。
		return OutEnd - OutStart > UE_DOUBLE_SMALL_NUMBER;
	}
}

bool FCastleBuildingTriggerPath::Compile(const FCastleSharedNavigationPath& Path, const uint64 InRegistryRevision,
	const TConstArrayView<FCastleBuildingTriggerRegion> Regions, FCastleBuildingTriggerPath& OutPath, FText& OutFailure)
{
	using namespace CastleBuildingTriggerPath;
	OutPath = FCastleBuildingTriggerPath();
	OutFailure = FText::GetEmpty();
	if (!Path.bValid || !Path.bClosedLoop || !FMath::IsFinite(Path.Length) || Path.Length < 0.0
		|| Path.PatrolPoints.IsEmpty() || Path.PatrolPoints.Num() != Path.PatrolDistances.Num())
	{
		OutFailure = FText::FromString(TEXT("建筑进入编译需要有效闭环及完整的贴地采样距离。"));
		return false;
	}
	OutPath.PathRevision = Path.PathRevision;
	OutPath.RegistryRevision = InRegistryRevision;
	OutPath.Length = Path.Length;
	if (Path.Length <= UE_DOUBLE_SMALL_NUMBER) { return true; }
	for (const FCastleBuildingTriggerRegion& Region : Regions)
	{
		TArray<FInterval> Intervals;
		for (int32 Index = 1; Index < Path.PatrolPoints.Num(); ++Index)
		{
			const double Distance = Path.PatrolDistances[Index - 1];
			const double SegmentLength = Path.PatrolDistances[Index] - Distance;
			if (SegmentLength <= UE_DOUBLE_SMALL_NUMBER) { continue; }
			const FVector Start = Region.LocalToWorld.InverseTransformPosition(Path.PatrolPoints[Index - 1]);
			const FVector End = Region.LocalToWorld.InverseTransformPosition(Path.PatrolPoints[Index]);
			for (const FBox& Box : Region.LocalBoxes)
			{
				double First, Last;
				if (ClipSegment(Start, End, Box, First, Last))
				{
					Intervals.Add({Distance + First * SegmentLength, Distance + Last * SegmentLength});
				}
			}
		}
		Intervals.Sort([](const FInterval& A, const FInterval& B) { return A.Start < B.Start; });
		TArray<FInterval> Merged;
		for (const FInterval& Interval : Intervals)
		{
			if (!Merged.IsEmpty() && Interval.Start <= Merged.Last().End + UE_DOUBLE_SMALL_NUMBER)
			{
				Merged.Last().End = FMath::Max(Merged.Last().End, Interval.End);
			}
			else { Merged.Add(Interval); }
		}
		const bool bContinuousAcrossSeam = !Merged.IsEmpty() && Merged[0].Start <= UE_DOUBLE_SMALL_NUMBER
			&& Merged.Last().End >= Path.Length - UE_DOUBLE_SMALL_NUMBER;
		for (int32 Index = 0; Index < Merged.Num(); ++Index)
		{
			// 路线零点只是一种坐标选择；首尾连在楼内不能额外送一次进入。
			// 整圈均在楼内时唯一的区间也被跳过，之后绕多少圈都不会触发。
			if (Index == 0 && bContinuousAcrossSeam) { continue; }
			FCastleBuildingEntryMarker Marker;
			Marker.BuildingId = Region.BuildingId;
			Marker.Distance = Merged[Index].Start;
			FVector Direction;
			Path.SamplePatrol(Marker.Distance, Marker.Location, Direction);
			OutPath.Entries.Add(Marker);
		}
	}
	OutPath.Entries.Sort([](const FCastleBuildingEntryMarker& A, const FCastleBuildingEntryMarker& B)
	{
		return A.Distance == B.Distance ? A.BuildingId < B.BuildingId : A.Distance < B.Distance;
	});
	return true;
}

void FCastleBuildingTriggerPath::ForEachEntry(const double StartDistance, const double TravelDistance,
	const TFunctionRef<void(const FCastleBuildingEntryMarker&)> Visitor) const
{
	if (Entries.IsEmpty() || Length <= UE_DOUBLE_SMALL_NUMBER || TravelDistance <= 0.0) { return; }
	// 二分跳过本帧起点及之前的标记；停在进入边界后，下一帧不会再次消费相同标记。
	int32 Index = Algo::UpperBoundBy(Entries, StartDistance, &FCastleBuildingEntryMarker::Distance);
	double LapBase = 0.0;
	if (Index == Entries.Num()) { Index = 0; LapBase = Length; }
	const double End = StartDistance + TravelDistance;
	while (LapBase + Entries[Index].Distance <= End)
	{
		Visitor(Entries[Index]);
		if (++Index == Entries.Num()) { Index = 0; LapBase += Length; }
	}
}
