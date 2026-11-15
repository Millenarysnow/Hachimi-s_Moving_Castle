#pragma once

struct FCastleSharedNavigationPath;
struct FCastleMassMovementSettings;

namespace CastlePatrolMovement
{
	/**
	 * 按楼板/梯子边界精确分段积分一帧的标量进度；所有输入都是不可变数值，不访问导航或 UObject。
	 * 非法梯子倍率返回 false，并将速度设为零；已在合法楼板上到达梯口的距离仍可正常消费。
	 */
	bool Advance(const FCastleSharedNavigationPath& Path, const FCastleMassMovementSettings& Settings,
		double StartDistance, double DeltaTime, double& InOutSpeed, double& OutTravel);
}
