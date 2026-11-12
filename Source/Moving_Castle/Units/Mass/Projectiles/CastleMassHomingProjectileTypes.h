#pragma once

#include "CoreMinimal.h"
#include "CastleMassTypes.h"
#include "CastleMassHomingProjectileTypes.generated.h"

/** 单发追踪子弹的生成参数；每发的伤害必须由攻击方显式写入，不读取 Trait 的模板伤害。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleMassHomingProjectileSpawn
{
	GENERATED_BODY()

	/** 世界空间发射点；生成整批前检查所有坐标均为有限数值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="追踪子弹")
	FVector Origin = FVector::ZeroVector;

	/** 本发锁定的敌人；世界身份和 Mass 序列号共同防止跨世界或实体复用误伤。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="追踪子弹")
	FCastleMassUnitHandle Target;

	/** 本发伤害，允许零伤害；负数或非有限数值导致整批拒绝。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="追踪子弹")
	float Damage = 0.0f;
};
