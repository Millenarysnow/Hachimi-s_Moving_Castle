#pragma once

#include "CoreMinimal.h"

struct FCastleSharedNavigationPath;

/** 登记时冻结的逻辑占用区域；不保存 UObject，空间计算可独立验证。 */
struct MOVING_CASTLE_API FCastleBuildingTriggerRegion
{
	uint64 BuildingId = 0;
	FTransform LocalToWorld = FTransform::Identity;
	TArray<FBox> LocalBoxes;
};

/** 一次真实进入在闭环上的位置；同栋楼的多格区间先合并，再生成此标记。 */
struct MOVING_CASTLE_API FCastleBuildingEntryMarker
{
	uint64 BuildingId = 0;
	double Distance = 0.0;
	FVector Location = FVector::ZeroVector;
};

/**
 * 只读进入快照。空间相交按路线/登记版本集中计算一次，工作线程只做距离查找。
 * 进入标记保留同一建筑在一圈中的多次出入，不退化为“每楼每圈一次”。
 */
struct MOVING_CASTLE_API FCastleBuildingTriggerPath
{
	int32 PathRevision = INDEX_NONE;
	uint64 RegistryRevision = 0;
	double Length = 0.0;
	TArray<FCastleBuildingEntryMarker> Entries;

	/** 沿精确折线裁剪区域；相邻区间及闭环首尾的同次停留不重复生成标记。 */
	static bool Compile(const FCastleSharedNavigationPath& Path, uint64 InRegistryRevision,
		TConstArrayView<FCastleBuildingTriggerRegion> Regions, FCastleBuildingTriggerPath& OutPath, FText& OutFailure);

	/** 起点排除、终点包含，跨圈时按行走顺序枚举每次进入，不截断大时间步的真实事件。 */
	void ForEachEntry(double StartDistance, double TravelDistance,
		TFunctionRef<void(const FCastleBuildingEntryMarker&)> Visitor) const;
};
