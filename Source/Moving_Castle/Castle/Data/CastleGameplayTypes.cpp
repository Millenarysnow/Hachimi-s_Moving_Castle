// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleGameplayTypes.h"

#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"

bool FCastleBuildingDescriptor::HasDefinition() const
{
	return IsValid(Definition) && Definition->FindLevel(Level) != nullptr;
}

TSubclassOf<ACastleBuildingBase> FCastleBuildingDescriptor::GetBuildingClass() const
{
	const FCastleBuildingLevelDefinition* Entry = IsValid(Definition) ? Definition->FindLevel(Level) : nullptr;
	return Entry != nullptr ? Entry->BuildingClass : nullptr;
}

ECastleBuildingType FCastleBuildingDescriptor::GetBuildingType() const
{
	checkf(IsValid(Definition), TEXT("读取建筑类别前必须先验证建筑 Data Asset。"));
	return Definition->BuildingType;
}

bool FCastleBuildingDescriptor::Validate(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (!IsValid(Definition))
	{
		OutFailureReason = FText::FromString(TEXT("建筑卡牌没有配置建筑 Data Asset。"));
		return false;
	}
	if (!Definition->ValidateConfiguration(OutFailureReason))
	{
		return false;
	}
	if (Definition->FindLevel(Level) == nullptr)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("%s 没有配置 %d 级。"), *Definition->DisplayName.ToString(), Level));
		return false;
	}
	const bool bRoad = Definition->BuildingType == ECastleBuildingType::Road;
	const bool bValidColor = Color >= ECastleTileColor::Red && Color <= ECastleTileColor::Purple;
	if ((bRoad && (Level != 1 || Color != ECastleTileColor::Gray || Number != 0))
		|| (!bRoad && (!bValidColor || Number < 1 || Number > 9)))
	{
		OutFailureReason = FText::FromString(TEXT("道路必须是灰色 0 号 1 级；功能建筑必须是有效颜色的 1 至 9 号。"));
		return false;
	}
	return true;
}
