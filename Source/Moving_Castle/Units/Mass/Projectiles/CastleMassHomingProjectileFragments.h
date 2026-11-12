#pragma once

#include "MassEntityTypes.h"
#include "CastleMassHomingProjectileFragments.generated.h"

/** 追踪能力的逐实体状态；伤害和上一帧位置继续使用子弹基类 Fragment。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassHomingProjectileFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 只在专用批量生成入口写入一次，不重新索敌；序列号防止伤害复用槽位的新单位。 */
	UPROPERTY()
	FMassEntityHandle Target;
};
