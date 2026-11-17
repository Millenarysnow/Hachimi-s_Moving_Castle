#pragma once

#include "MassEntityTypes.h"
#include "CastleMassPatrolFragments.generated.h"

/** NPC 只存自己的进度与开关，不存路径数组、导航多边形或逐实体寻路请求。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassPatrolFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 通过生成器或批量 FollowSharedPatrolRoute 显式启用；普通直线移动不会被巡逻抢占。 */
	bool bEnabled = false;
	/** 临时集结路线拥有本轮移动，巡逻距离和原移动目标均暂停。 */
	bool bSupportJourney = false;
	bool bBlocked = false;
	int32 PathRevision = INDEX_NONE;
	int32 RouteRevision = INDEX_NONE;
	int32 Leg = 0;
	int64 CompletedLaps = 0;
	double Distance = 0.0;
	double Speed = 0.0;
};
