#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "CastleMassTypes.generated.h"

UENUM(BlueprintType)
enum class ECastleMassUnitKind : uint8
{
	Enemy,
	NPC,
	Projectile
};

/** 蓝图只持有值类型句柄；世界引用和 Mass 序列号共同防止误操作其他世界或复用后的实体。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleMassUnitHandle
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FMassEntityHandle Entity;

	UPROPERTY(Transient)
	TWeakObjectPtr<UWorld> World;
};

/** 一份配置由同类实体共享；目标点和实时速度单独存储在每个实体上。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleMassMovementSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="移动", meta=(ClampMin="0.0", Units="cm/s"))
	float MaxSpeed = 300.0f;

	/** 为 0 时直接使用期望速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="移动", meta=(ClampMin="0.0"))
	float Acceleration = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="移动", meta=(ClampMin="0.0", Units="cm"))
	float AcceptanceRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="移动", meta=(ClampMin="0.0", Units="deg/s"))
	float TurnRate = 540.0f;

	/** 人形只旋转朝向角，仍可沿目标点的 Z 方向移动；不会自动进行地面吸附或寻路。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="移动")
	bool bKeepUpright = true;
};

USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleMassUnitSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="单位")
	ECastleMassUnitKind Kind = ECastleMassUnitKind::Enemy;

	UPROPERTY(BlueprintReadOnly, Category="单位")
	FTransform Transform;

	UPROPERTY(BlueprintReadOnly, Category="单位")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="单位")
	float Health = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="单位")
	int32 Team = 0;

	UPROPERTY(BlueprintReadOnly, Category="单位")
	bool bHasMoveTarget = false;

	/** 敌人是否已经进入主塔攻击距离。 */
	UPROPERTY(BlueprintReadOnly, Category="战斗")
	bool bInAttackRange = false;

	/** 已完成的主塔攻击次数，零伤害攻击也会计入。 */
	UPROPERTY(BlueprintReadOnly, Category="战斗")
	int32 AttackCount = 0;

	/** 调试与验收读取巡逻状态；高频整军查询仍应使用 Mass Query。 */
	UPROPERTY(BlueprintReadOnly, Category="巡逻")
	bool bFollowingSharedRoute = false;

	UPROPERTY(BlueprintReadOnly, Category="巡逻")
	bool bPatrolBlocked = false;

	UPROPERTY(BlueprintReadOnly, Category="巡逻")
	int32 PatrolLeg = 0;

	UPROPERTY(BlueprintReadOnly, Category="巡逻")
	int64 CompletedPatrolLaps = 0;
};
