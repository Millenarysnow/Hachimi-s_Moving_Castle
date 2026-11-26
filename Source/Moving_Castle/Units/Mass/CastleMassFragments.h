#pragma once

#include "MassEntityTypes.h"
#include "CastleMassTypes.h"
#include "CastleMassFragments.generated.h"

USTRUCT()
struct MOVING_CASTLE_API FCastleMassUnitTag : public FMassTag { GENERATED_BODY() };

USTRUCT()
struct MOVING_CASTLE_API FCastleMassEnemyTag : public FMassTag { GENERATED_BODY() };

USTRUCT()
struct MOVING_CASTLE_API FCastleMassNPCTag : public FMassTag { GENERATED_BODY() };

USTRUCT()
struct MOVING_CASTLE_API FCastleMassProjectileTag : public FMassTag { GENERATED_BODY() };

USTRUCT()
struct MOVING_CASTLE_API FCastleMassHumanoidTag : public FMassTag { GENERATED_BODY() };

USTRUCT()
struct MOVING_CASTLE_API FCastleMassUnitFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	ECastleMassUnitKind Kind = ECastleMassUnitKind::Enemy;

	UPROPERTY()
	int32 Team = 0;

	UPROPERTY()
	float Health = 100.0f;

	/** 小于等于 0 表示没有自动过期时间。 */
	UPROPERTY()
	float Lifetime = 0.0f;

	UPROPERTY()
	float Age = 0.0f;

	/** 空投系统只修改此倍率；计时结束恢复中性值，不改共享模板移速。 */
	float SupportMovementMultiplier = 1.0f;

	/** 敌人所处分批的永久移速倍率；支援效果到期时不修改它。 */
	float SpawnMovementMultiplier = 1.0f;
};

USTRUCT()
struct MOVING_CASTLE_API FCastleMassMoveTargetFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Target = FVector::ZeroVector;

	UPROPERTY()
	FVector DesiredVelocity = FVector::ZeroVector;

	UPROPERTY()
	bool bHasTarget = false;
};

/**
 * 攻击处理器交给唯一移动处理器的塔面意图。
 *
 * 攻击处理器只更新索敌状态和朝向，移动处理器根据当前塔身几何积分法向贴靠、
 * 三维软分离与塔面切向滑移，不在攻击处理器中清零速度或写入位置。
 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassFlightIntentFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	bool bTrackingMainTower = false;

	UPROPERTY()
	bool bInAttackRange = false;

	UPROPERTY()
	bool bStopMovement = false;

	UPROPERTY()
	float AttackRange = 0.0f;

	UPROPERTY()
	float IdealStandOff = 0.0f;

	UPROPERTY()
	FVector TowerSurfacePoint = FVector::ZeroVector;

	UPROPERTY()
	FVector TowerSurfaceNormal = FVector::ZeroVector;
};

/** NPC 农田增益的实体值状态；所有 NPC 默认装配，建筑只通过单位子系统读写。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassNPCFarmBuffFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 累计的附加数字，初始零；增加时拒绝非正数、非有限数及溢出。 */
	UPROPERTY()
	float BonusNumber = 0.0f;
};

USTRUCT()
struct MOVING_CASTLE_API FCastleMassMovementParameters : public FMassConstSharedFragment
{
	GENERATED_BODY()

	UPROPERTY()
	FCastleMassMovementSettings Settings;
};

/** 命中系统可读取上一帧位置和伤害数据执行连续检测，不需要为每颗子弹创建碰撞 Actor。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassProjectileFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	FVector PreviousLocation = FVector::ZeroVector;

	UPROPERTY()
	float Gravity = 0.0f;

	UPROPERTY()
	float Damage = 10.0f;
};

USTRUCT()
struct MOVING_CASTLE_API FCastleMassAnimationParameters : public FMassConstSharedFragment
{
	GENERATED_BODY()

	UPROPERTY()
	int32 IdleSequence = 0;

	UPROPERTY()
	int32 MoveSequence = 1;

	UPROPERTY()
	float IdleLength = 1.0f;

	UPROPERTY()
	float MoveLength = 1.0f;

	UPROPERTY()
	float MovePlayRate = 1.0f;

	UPROPERTY()
	int32 AttackSequence = INDEX_NONE;

	UPROPERTY()
	float AttackLength = 1.0f;

	UPROPERTY()
	float AttackPlayRate = 1.0f;

	UPROPERTY()
	float BlendTime = 0.15f;
};

USTRUCT()
struct MOVING_CASTLE_API FCastleMassAnimationFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 生成时按批次索引打散相位；每个实体没有 AnimInstance 或骨骼组件。 */
	UPROPERTY()
	float Phase = 0.0f;

	UPROPERTY()
	float Position = 0.0f;

	UPROPERTY()
	int32 CurrentSequence = INDEX_NONE;

};

/** 由任一攻击能力写入，供通用动画处理器切换待机、移动和攻击序列。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassAttackAnimationFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	bool bInAttackState = false;
};
