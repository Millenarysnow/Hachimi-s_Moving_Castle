// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastleGameplayTypes.h"
#include "CastleBuildingDefinition.generated.h"

class AActor;
class UStaticMesh;

/** 同一种建筑的一级配置；逻辑仍由 C++ 建筑类实现，模型只负责表现。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleBuildingLevelDefinition
{
	GENERATED_BODY()

	/** 实际等级，从 1 开始连续配置；空模型不等于缺失这个等级。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "等级", meta = (ClampMin = "1", ClampMax = "5"))
	int32 Level = 1;

	/** 此等级使用的 C++ 逻辑类，类别和等级必须与所属定义一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "等级")
	TSubclassOf<ACastleBuildingBase> BuildingClass;

	/** 单个静态网格模型，与 ModelActorClass 二选一；两者都留空时使用立方体占位。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "模型")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	/** 多零件模型可直接引用已有 Actor 蓝图；该蓝图只提供模型，不承担建筑逻辑。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "模型")
	TSoftClassPtr<AActor> ModelActorClass;

	/** 模型相对于建筑占用范围中心的变换，单位为厘米；原有立方体占位不使用此变换。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "模型")
	FTransform ModelTransform = FTransform::Identity;
};

/**
 * 一种建筑唯一的共享定义。
 * 放置、合成、背包和商店均保存此资产的引用以及卡牌等级、颜色、数字。
 * 修改任一级模型后，场上表现与卡片预览会在下次生成时读取同一份配置。
 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleBuildingDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 背包、商店、建筑标签与操作反馈共用的中文名称。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "建筑")
	FText DisplayName;

	/** 用于放置规则和功能判断，不能与等级逻辑类的类别冲突。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "建筑")
	ECastleBuildingType BuildingType = ECastleBuildingType::Road;

	/** 一份资产配置该建筑的所有等级；道路只配置一级。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "建筑", meta = (TitleProperty = "Level"))
	TArray<FCastleBuildingLevelDefinition> Levels;

	/** 查找准确等级，不把缺失等级替换为其他等级或其他建筑。 */
	const FCastleBuildingLevelDefinition* FindLevel(int32 InLevel) const;

	/** 校验名称、连续等级、逻辑类和互斥模型配置；空模型是用户允许的合法配置。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑定义")
	bool ValidateConfiguration(FText& OutFailureReason) const;

	/** 创建携带本资产引用的卡牌，所有入口使用同一份描述校验。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|建筑定义")
	bool CreateDescriptor(int32 InLevel, ECastleTileColor Color, int32 Number,
		FCastleBuildingDescriptor& OutDescriptor, FText& OutFailureReason) const;

#if WITH_EDITOR
	/** 内容浏览器的“验证资产”使用相同规则，配置错误可在进入游戏前发现。 */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
