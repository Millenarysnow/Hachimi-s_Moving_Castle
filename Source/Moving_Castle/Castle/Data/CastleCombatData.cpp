// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleCombatData.h"

#include "CastleMassTraits.h"
#include "MassEntityConfigAsset.h"

bool UCastleCombatDataAsset::ValidateConfiguration(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();

	if (!FMath::IsFinite(MainTowerMaxHealth) || MainTowerMaxHealth <= 0.0f)
	{
		OutFailureReason = FText::FromString(TEXT("战斗配置的主塔生命必须是有限正数。"));
		return false;
	}
	if (!FMath::IsFinite(MainTowerTargetHeightOffset))
	{
		OutFailureReason = FText::FromString(TEXT("战斗配置的主塔目标高度偏移必须是有限数值。"));
		return false;
	}
	if (!IsValid(EnemyConfiguration) || EnemyConfiguration->FindTrait(UCastleMassEnemyTrait::StaticClass()) == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("战斗配置必须引用包含 Castle Mass 敌人 Trait 的配置资产。"));
		return false;
	}
	if (InitialEnemyCount <= 0)
	{
		OutFailureReason = FText::FromString(TEXT("战斗配置的开局敌人数量必须大于 0。"));
		return false;
	}
	if (!FMath::IsFinite(EnemySpawnInnerRadius) || !FMath::IsFinite(EnemySpawnOuterRadius)
		|| EnemySpawnInnerRadius < 0.0f || EnemySpawnOuterRadius <= EnemySpawnInnerRadius)
	{
		OutFailureReason = FText::FromString(TEXT("敌人生成圆环必须满足 0 <= 内半径 < 外半径。"));
		return false;
	}
	if (!FMath::IsFinite(EnemySpawnHeightOffset))
	{
		OutFailureReason = FText::FromString(TEXT("敌人生成高度偏移必须是有限数值。"));
		return false;
	}

	return true;
}
