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
};

/**
 * 敌人专用战斗状态；攻击逻辑只写入当前实体的值数据，不创建逐敌人 UObject。
 *
 * AttackCount 和 bInAttackRange 同时用于运行时诊断，便于在伤害暂时配置为 0
 * 时仍能确认敌人已经停在主塔附近并持续执行攻击事件。
 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassEnemyFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY()
	float AttackDamage = 0.0f;

	UPROPERTY()
	float AttackInterval = 1.0f;

	UPROPERTY()
	float AttackRange = 150.0f;

	UPROPERTY()
	float AttackCooldown = 0.0f;

	UPROPERTY()
	bool bInAttackRange = false;

	UPROPERTY()
	int32 AttackCount = 0;
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
