// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingBase.h"

#include "Moving_Castle.h"

ACastleBuildingBase::ACastleBuildingBase()
{
	// 建筑 Actor 当前阶段不绑定任何美术组件，只保留可供棋盘和蓝图访问的数据。
	PrimaryActorTick.bCanEverTick = false;
}
bool ACastleBuildingBase::InitializeBuilding(const FCastleBuildingDescriptor& Descriptor, const TArray<FIntVector>& InOccupiedCoordinates)
{
	if (!Descriptor.HasBuildingClass())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：没有提供 BuildingClass。"));
		return false;
	}

	if (Descriptor.BuildingClass != GetClass())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：描述类与实际 Actor 类不一致。"));
		return false;
	}

	if (Descriptor.Level < 1 || Descriptor.Level != BuildingLevel)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：描述等级 %d 与 Actor 等级 %d 不一致。"), Descriptor.Level, BuildingLevel);
		return false;
	}

	if (Descriptor.BuildingType != BuildingType)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：描述类别与 Actor 类别不一致。"));
		return false;
	}

	if (InOccupiedCoordinates.Num() == 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：建筑至少需要占用一个网格坐标。"));
		return false;
	}

	if (Descriptor.Number < 0 || Descriptor.Number > 9)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：数字 %d 超出 0 到 9 的范围。"), Descriptor.Number);
		return false;
	}

	if (BuildingType == ECastleBuildingType::Road)
	{
		if (Descriptor.Color != ECastleTileColor::Gray || Descriptor.Number != 0)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：道路必须是灰色 0 号。"));
			return false;
		}
	}
	else if (Descriptor.Color == ECastleTileColor::Gray || Descriptor.Number == 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：功能建筑不能使用灰色或 0 号属性。"));
		return false;
	}

	TSet<FIntVector> UniqueCoordinates;
	for (const FIntVector& Coordinate : InOccupiedCoordinates)
	{
		if (UniqueCoordinates.Contains(Coordinate))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：占用坐标出现重复值 (%d, %d, %d)。"), Coordinate.X, Coordinate.Y, Coordinate.Z);
			return false;
		}

		UniqueCoordinates.Add(Coordinate);
	}

	const int32 ExpectedCellCount = GetExpectedFootprintCellCount(BuildingLevel);
	if (ExpectedCellCount > 0 && InOccupiedCoordinates.Num() != ExpectedCellCount)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：等级 %d 需要 %d 个占用格子，实际收到 %d 个。"), BuildingLevel, ExpectedCellCount, InOccupiedCoordinates.Num());
		return false;
	}

	BuildingType = Descriptor.BuildingType;
	Color = Descriptor.Color;
	Number = Descriptor.Number;
	OccupiedCoordinates = InOccupiedCoordinates;
	return true;
}

FCastleBuildingDescriptor ACastleBuildingBase::GetBuildingDescriptor() const
{
	FCastleBuildingDescriptor Descriptor;
	Descriptor.BuildingClass = GetClass();
	Descriptor.BuildingType = BuildingType;
	Descriptor.Color = Color;
	Descriptor.Number = Number;
	Descriptor.Level = BuildingLevel;
	return Descriptor;
}

TSubclassOf<ACastleBuildingBase> ACastleBuildingBase::GetClassForLevel(int32 InLevel) const
{
	// 基类没有具体等级类，具体建筑族必须覆盖此函数才能参与合成替换。
	return nullptr;
}

bool ACastleBuildingBase::CanMergeToNextLevel() const
{
	// 4 级是策划案规定的自动合成上限，5 级可以由后续奖励系统直接生成。
	if (BuildingType == ECastleBuildingType::Road || BuildingLevel < 1 || BuildingLevel >= 4)
	{
		return false;
	}

	return GetClassForLevel(BuildingLevel + 1) != nullptr;
}

bool ACastleBuildingBase::CanMergeWith(const ACastleBuildingBase* Other) const
{
	if (Other == nullptr || Other == this || !CanMergeToNextLevel())
	{
		return false;
	}

	if (BuildingType != Other->BuildingType || Color != Other->Color || Number != Other->Number || BuildingLevel != Other->BuildingLevel)
	{
		return false;
	}

	// 通过同一等级映射类判断是否属于同一个建筑族，防止不同功能建筑误合成。
	return GetClassForLevel(BuildingLevel) == Other->GetClassForLevel(Other->BuildingLevel);
}

int32 ACastleBuildingBase::GetExpectedFootprintCellCount(int32 InLevel)
{
	if (InLevel < 1 || InLevel > 5)
	{
		return 0;
	}

	// 等级 1、2、3、4 分别对应 1、2、4、8 个合成方块；5 级由奖励直接升级，
	// 仍然代表一座建筑，因此恢复为单格占用而不是继续扩大合成形状。
	return InLevel == 5 ? 1 : (1 << (InLevel - 1));
}

