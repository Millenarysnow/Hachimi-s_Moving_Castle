// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastleTowerAttackDataAsset.generated.h"

class UMassEntityConfigAsset;

/** 单个等级的塔攻击参数；每座塔每次触发会为待射队列增加一发。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleTowerAttackLevelData
{
	GENERATED_BODY()

	/** 对应建筑等级，必须在同一资产中恰好覆盖 1 至 5 级。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "塔攻击|等级")
	int32 Level = 0;

	/** 待射队列的发射速度，单位为每秒发射数，必须为有限正数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "塔攻击|等级")
	float AttacksPerSecond = 0.0f;

	/** 每发追踪子弹命中时造成的伤害，必须为有限正数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "塔攻击|等级")
	float ProjectileDamage = 0.0f;

	/** 查找敌人的球形半径，单位为厘米，必须为有限正数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "塔攻击|等级", meta = (Units = "cm"))
	float AttackRadius = 0.0f;
};

/**
 * 一种塔的共享攻击数据。建筑等级从建筑 Actor 读取，本资产只提供精确等级参数，
 * 不保存实例队列，也不在缺配置时回退到默认等级。
 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleTowerAttackDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 追踪箭矢使用的 Mass 实体配置；必须含追踪 Trait，缺失时拒绝发射。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "塔攻击")
	TObjectPtr<UMassEntityConfigAsset> ProjectileConfiguration;

	/** 塔 Actor 局部向上的发射点高度偏移，单位为厘米；零表示从 Actor 原点发射。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "塔攻击", meta = (Units = "cm"))
	float LaunchHeightOffset = 0.0f;

	/** 此塔 1 至 5 级的完整攻击参数，不允许重复或缺级。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "塔攻击", meta = (TitleProperty = "Level"))
	TArray<FCastleTowerAttackLevelData> Levels;

	/** 返回精确等级的攻击参数；未配置时返回空指针，不寻找其他等级替代。 */
	const FCastleTowerAttackLevelData* FindLevel(int32 InLevel) const;

	/** 校验子弹配置、1 至 5 级覆盖和各项攻击数值。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|塔攻击")
	bool ValidateConfiguration(FText& OutFailureReason) const;

#if WITH_EDITOR
	/** 内容浏览器的“验证资产”使用相同校验规则。 */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
