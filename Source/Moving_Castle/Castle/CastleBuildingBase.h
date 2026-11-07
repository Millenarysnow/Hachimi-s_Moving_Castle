// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleGameplayTypes.h"
#include "CastleBuildingBase.generated.h"

/**
 * 所有可放置房间的共同 Actor 基类。
 *
 * 基类只保存棋盘、背包和合成所需的通用数据，不包含敌人、NPC 或战斗行为。
 * 每个功能建筑族再派生自己的等级类，合成时由棋盘删除旧 Actor 并生成新类。
 */
UCLASS(Abstract, Blueprintable)
class MOVING_CASTLE_API ACastleBuildingBase : public AActor
{
	GENERATED_BODY()

public:
	/** 构造函数只设置通用的默认属性，不依赖 World 或棋盘。 */
	ACastleBuildingBase();

	/**
	 * 用背包描述和占用格子初始化一个新建筑。
	 *
	 * 棋盘负责在调用前检查坐标是否可用；该函数仍会校验数据，避免其他调用者
	 * 绕过棋盘写入道路颜色、数字或重复坐标。
	 */
	bool InitializeBuilding(const FCastleBuildingDescriptor& Descriptor, const TArray<FIntVector>& InOccupiedCoordinates);

	/** 返回当前建筑的类别。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	ECastleBuildingType GetBuildingType() const
	{
		return BuildingType;
	}

	/** 返回当前建筑的颜色。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	ECastleTileColor GetBuildingColor() const
	{
		return Color;
	}

	/** 返回当前建筑的数字。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	int32 GetBuildingNumber() const
	{
		return Number;
	}

	/** 返回当前建筑的等级。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	int32 GetBuildingLevel() const
	{
		return BuildingLevel;
	}

	/** 返回建筑所占用的全部网格坐标副本，避免外部直接修改棋盘索引。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	TArray<FIntVector> GetOccupiedCoordinates() const
	{
		return OccupiedCoordinates;
	}

	/** 返回建筑描述，删除建筑或放回背包时使用。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	FCastleBuildingDescriptor GetBuildingDescriptor() const;

	/** 判断当前建筑是否属于道路。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	bool IsRoad() const
	{
		return BuildingType == ECastleBuildingType::Road;
	}

	/**
	 * 返回指定等级对应的具体 Actor 类。
	 *
	 * 基类没有等级映射；箭塔、炮塔和农田族会覆盖此函数，返回各自的等级
	 * 子类。Road 不支持合成，因此保持返回空类。
	 */
	virtual TSubclassOf<ACastleBuildingBase> GetClassForLevel(int32 InLevel) const;

	/** 判断当前建筑是否还有可合成的下一级，功能建筑默认最多自动合成到 4 级。 */
	bool CanMergeToNextLevel() const;

	/** 判断两个建筑是否满足同类别、同等级、同颜色和同数字的基础合成条件。 */
	bool CanMergeWith(const ACastleBuildingBase* Other) const;

	/** 返回当前等级合成所需的单个建筑占用格子数。 */
	static int32 GetExpectedFootprintCellCount(int32 InLevel);

protected:
	/** 建筑类别由具体建筑族构造函数设置，初始化时会再次校验描述。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑")
	ECastleBuildingType BuildingType = ECastleBuildingType::Road;

	/** 建筑颜色由放置卡牌提供。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑")
	ECastleTileColor Color = ECastleTileColor::Gray;

	/** 建筑数字由放置卡牌提供。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑")
	int32 Number = 0;

	/** 建筑等级由具体等级子类的构造函数设置。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑")
	int32 BuildingLevel = 1;

	/** 该 Actor 当前占用的全部网格坐标，合成后的 Actor 会拥有多个坐标。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑")
	TArray<FIntVector> OccupiedCoordinates;
};

