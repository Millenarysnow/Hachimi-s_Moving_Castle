#pragma once

#include "MassEntityTypes.h"
#include "CastleMassEnemyRangedAttackFragments.generated.h"

class UMassEntityConfigAsset;

/** 敌方远程攻击独立状态；不与近战共用冷却，也不让基类隐式获得远程能力。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassEnemyRangedAttackFragment : public FMassFragment
{
	GENERATED_BODY()
	UPROPERTY() float AttackDamage = 15.0f;
	UPROPERTY() float AttackInterval = 1.25f;
	UPROPERTY() float AttackRange = 2200.0f;
	/** 倍率可以很接近零，有效冷却用双精度保存，避免变成无穷。 */
	UPROPERTY() double AttackCooldown = 0.0;
	UPROPERTY() bool bInAttackRange = false;
	UPROPERTY() int32 AttackCount = 0;
	/** 配置资产由生成它的 Trait 持有；发射时仍需验证投射物能力。 */
	UPROPERTY() TWeakObjectPtr<UMassEntityConfigAsset> ProjectileConfiguration;
};
