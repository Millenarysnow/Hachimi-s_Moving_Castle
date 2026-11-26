// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleMassFlightTargeting.h"

#include "CastleMainTowerComponent.h"
#include "Moving_Castle.h"

namespace CastleMassFlightTargeting
{
	bool SetMainTowerTarget(UCastleMainTowerSubsystem* TowerSubsystem,
		const FTransform& Transform, const float ConfiguredRadius, const float AttackRange,
		const float IdealStandOff, const int32 EntityIndex,
		FCastleMassMoveTargetFragment& MoveTarget, FCastleMassFlightIntentFragment& FlightIntent,
		FVector& OutSurfacePoint, float& OutSurfaceDistance)
	{
		OutSurfacePoint = FVector::ZeroVector;
		OutSurfaceDistance = 0.0f;
		const FVector Scale = Transform.GetScale3D().GetAbs();
		const float Radius = ConfiguredRadius * Scale.GetAbsMax();
		if (!TowerSubsystem || !FMath::IsFinite(ConfiguredRadius) || ConfiguredRadius < 0.0f
			|| !FMath::IsFinite(Radius) || !FMath::IsFinite(AttackRange) || AttackRange <= 0.0f
			|| !FMath::IsFinite(IdealStandOff) || IdealStandOff <= 0.0f || Transform.ContainsNaN())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("敌人实体 %d 主塔索敌失败：塔身查询、逻辑半径、射程或贴靠距离无效。"), EntityIndex);
			return false;
		}

		FCastleTowerPrism Tower;
		if (!TowerSubsystem->GetMainTowerPrism(Tower))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("敌人实体 %d 主塔索敌失败：当前主塔没有有效塔身几何。"), EntityIndex);
			return false;
		}

		const FVector Center = Transform.GetLocation() + Tower.GetUpVector() * Radius;
		FVector SurfaceNormal;
		if (!Tower.QuerySurface(Center, OutSurfacePoint, SurfaceNormal, OutSurfaceDistance))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("敌人实体 %d 主塔索敌失败：无法查询真实塔身表面。"), EntityIndex);
			return false;
		}

		// 塔身底面是地面边界，实体根节点必须位于塔根平面以上。
		const FVector DesiredCenter = OutSurfacePoint + SurfaceNormal * IdealStandOff;
		MoveTarget.Target = DesiredCenter - Tower.GetUpVector() * Radius;
		MoveTarget.DesiredVelocity = FVector::ZeroVector;
		MoveTarget.bHasTarget = true;
		FlightIntent.bTrackingMainTower = true;
		FlightIntent.bInAttackRange = OutSurfaceDistance >= 0.0f && OutSurfaceDistance <= AttackRange;
		FlightIntent.bStopMovement = false;
		FlightIntent.AttackRange = AttackRange;
		FlightIntent.IdealStandOff = IdealStandOff;
		FlightIntent.TowerSurfacePoint = OutSurfacePoint;
		FlightIntent.TowerSurfaceNormal = SurfaceNormal;
		return true;
	}

	void SetAlternateTarget(const FVector& TargetLocation, const float AttackRange,
		const bool bInAttackRange, FCastleMassMoveTargetFragment& MoveTarget,
		FCastleMassFlightIntentFragment& FlightIntent)
	{
		FlightIntent.bTrackingMainTower = false;
		FlightIntent.bInAttackRange = bInAttackRange;
		FlightIntent.bStopMovement = false;
		FlightIntent.AttackRange = AttackRange;
		FlightIntent.IdealStandOff = 0.0f;
		FlightIntent.TowerSurfacePoint = FVector::ZeroVector;
		FlightIntent.TowerSurfaceNormal = FVector::ZeroVector;
		if (bInAttackRange)
		{
			MoveTarget = FCastleMassMoveTargetFragment();
		}
		else
		{
			MoveTarget.Target = TargetLocation;
			MoveTarget.DesiredVelocity = FVector::ZeroVector;
			MoveTarget.bHasTarget = true;
		}
	}

	void SetStopIntent(FCastleMassMoveTargetFragment& MoveTarget,
		FCastleMassFlightIntentFragment& FlightIntent)
	{
		MoveTarget = FCastleMassMoveTargetFragment();
		FlightIntent.bTrackingMainTower = false;
		FlightIntent.bInAttackRange = false;
		FlightIntent.bStopMovement = true;
		FlightIntent.AttackRange = 0.0f;
		FlightIntent.IdealStandOff = 0.0f;
		FlightIntent.TowerSurfacePoint = FVector::ZeroVector;
		FlightIntent.TowerSurfaceNormal = FVector::ZeroVector;
	}
}
