// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastleGameplayTypes.h"
#include "CastleBuildingDefinition.generated.h"

class UBuildingPartCatalog;

/**
 * 同一种建筑的一级配置。
 *
 * 每个等级只保存一个最终生成类：没有模型组件的 C++ 类使用统一立方体占位，
 * 带模型的蓝图则直接继承对应等级 C++ 类。这样棋盘、合成、背包和商店始终
 * 生成同一个 Actor，不再额外维护模型 Actor 引用。
 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleBuildingLevelDefinition
{
	GENERATED_BODY()

	/** 实际等级，从 1 开始连续配置；空模型不等于缺失这个等级。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "等级", meta = (ClampMin = "1", ClampMax = "5"))
	int32 Level = 1;

	/** 此等级最终生成的 C++ 或蓝图类；类别和等级必须与所属定义一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "等级")
	TSubclassOf<ACastleBuildingBase> BuildingClass;
};

/**
 * 一种建筑唯一的共享定义。
 * 放置、合成、背包和商店均保存此资产的引用以及卡牌等级、颜色、数字。
 * 修改某级生成类或对应等级蓝图后，场上建筑与卡片预览都在下次生成时使用相同配置。
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

	/** 各等级使用不同导入资产时，通过离线几何目录建立可复用零件关系。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="建筑|变化动画")
	TObjectPtr<UBuildingPartCatalog> PartCatalog;

	/** 查找准确等级，不把缺失等级替换为其他等级或其他建筑。 */
	const FCastleBuildingLevelDefinition* FindLevel(int32 InLevel) const;

	/** 校验名称、连续等级和最终生成类；没有模型组件的类使用统一立方体占位。 */
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
