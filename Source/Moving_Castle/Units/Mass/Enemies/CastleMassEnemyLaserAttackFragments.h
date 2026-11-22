#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "CastleMassEnemyLaserAttackFragments.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** 激光实体值状态；弱资产引用由 Trait/配置资产维持生命周期。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassEnemyLaserAttackFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY() float AttackDamage = 5.0f;
	UPROPERTY() float AttackInterval = 1.0f;
	UPROPERTY() float AttackRange = 800.0f;
	UPROPERTY() FVector MuzzleLocalOffset = FVector(25.0f, 0.0f, 65.0f);
	UPROPERTY() float BeamThickness = 10.0f;
	UPROPERTY() TWeakObjectPtr<UStaticMesh> BeamMesh;
	UPROPERTY() TWeakObjectPtr<UMaterialInterface> BeamMaterial;
	/** 倍率可以很接近零，有效冷却用双精度保存，避免变成无穷。 */
	UPROPERTY() double AttackCooldown = 0.0;
	UPROPERTY() int32 AttackCount = 0;
	UPROPERTY() bool bInAttackRange = false;
	/** 只由攻击处理器写端点，显示处理器在全部伤害结算后确认实体仍存活才提交。 */
	UPROPERTY() FVector BeamStart = FVector::ZeroVector;
	UPROPERTY() FVector BeamEnd = FVector::ZeroVector;
};
