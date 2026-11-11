// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CastleMassSpawner.h"
#include "CastleMassEnemySpawner.generated.h"

/**
 * 基础敌人专用的 Mass 批量生成器。
 *
 * 它只负责产生主塔周围的圆环位置，敌人的直线移动和攻击仍由 Mass
 * Processor 负责；生成器自身不 Tick，也不为每个敌人创建 Actor。
 */
UCLASS()
class MOVING_CASTLE_API ACastleMassEnemySpawner : public ACastleMassSpawner
{
	GENERATED_BODY()

public:
	/** 设置基础敌人默认配置和圆环生成参数。 */
	ACastleMassEnemySpawner();

	/** 圆环内半径，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass|敌人", meta = (ClampMin = "0.0", Units = "cm"))
	float InnerRadius = 3000.0f;

	/** 圆环外半径，单位为厘米，必须大于 InnerRadius。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass|敌人", meta = (ClampMin = "0.0", Units = "cm"))
	float OuterRadius = 4500.0f;

	/** 生成点相对主塔敌人目标点的高度偏移。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass|敌人", meta = (Units = "cm"))
	float HeightOffset = 0.0f;

	/** 圆环随机流种子；相同种子和数量会复现同一批位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mass|敌人")
	int32 RandomSeed = 20261112;

	/** 在主塔周围圆环随机生成敌人；范围错误或主塔缺失时整批拒绝。 */
	virtual bool SpawnUnits() override;
};
