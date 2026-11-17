#pragma once

#include "CoreMinimal.h"
#include "CastleMassTypes.h"
#include "CastleMassEnemyProjectileTypes.generated.h"

class UCastleMainTowerComponent;

/** 一发敌方追踪弹只能锁定一个对象：激活护盾、在空气球或主塔组件。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassEnemyProjectileSpawn
{
	GENERATED_BODY()
	UPROPERTY() FVector Origin = FVector::ZeroVector;
	UPROPERTY() FCastleMassUnitHandle Attacker;
	UPROPERTY() uint64 ShieldId = 0;
	/** 气球破裂后旧弹随即失去目标；不能改打主塔或碰撞货箱。 */
	UPROPERTY() uint64 BalloonId = 0;
	/** 锁定具体主塔组件，不在飞行中将过期目标替换成新主塔。 */
	UPROPERTY() TWeakObjectPtr<UCastleMainTowerComponent> TargetTower;
	UPROPERTY() float Damage = 0.0f;
};
