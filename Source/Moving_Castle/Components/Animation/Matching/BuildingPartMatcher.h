#pragma once

#include "CoreMinimal.h"

/** 一件网格用于匹配的几何身份与实际世界位置；不依赖 Actor 的等级、名称或导出顺序。 */
struct FBuildingPartMatchPoint
{
	FString Identity;
	FVector Center = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	double Radius = 0.0;
};

namespace BuildingPartMatcher
{
	/**
	 * 同一身份内进行一对一最小总代价匹配，结果按目标索引返回来源索引。
	 * INDEX_NONE 表示真实新增件；多出来的来源件由调用方作为移除件处理。
	 * 先分身份再求解，不能为了省距离把屋顶和立柱错误互换。
	 */
	TArray<int32> Match(const TArray<FBuildingPartMatchPoint>& Sources, const TArray<FBuildingPartMatchPoint>& Targets);
}
