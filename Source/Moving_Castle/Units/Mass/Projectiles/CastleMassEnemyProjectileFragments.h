#pragma once

#include "MassEntityTypes.h"
#include "CastleMassEnemyProjectileTypes.h"
#include "CastleMassEnemyProjectileFragments.generated.h"

/** 敌方弹只追踪发射时锁定的护盾 ID 或主塔；攻击者句柄用于命中护盾时即时反伤。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassEnemyProjectileFragment : public FMassFragment
{
	GENERATED_BODY()
	UPROPERTY() FCastleMassEnemyProjectileSpawn Launch;
};
