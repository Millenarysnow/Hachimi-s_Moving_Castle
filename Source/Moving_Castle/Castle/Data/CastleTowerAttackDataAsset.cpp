// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleTowerAttackDataAsset.h"

#include "Units/Mass/Projectiles/CastleMassHomingProjectileTrait.h"
#include "MassEntityConfigAsset.h"
#include "Misc/DataValidation.h"

const FCastleTowerAttackLevelData* UCastleTowerAttackDataAsset::FindLevel(const int32 InLevel) const
{
	return Levels.FindByPredicate([InLevel](const FCastleTowerAttackLevelData& Entry)
	{
		return Entry.Level == InLevel;
	});
}

bool UCastleTowerAttackDataAsset::ValidateConfiguration(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (!IsValid(ProjectileConfiguration))
	{
		OutFailureReason = FText::FromString(FString::Printf(
			TEXT("塔攻击资产 %s 没有配置追踪子弹 Mass 实体配置。"), *GetPathName()));
		return false;
	}
	if (ProjectileConfiguration->FindTrait(UCastleMassHomingProjectileTrait::StaticClass()) == nullptr)
	{
		OutFailureReason = FText::FromString(FString::Printf(
			TEXT("塔攻击资产 %s 的子弹配置没有 UCastleMassHomingProjectileTrait。"), *GetPathName()));
		return false;
	}
	if (!FMath::IsFinite(LaunchHeightOffset))
	{
		OutFailureReason = FText::FromString(FString::Printf(
			TEXT("塔攻击资产 %s 的发射点高度偏移必须是有限数值。"), *GetPathName()));
		return false;
	}

	TSet<int32> SeenLevels;
	for (const FCastleTowerAttackLevelData& Entry : Levels)
	{
		if (Entry.Level < 1 || Entry.Level > 5)
		{
			OutFailureReason = FText::FromString(FString::Printf(
				TEXT("塔攻击资产 %s 的等级 %d 超出 1 至 5 级范围。"), *GetPathName(), Entry.Level));
			return false;
		}
		if (SeenLevels.Contains(Entry.Level))
		{
			OutFailureReason = FText::FromString(FString::Printf(
				TEXT("塔攻击资产 %s 重复配置了 %d 级。"), *GetPathName(), Entry.Level));
			return false;
		}
		SeenLevels.Add(Entry.Level);

		if (!FMath::IsFinite(Entry.AttacksPerSecond) || Entry.AttacksPerSecond <= 0.0f)
		{
			OutFailureReason = FText::FromString(FString::Printf(
				TEXT("塔攻击资产 %s 的 %d 级攻击速度必须是有限正数。"), *GetPathName(), Entry.Level));
			return false;
		}
		if (!FMath::IsFinite(Entry.ProjectileDamage) || Entry.ProjectileDamage <= 0.0f)
		{
			OutFailureReason = FText::FromString(FString::Printf(
				TEXT("塔攻击资产 %s 的 %d 级子弹伤害必须是有限正数。"), *GetPathName(), Entry.Level));
			return false;
		}
		if (!FMath::IsFinite(Entry.AttackRadius) || Entry.AttackRadius <= 0.0f)
		{
			OutFailureReason = FText::FromString(FString::Printf(
				TEXT("塔攻击资产 %s 的 %d 级攻击半径必须是有限正数。"), *GetPathName(), Entry.Level));
			return false;
		}
	}

	for (int32 RequiredLevel = 1; RequiredLevel <= 5; ++RequiredLevel)
	{
		if (!SeenLevels.Contains(RequiredLevel))
		{
			OutFailureReason = FText::FromString(FString::Printf(
				TEXT("塔攻击资产 %s 缺少 %d 级配置。"), *GetPathName(), RequiredLevel));
			return false;
		}
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UCastleTowerAttackDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	FText FailureReason;
	if (!ValidateConfiguration(FailureReason))
	{
		Context.AddError(FailureReason);
		return EDataValidationResult::Invalid;
	}
	return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif
