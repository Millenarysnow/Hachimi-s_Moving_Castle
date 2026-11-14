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
	const bool bNormalRoom = Definition->BuildingType == ECastleBuildingType::NormalRoom;
	const bool bValidColor = Color >= ECastleTileColor::Red && Color <= ECastleTileColor::Purple;
	if (!bValidColor || Number < 1 || Number > 9 || (bNormalRoom && Level != 1))
	{
		OutFailureReason = FText::FromString(TEXT("所有房间必须使用五色之一与 1 至 9 数字；普通房间固定为 1 级。"));
		return false;
	}
	return true;
}
