// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CastleMassFragments.h"

class UCastleMainTowerSubsystem;

namespace CastleMassFlightTargeting
{
	/**
	 * 将主塔索敌转换为表面攻击点与逻辑停靠目标。
	 *
	 * @param TowerSubsystem 当前 World 的主塔查询契约。
	 * @param Transform 当前敌人实体变换，模型 MeshOffset 不参与半径缩放。
	 * @param ConfiguredRadius 来自 FAgentRadiusFragment 的配置半径。
	 * @param AttackRange 兵种攻击射程。
	 * @param IdealStandOff 球心到真实塔身表面的理想距离。
	 * @param EntityIndex 供无效参数日志标识实体。
	 * @param OutSurfacePoint 用于攻击朝向、激光终点和敌方追踪弹命中点。
	 * @param OutSurfaceDistance 敌人球心到最近塔面的有符号距离。
	 */
	bool SetMainTowerTarget(UCastleMainTowerSubsystem* TowerSubsystem,
		const FTransform& Transform, float ConfiguredRadius, float AttackRange, float IdealStandOff,
		int32 EntityIndex, FCastleMassMoveTargetFragment& MoveTarget,
		FCastleMassFlightIntentFragment& FlightIntent, FVector& OutSurfacePoint,
		float& OutSurfaceDistance);

	/** 将气球或护盾目标写入通用移动意图，不参与塔面停靠计算。 */
	void SetAlternateTarget(const FVector& TargetLocation, float AttackRange, bool bInAttackRange,
		FCastleMassMoveTargetFragment& MoveTarget, FCastleMassFlightIntentFragment& FlightIntent);

	/** 目标全部失效时只提交停止意图，由移动处理器负责消除速度。 */
	void SetStopIntent(FCastleMassMoveTargetFragment& MoveTarget,
		FCastleMassFlightIntentFragment& FlightIntent);
}
