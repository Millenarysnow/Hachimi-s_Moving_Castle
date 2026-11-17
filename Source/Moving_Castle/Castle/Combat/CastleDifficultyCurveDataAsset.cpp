#include "Castle/Combat/CastleDifficultyCurveDataAsset.h"

bool UCastleDifficultyCurveDataAsset::ValidateConfiguration(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	const auto Reject = [this, &OutFailureReason](const FString& Field, const TCHAR* Reason)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("难度曲线 %s：%s：%s"), *GetPathName(), *Field, Reason));
		return false;
	};
	if (!FMath::IsFinite(CountGrowthPerOrdinal) || CountGrowthPerOrdinal < 0 || CountGrowthPerOrdinal > 10)
	{
		return Reject(TEXT("CountGrowthPerOrdinal"), TEXT("增长率必须是 0～10 的有限数。"));
	}
	const TArray<FCastleCombatBattleDefinition>* Tables[] = {&NormalBattles, &EliteBattles};
	const TCHAR* Names[] = {TEXT("NormalBattles"), TEXT("EliteBattles")};
	for (int32 Table = 0; Table < 2; ++Table)
	{
		if (Tables[Table]->IsEmpty()) { return Reject(FString(Names[Table]) + TEXT("[0]"), TEXT("至少需要一场战斗。")); }
		for (int32 Battle = 0; Battle < Tables[Table]->Num(); ++Battle)
		{
			const auto& Groups = (*Tables[Table])[Battle].Groups;
			const FString Path = FString::Printf(TEXT("%s[%d].Groups"), Names[Table], Battle);
			if (Groups.IsEmpty()) { return Reject(Path + TEXT("[0]"), TEXT("至少需要一个出场组。")); }
			for (int32 Group = 0; Group < Groups.Num(); ++Group)
			{
				const FString GroupPath = FString::Printf(TEXT("%s[%d]"), *Path, Group);
				if (Groups[Group].EnemyId.IsNone()) { return Reject(GroupPath + TEXT(".EnemyId"), TEXT("不能为 None。")); }
				if (Groups[Group].Count < 1) { return Reject(GroupPath + TEXT(".Count"), TEXT("数量必须大于零。")); }
				if (!FMath::IsFinite(Groups[Group].Delay) || Groups[Group].Delay < 0)
				{
					return Reject(GroupPath + TEXT(".Delay"), TEXT("延迟必须为有限非负数。"));
				}
			}
		}
	}
	return true;
}

bool UCastleDifficultyCurveDataAsset::BuildBattlePlan(const ECastleBattleKind Kind, const int32 KindOrdinal,
	TArray<FCastleCombatSpawnGroup>& OutGroups, FText& OutFailureReason) const
{
	OutGroups.Reset();
	if (!ValidateConfiguration(OutFailureReason)) { return false; }
	if ((Kind != ECastleBattleKind::Normal && Kind != ECastleBattleKind::Elite) || KindOrdinal < 1)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("难度曲线 %s：Kind/KindOrdinal 无效，序号必须从 1 开始。"), *GetPathName()));
		return false;
	}
	const auto& Table = Kind == ECastleBattleKind::Normal ? NormalBattles : EliteBattles;
	const int32 BaseIndex = KindOrdinal <= Table.Num() ? KindOrdinal - 1 : Table.Num() - 1;
	TArray<FCastleCombatSpawnGroup> Plan = Table[BaseIndex].Groups;
	if (KindOrdinal > Table.Num())
	{
		// 使用双精度中间值并在转 int32 前检查上界；线性外推只改数量，延迟保持原值。
		const double Scale = 1.0 + static_cast<double>(CountGrowthPerOrdinal) * (KindOrdinal - Table.Num());
		for (int32 Index = 0; Index < Plan.Num(); ++Index)
		{
			const double Count = FMath::CeilToDouble(static_cast<double>(Plan[Index].Count) * Scale);
			if (!FMath::IsFinite(Count) || Count > MAX_int32)
			{
				OutFailureReason = FText::FromString(FString::Printf(TEXT("难度曲线 %s：%s[%d].Groups[%d].Count 外推溢出，KindOrdinal=%d。"),
					*GetPathName(), Kind == ECastleBattleKind::Normal ? TEXT("NormalBattles") : TEXT("EliteBattles"), BaseIndex, Index, KindOrdinal));
				return false;
			}
			Plan[Index].Count = static_cast<int32>(Count);
		}
	}
	OutGroups = MoveTemp(Plan);
	return true;
}
