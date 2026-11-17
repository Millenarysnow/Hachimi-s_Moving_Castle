// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingDefinition.h"

#include "CastleBuildingBase.h"
#include "CastleTowerAttackDataAsset.h"
#include "Castle/Support/CastleSupportTowerData.h"
#include "Misc/DataValidation.h"

const FCastleBuildingLevelDefinition* UCastleBuildingDefinition::FindLevel(int32 InLevel) const
{
	return Levels.FindByPredicate([InLevel](const FCastleBuildingLevelDefinition& Entry)
	{
		return Entry.Level == InLevel;
	});
}

TSubclassOf<ACastleBuildingBase> UCastleBuildingDefinition::ResolveBuildingClassForDimensions(
	const int32 InLevel, const FIntVector& Dimensions) const
{
	const FCastleBuildingLevelDefinition* Entry = FindLevel(InLevel);
	if (Entry == nullptr)
	{
		return nullptr;
	}

	// 只有二级和三级允许垂直合成；这里按完整包围盒尺寸区分水平与垂直类，
	// 不对不完整、斜接或超出等级规则的占用形状做自动修正。
	if (InLevel == 2 && Dimensions == FIntVector(1, 1, 2))
	{
		return Entry->VerticalBuildingClass;
	}
	if (InLevel == 3 && (Dimensions == FIntVector(2, 1, 2) || Dimensions == FIntVector(1, 2, 2)))
	{
		return Entry->VerticalBuildingClass;
	}

	const bool bHorizontalShape = (InLevel == 1 && Dimensions == FIntVector(1, 1, 1))
		|| (InLevel == 2 && (Dimensions == FIntVector(2, 1, 1) || Dimensions == FIntVector(1, 2, 1)))
		|| (InLevel == 3 && Dimensions == FIntVector(2, 2, 1))
		|| (InLevel == 4 && Dimensions == FIntVector(2, 2, 2))
		|| (InLevel == 5 && Dimensions == FIntVector(1, 1, 1));
	return bHorizontalShape ? Entry->BuildingClass : nullptr;
}

bool UCastleBuildingDefinition::ValidateConfiguration(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (DisplayName.IsEmpty() || Levels.IsEmpty() || Levels.Num() > 5)
	{
		OutFailureReason = FText::FromString(TEXT("建筑定义必须填写名称，并配置 1 至 5 个连续等级。"));
		return false;
	}
	// 普通房间保持单级，功能建筑仍严格校验连续等级。
	if (BuildingType > ECastleBuildingType::BalloonTower || (BuildingType == ECastleBuildingType::NormalRoom && Levels.Num() != 1))
	{
		OutFailureReason = FText::FromString(TEXT("建筑类别无效，或普通房间配置了一级以外的等级。"));
		return false;
	}
	if (BuildingType == ECastleBuildingType::ArcherTower || BuildingType == ECastleBuildingType::CannonTower)
	{
		if (!IsValid(AttackData))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("%s 没有配置攻击塔 Data Asset。"), *DisplayName.ToString()));
			return false;
		}
		if (!AttackData->ValidateConfiguration(OutFailureReason))
		{
			return false;
		}
	}

	if (BuildingType == ECastleBuildingType::BeeHive || BuildingType == ECastleBuildingType::BalloonTower)
	{
		// 四级为这两类塔的完整配置；资产缺级或没绑定数据时拒绝购买、预览和放置。
		if (Levels.Num() != 4 || !IsValid(SupportData))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("支援塔定义 %s 必须配置四个等级和支援数据资产。"), *GetPathName()));
			return false;
		}
		if (!SupportData->ValidateFor(BuildingType, OutFailureReason)) { return false; }
	}

	TSet<int32> SeenLevels;
	for (const FCastleBuildingLevelDefinition& Entry : Levels)
	{
		// 等级必须覆盖 1..Num，避免配置了四级却缺少通向它的三级类定义。
		if (Entry.Level < 1 || Entry.Level > Levels.Num() || SeenLevels.Contains(Entry.Level))
		{
			OutFailureReason = FText::FromString(TEXT("建筑等级必须从 1 开始连续且不重复；缺模型的等级也要保留配置项。"));
			return false;
		}
		SeenLevels.Add(Entry.Level);
		if (Entry.BuildingClass == nullptr || Entry.BuildingClass->HasAnyClassFlags(CLASS_Abstract))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("%s 的 %d 级没有可生成的建筑类。"), *DisplayName.ToString(), Entry.Level));
			return false;
		}
		const ACastleBuildingBase* Defaults = Entry.BuildingClass->GetDefaultObject<ACastleBuildingBase>();
		if (Defaults->GetBuildingType() != BuildingType || Defaults->GetBuildingLevel() != Entry.Level)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("%s 的 %d 级建筑类与配置的类别或等级不一致。"), *DisplayName.ToString(), Entry.Level));
			return false;
		}

		// 功能塔二级和三级必须显式提供垂直类；缺失时直接拒绝配置，
		// 防止垂直合成静默退回水平模型或生成占位对象。
		const bool bRequiresVerticalClass = BuildingType != ECastleBuildingType::NormalRoom;
		if (bRequiresVerticalClass && (Entry.Level == 2 || Entry.Level == 3))
		{
			if (Entry.VerticalBuildingClass == nullptr || Entry.VerticalBuildingClass->HasAnyClassFlags(CLASS_Abstract))
			{
				OutFailureReason = FText::FromString(FString::Printf(TEXT("%s 的 %d 级缺少有效垂直建筑类。"), *DisplayName.ToString(), Entry.Level));
				return false;
			}
			const ACastleBuildingBase* VerticalDefaults = Entry.VerticalBuildingClass->GetDefaultObject<ACastleBuildingBase>();
			if (VerticalDefaults == nullptr || VerticalDefaults->GetBuildingType() != BuildingType
				|| VerticalDefaults->GetBuildingLevel() != Entry.Level)
			{
				OutFailureReason = FText::FromString(FString::Printf(TEXT("%s 的 %d 级垂直建筑类与配置的类别或等级不一致。"), *DisplayName.ToString(), Entry.Level));
				return false;
			}
		}
	}
	return true;
}

bool UCastleBuildingDefinition::CreateDescriptor(int32 InLevel, ECastleTileColor Color, int32 Number,
	FCastleBuildingDescriptor& OutDescriptor, FText& OutFailureReason) const
{
	OutDescriptor = FCastleBuildingDescriptor();
	FCastleBuildingDescriptor Candidate;
	// 描述只持有资产身份，不修改资产；const_cast 用于赋给 UObject 引用属性。
	Candidate.Definition = const_cast<UCastleBuildingDefinition*>(this);
	Candidate.Level = InLevel;
	Candidate.Color = Color;
	Candidate.Number = Number;
	if (!Candidate.Validate(OutFailureReason))
	{
		return false;
	}
	OutDescriptor = Candidate;
	return true;
}

#if WITH_EDITOR
EDataValidationResult UCastleBuildingDefinition::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	FText Reason;
	if (!ValidateConfiguration(Reason))
	{
		Context.AddError(Reason);
		return EDataValidationResult::Invalid;
	}
	return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif
