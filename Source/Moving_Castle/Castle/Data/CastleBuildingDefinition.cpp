// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingDefinition.h"

#include "CastleBuildingBase.h"
#include "Misc/DataValidation.h"

const FCastleBuildingLevelDefinition* UCastleBuildingDefinition::FindLevel(int32 InLevel) const
{
	return Levels.FindByPredicate([InLevel](const FCastleBuildingLevelDefinition& Entry)
	{
		return Entry.Level == InLevel;
	});
}

bool UCastleBuildingDefinition::ValidateConfiguration(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (DisplayName.IsEmpty() || Levels.IsEmpty() || Levels.Num() > 5)
	{
		OutFailureReason = FText::FromString(TEXT("建筑定义必须填写名称，并配置 1 至 5 个连续等级。"));
		return false;
	}
	if (BuildingType > ECastleBuildingType::Farm || (BuildingType == ECastleBuildingType::Road && Levels.Num() != 1))
	{
		OutFailureReason = FText::FromString(TEXT("建筑类别无效，或道路配置了一级以外的等级。"));
		return false;
	}

	TSet<int32> SeenLevels;
	for (const FCastleBuildingLevelDefinition& Entry : Levels)
	{
		// 等级必须覆盖 1..Num，避免配置了四级却无法经过三级升级到达。
		if (Entry.Level < 1 || Entry.Level > Levels.Num() || SeenLevels.Contains(Entry.Level))
		{
			OutFailureReason = FText::FromString(TEXT("建筑等级必须从 1 开始连续且不重复；缺模型的等级也要保留配置项。"));
			return false;
		}
		SeenLevels.Add(Entry.Level);
		if (Entry.BuildingClass == nullptr || Entry.BuildingClass->HasAnyClassFlags(CLASS_Abstract))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("%s 的 %d 级没有可生成的建筑逻辑类。"), *DisplayName.ToString(), Entry.Level));
			return false;
		}
		const ACastleBuildingBase* Defaults = Entry.BuildingClass->GetDefaultObject<ACastleBuildingBase>();
		if (Defaults->GetBuildingType() != BuildingType || Defaults->GetBuildingLevel() != Entry.Level)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("%s 的 %d 级逻辑类与配置的类别或等级不一致。"), *DisplayName.ToString(), Entry.Level));
			return false;
		}
		if (!Entry.StaticMesh.IsNull() && !Entry.ModelActorClass.IsNull())
		{
			OutFailureReason = FText::FromString(TEXT("同一级只能配置静态网格或模型 Actor，不能同时配置两者。"));
			return false;
		}
		if (!Entry.ModelTransform.IsValid() || Entry.ModelTransform.GetScale3D().GetMin() <= UE_SMALL_NUMBER)
		{
			OutFailureReason = FText::FromString(TEXT("模型变换必须有效，且三个轴的缩放均大于零。"));
			return false;
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
