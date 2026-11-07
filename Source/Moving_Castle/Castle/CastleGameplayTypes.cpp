// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleGameplayTypes.h"

#include "CastleBuildingBase.h"

bool FCastleBuildingDescriptor::HasBuildingClass() const
{
	// 该实现放在拥有完整 ACastleBuildingBase 定义的 cpp 中，避免数据头的前置声明
	// 触发 TSubclassOf 模板对不完整类型的 StaticClass 调用。
	return BuildingClass != nullptr;
}
