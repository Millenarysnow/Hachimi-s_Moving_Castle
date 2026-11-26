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
	/** 调度器显式标记门 A 派出的 NPC；调试闭环的单位不读取战斗派兵模式。 */
	bool bTowerDispatched = false;
	/** 由单圈临时派遣设置，回收和累计圈数都与正式士兵分开处理。 */
	bool bTemporaryOneLap = false;
	/** 手动模式到门 B 后保持停止，直到调度器在 Mass 安全边界提交回收。 */
	bool bReachedDispatchEntrance = false;
	bool bBlocked = false;
	int32 PathRevision = INDEX_NONE;
	int32 RouteRevision = INDEX_NONE;
	int32 Leg = 0;
	int64 CompletedLaps = 0;
	double Distance = 0.0;
	double Speed = 0.0;
};
