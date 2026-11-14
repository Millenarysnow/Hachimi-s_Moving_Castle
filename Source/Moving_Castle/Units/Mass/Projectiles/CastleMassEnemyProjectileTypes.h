#pragma once

#include "CoreMinimal.h"
#include "CastleMassTypes.h"
#include "CastleMassEnemyProjectileTypes.generated.h"

class UCastleMainTowerComponent;

/** 一发敌方追踪弹的完整发射契约；ShieldId 非零与 TargetTower 二者必须且只能设置一个。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassEnemyProjectileSpawn
{
	GENERATED_BODY()
	UPROPERTY() FVector Origin = FVector::ZeroVector;
	UPROPERTY() FCastleMassUnitHandle Attacker;
	UPROPERTY() uint64 ShieldId = 0;
	/** 锁定具体主塔组件，不在飞行中将过期目标替换成新主塔。 */
	UPROPERTY() TWeakObjectPtr<UCastleMainTowerComponent> TargetTower;
	UPROPERTY() float Damage = 0.0f;
};
