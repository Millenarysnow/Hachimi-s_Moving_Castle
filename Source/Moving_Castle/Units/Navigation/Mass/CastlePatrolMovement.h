#pragma once

struct FCastleSharedNavigationPath;
struct FCastleMassMovementSettings;

namespace CastlePatrolMovement
{
	/**
	 * 按楼板、梯子和高速公路边界精确分段积分一帧，只读取数值快照。
	 * InOutSpeed 为所在区间的实际速度；道路倍率不进入跨格保留的基础速度。
	 * 非法梯子倍率返回 false，并将速度设为零；已在合法楼板上到达梯口的距离仍可正常消费。
	 */
	bool Advance(const FCastleSharedNavigationPath& Path, const FCastleMassMovementSettings& Settings,
		double StartDistance, double DeltaTime, double& InOutSpeed, double& OutTravel, bool bStopAtEnd = false);
}
