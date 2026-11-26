// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Castle/Combat/CastleTowerPrism.h"

/** 单个存活敌人的不可变帧快照，供同帧对称的群体解算使用。 */
struct MOVING_CASTLE_API FCastleMassFlightBody
{
	int32 EntityIndex = INDEX_NONE;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	FVector DesiredVelocity = FVector::ZeroVector;
	float Radius = 0.0f;
	float MaxSpeed = 0.0f;
	float Acceleration = 0.0f;
	float AcceptanceRadius = 0.0f;
	float AttackRange = 0.0f;
	float IdealStandOff = 0.0f;
	bool bHasTarget = false;
	bool bTrackingMainTower = false;
	bool bInAttackRange = false;
	bool bStopMovement = false;
	bool bSpawnBurst = false;
};

/** 一次完整移动积分的结果；结果数组与输入快照使用相同索引。 */
struct MOVING_CASTLE_API FCastleMassFlightResult
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	bool bReachedTarget = false;
};

/**
 * 纯值类型的三维 Boids 和塔面约束解算器。
 *
 * 每个子步以空间网格查询邻域，先计算全体结果再同时推进快照；塔面法向与切向
 * 分量分别处理，避免攻击状态的法向阻尼吞掉前后单位沿墙绕流时需要的切向速度。
 */
class MOVING_CASTLE_API FCastleMassFlightSolver
{
public:
	/** 解算一个完整帧；参数非法或几何查询失败时返回 false，不修改输入快照。 */
	static bool Solve(const TArray<FCastleMassFlightBody>& Bodies, float DeltaTime,
		const FCastleTowerPrism* Tower, TArray<FCastleMassFlightResult>& OutResults,
		FString& OutFailureReason);
};
