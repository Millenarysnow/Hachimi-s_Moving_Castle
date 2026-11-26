// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 主塔真实战斗塔身的有向方柱快照。
 *
 * 几何只覆盖宽 515 cm、底面位于 Actor 根节点的塔身，不包含网格挑檐与烘焙挂环。
 * 半尺寸已吸收 Actor 各轴缩放，位置和朝向仍使用 Actor 世界变换。
 */
struct MOVING_CASTLE_API FCastleTowerPrism
{
	static constexpr float DefaultShaftWidth = 515.0f;
	static constexpr float DefaultShaftHeight = 1680.0f;

	FVector BottomCenter = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	/** X/Y 是半边长，Z 是从 BottomCenter 起算的完整塔身高度。 */
	FVector HalfSize = FVector(DefaultShaftWidth * 0.5f, DefaultShaftWidth * 0.5f, DefaultShaftHeight);

	/** 从主塔 Actor 当前变换构造快照；非法尺寸或缩放时写入原因并返回失败。 */
	static bool Build(const FTransform& ActorTransform, float ShaftWidth, float ShaftHeight,
		FCastleTowerPrism& OutPrism, FString& OutFailureReason);

	/** 查询球心到四个侧面及檐口的最近点、有向距离和外法线，底面按地面边界处理。 */
	bool QuerySurface(const FVector& WorldPoint, FVector& OutClosestPoint,
		FVector& OutOutwardNormal, float& OutSignedDistance) const;

	/** 将球心推出塔身外扩半径，且仅移除指向塔内的速度法向分量。 */
	bool ProjectSphereOutside(FVector& InOutCenter, float Radius, FVector& InOutVelocity) const;

	/** 把世界坐标变换到以塔根为底面中心的无缩放局部空间。 */
	FVector ToLocalPoint(const FVector& WorldPoint) const;

	/** 返回与塔 Actor 旋转一致的世界上方向。 */
	FVector GetUpVector() const;
};
