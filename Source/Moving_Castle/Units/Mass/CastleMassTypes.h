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

/** 敌人批次的实例属性；创建时应用一次，共享模板与死亡分裂子体仍使用自身配置。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleMassEnemyAttributeOverrides
{
	GENERATED_BODY()

	/** 大于零时指定绝对生命；零表示继续使用模板生命及其倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="敌人覆写")
	float HealthOverride = 0.0f;

	/** 生命倍率；设置绝对生命时必须为中性的 1。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="敌人覆写")
	float HealthMultiplier = 1.0f;

	/** 作用于模板移速上限，与支援塔实时移速修正相乘。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="敌人覆写")
	float MoveSpeedMultiplier = 1.0f;

	/** 作用于实体已有的近战、远程或激光伤害。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="敌人覆写")
	float DamageMultiplier = 1.0f;

	/** 作用于模板攻击间隔，大于 1 时两次攻击间隔更久。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="敌人覆写")
	float AttackIntervalMultiplier = 1.0f;

	/** 按比例缩放本次生成变换，骨骼实例消费该实体的变换。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="敌人覆写")
	float ScaleMultiplier = 1.0f;

	/** 返回首个非法字段，供战斗配置和 Mass 入口附上各自的资产与条目上下文。 */
	const TCHAR* GetInvalidField() const;
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

	/** 梯子区间内的最大速度倍率；必须为有限数且位于 (0.01, 10.0]。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="移动")
	float LadderSpeedMultiplier = 1.0f;

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

	/** 敌人是否已经进入当前护盾或主塔的攻击距离。 */
	UPROPERTY(BlueprintReadOnly, Category="战斗")
	bool bInAttackRange = false;

	/** 已完成的近战攻击或远程发射次数，零伤害攻击也会计入。 */
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
