#pragma once

#include "MassEntityTypes.h"
#include "CastleMassBuildingTraversalFragment.generated.h"

/**
 * 巡逻位移本轮实际经过的路线区间。路线重绑定先完成，再记录起点，不能把新旧进度差当成移动。
 * 序号保证重复执行检测、暂停或切换到普通命令时，不会再次消费上轮位移。
 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassBuildingTraversalFragment : public FMassFragment
{
	GENERATED_BODY()

	int32 PathRevision = INDEX_NONE;
	double StartDistance = 0.0;
	double TravelDistance = 0.0;
	uint64 MovementSerial = 0;
	uint64 ConsumedMovementSerial = 0;
};
