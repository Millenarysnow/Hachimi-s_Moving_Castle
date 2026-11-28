// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleGameplayTypes.h"

#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"

TConstArrayView<ECastleTileColor> CastleTileColors::GetPlayable()
{
	// 列表顺序用于工坊展示和随机索引，保持资产序号与显示顺序相互独立。
	static constexpr ECastleTileColor Colors[] = {
		ECastleTileColor::Red, ECastleTileColor::Yellow, ECastleTileColor::Blue, ECastleTileColor::Green
	};
	return MakeArrayView(Colors);
}

bool CastleTileColors::IsPlayable(const ECastleTileColor Color)
{
	return GetPlayable().Contains(Color);
}

bool CastleTileColors::TryGetGridTint(const ECastleTileColor Color, FLinearColor& OutTint)
{
	// 塔侧格和地面保留相同的材质色值，玩家切换场景时仍能按同一颜色识别匹配规则。
	switch (Color)
	{
	case ECastleTileColor::Red: OutTint = FLinearColor(0.75f, 0.08f, 0.08f); return true;
	case ECastleTileColor::Yellow: OutTint = FLinearColor(0.90f, 0.60f, 0.04f); return true;
	case ECastleTileColor::Blue: OutTint = FLinearColor(0.06f, 0.25f, 0.80f); return true;
	case ECastleTileColor::Green: OutTint = FLinearColor(0.05f, 0.58f, 0.18f); return true;
	default: return false;
	}
}

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
	if (!CastleTileColors::IsPlayable(Color) || Number < 1 || Number > 9 || (bNormalRoom && Level != 1))
	{
		OutFailureReason = FText::FromString(FString::Printf(
			TEXT("房间 %s 必须使用红、黄、蓝、绿之一与 1 至 9 数字，普通房间固定为 1 级（颜色=%d，数字=%d，等级=%d）。"),
			*Definition->GetPathName(), static_cast<int32>(Color), Number, Level));
		return false;
	}
	return true;
}
