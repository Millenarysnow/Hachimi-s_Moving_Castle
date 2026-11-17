#include "Castle/Combat/CastleCombatConfigDataAsset.h"

#include "Castle/Combat/CastleDifficultyCurveDataAsset.h"
#include "Units/Mass/CastleMassTraits.h"
#include "MassEntityConfigAsset.h"

bool UCastleCombatConfigDataAsset::ValidateConfiguration(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	const auto Reject = [this, &OutFailureReason](const FString& Field, const FString& Reason)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("战斗配置 %s：%s：%s"), *GetPathName(), *Field, *Reason));
		return false;
	};
	if (Roster.IsEmpty()) { return Reject(TEXT("Roster[0]"), TEXT("敌人名册不能为空。")); }
	TSet<FName> EnemyIds;
	for (int32 Index = 0; Index < Roster.Num(); ++Index)
	{
		const auto& Enemy = Roster[Index];
		const FString Path = FString::Printf(TEXT("Roster[%d]"), Index);
		if (Enemy.EnemyId.IsNone() || EnemyIds.Contains(Enemy.EnemyId))
		{
			return Reject(Path + TEXT(".EnemyId"), FString::Printf(TEXT("%s 为 None 或重复。"), *Enemy.EnemyId.ToString()));
		}
		EnemyIds.Add(Enemy.EnemyId);
		if (!IsValid(Enemy.Configuration) || !Enemy.Configuration->FindTrait(UCastleMassEnemyTrait::StaticClass()))
		{
			return Reject(Path + TEXT(".Configuration"), FString::Printf(TEXT("%s 缺失或不含 UCastleMassEnemyTrait。"), *GetPathNameSafe(Enemy.Configuration)));
		}
		if (Enemy.MinClusterCount < 1 || Enemy.MaxClusterCount < Enemy.MinClusterCount)
		{
			return Reject(Path + TEXT(".MinClusterCount/MaxClusterCount"), TEXT("必须满足 1 <= MinClusterCount <= MaxClusterCount。"));
		}
		if (!FMath::IsFinite(Enemy.ClusterSpreadRadius) || Enemy.ClusterSpreadRadius < 0
			|| (Enemy.MaxClusterCount > 1 && Enemy.ClusterSpreadRadius <= 0))
		{
			return Reject(Path + TEXT(".ClusterSpreadRadius"), TEXT("必须为有限非负数，多实体簇必须大于零。"));
		}
	}
	if (!IsValid(DifficultyCurve)) { return Reject(TEXT("DifficultyCurve"), TEXT("曲线资产缺失。")); }
	FText CurveFailure;
	if (!DifficultyCurve->ValidateConfiguration(CurveFailure)) { return Reject(TEXT("DifficultyCurve"), CurveFailure.ToString()); }
	const TArray<FCastleCombatBattleDefinition>* Tables[] = {&DifficultyCurve->NormalBattles, &DifficultyCurve->EliteBattles};
	const TCHAR* Names[] = {TEXT("NormalBattles"), TEXT("EliteBattles")};
	for (int32 Table = 0; Table < 2; ++Table)
	{
		for (int32 Battle = 0; Battle < Tables[Table]->Num(); ++Battle)
		{
			const auto& Groups = (*Tables[Table])[Battle].Groups;
			for (int32 Group = 0; Group < Groups.Num(); ++Group)
			{
				if (!EnemyIds.Contains(Groups[Group].EnemyId))
				{
					return Reject(FString::Printf(TEXT("DifficultyCurve %s.%s[%d].Groups[%d].EnemyId"), *DifficultyCurve->GetPathName(), Names[Table], Battle, Group),
						FString::Printf(TEXT("%s 不在名册中。"), *Groups[Group].EnemyId.ToString()));
				}
			}
		}
	}
	if (!FMath::IsFinite(SpawnInnerRadius) || !FMath::IsFinite(SpawnOuterRadius) || SpawnInnerRadius < 0 || SpawnOuterRadius <= SpawnInnerRadius)
	{
		return Reject(TEXT("SpawnInnerRadius/SpawnOuterRadius"), TEXT("必须是有限数且满足 0 <= 内半径 < 外半径。"));
	}
	if (!FMath::IsFinite(SpawnHeightOffset)) { return Reject(TEXT("SpawnHeightOffset"), TEXT("必须为有限数。")); }
	return true;
}
