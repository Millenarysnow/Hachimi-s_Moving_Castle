// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastleCombatData.generated.h"

class UMassEntityConfigAsset;

/**
 * 当前战斗原型的集中配置。
 *
 * 主塔生命、敌人批次生成参数和敌人 Mass 配置从同一份 Data Asset 读取；
 * 敌人生命与移动由 EnemyConfiguration 的基础 Trait 配置，具体攻击由其
 * 可选能力 Trait 配置（例如近战），避免在棋盘或基类中写入兵种逻辑。
 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleCombatDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 内容浏览器中显示的战斗配置名称。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗")
	FText DisplayName;

	/** 主塔初始生命，当前原型默认 1000。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|主塔", meta = (ClampMin = "0.01"))
	float MainTowerMaxHealth = 1000.0f;

	/** 敌人追踪主塔根节点的高度偏移。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|主塔", meta = (Units = "cm"))
	float MainTowerTargetHeightOffset = 250.0f;

	/** 开局使用的 Mass 敌人配置；必须包含 UCastleMassEnemyTrait。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|敌人")
	TObjectPtr<UMassEntityConfigAsset> EnemyConfiguration;

	/** 开局生成的敌人数量；当前原型默认为 1。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|敌人", meta = (ClampMin = "1"))
	int32 InitialEnemyCount = 1;

	/** 敌人圆环内半径和外半径，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|敌人", meta = (ClampMin = "0.0", Units = "cm"))
	float EnemySpawnInnerRadius = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|敌人", meta = (ClampMin = "0.0", Units = "cm"))
	float EnemySpawnOuterRadius = 1800.0f;

	/** 敌人相对主塔目标点的额外飞行高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|敌人", meta = (Units = "cm"))
	float EnemySpawnHeightOffset = 0.0f;

	/** 圆环随机流种子；相同配置和种子会得到相同生成位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "战斗|敌人")
	int32 EnemySpawnSeed = 20261112;

	/** 校验主塔生命、圆环范围和 Mass 敌人配置，失败时明确返回原因。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|战斗")
	bool ValidateConfiguration(FText& OutFailureReason) const;
};
