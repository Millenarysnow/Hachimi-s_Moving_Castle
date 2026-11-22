#include "Castle/Combat/CastleDifficultyCurveDataAsset.h"

int32 FCastleBugCurveConfig::GetSpawnPointCount(const int64 GlobalWaveIndex) const
{
	const double Raw = BugBase + CurveK * FMath::Loge(static_cast<double>(GlobalWaveIndex));
	// 先判断硬上限，避免系数很大时在 RoundToInt 之前就超出 int32；与公式等价。
	return Raw >= BugMaxCap ? BugMaxCap : FMath::RoundToInt(Raw);
}

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
			const auto& Waves = (*Tables[Table])[Battle].Waves;
			const FString BattlePath = FString::Printf(TEXT("%s[%d].Waves"), Names[Table], Battle);
			if (Waves.IsEmpty()) { return Reject(BattlePath + TEXT("[0]"), TEXT("一场战斗至少需要一波。")); }
			for (int32 WaveIndex = 0; WaveIndex < Waves.Num(); ++WaveIndex)
			{
				const auto& Wave = Waves[WaveIndex];
				const auto& Bug = Wave.BugCurve;
				const FString WavePath = FString::Printf(TEXT("%s[%d]"), *BattlePath, WaveIndex);
				if (!Wave.bAdvanceOnClear && (!FMath::IsFinite(Wave.WaveDuration) || Wave.WaveDuration <= 0.0f))
				{
					return Reject(WavePath + TEXT(".WaveDuration"), TEXT("定时推进需要有限的正时长。"));
				}
				if (!Bug.bEnabled && Wave.RaidEntries.IsEmpty())
				{
					return Reject(WavePath, TEXT("至少启用虫群或填写一个突袭条目。"));
				}
				if (Bug.bEnabled && Bug.BugEnemyId.IsNone())
				{
					return Reject(WavePath + TEXT(".BugCurve.BugEnemyId"), TEXT("启用的虫群曲线必须指定敌人。"));
				}
				if (Bug.BugBase < 1)
				{
					return Reject(WavePath + TEXT(".BugCurve.BugBase"), TEXT("基础点数至少为 1。"));
				}
				if (Bug.BugMaxCap < Bug.BugBase)
				{
					return Reject(WavePath + TEXT(".BugCurve.BugMaxCap"), TEXT("虫群上限不得小于基础点数。"));
				}
				if (!FMath::IsFinite(Bug.CurveK) || Bug.CurveK < 0.0f)
				{
					return Reject(WavePath + TEXT(".BugCurve.CurveK"), TEXT("曲线系数必须为有限非负数。"));
				}
				if (!FMath::IsFinite(Bug.SpawnInterval) || Bug.SpawnInterval < 0.0f)
				{
					return Reject(WavePath + TEXT(".BugCurve.SpawnInterval"), TEXT("间隔必须为有限非负数。"));
				}
				if (Bug.BatchSize < 1)
				{
					return Reject(WavePath + TEXT(".BugCurve.BatchSize"), TEXT("每批至少一个基准点。"));
				}
				for (int32 RaidIndex = 0; RaidIndex < Wave.RaidEntries.Num(); ++RaidIndex)
				{
					// 门槛只决定本场是否出场；尚未解锁的条目也必须通过静态校验。
					const auto& Raid = Wave.RaidEntries[RaidIndex];
					const FString RaidPath = FString::Printf(TEXT("%s.RaidEntries[%d]"), *WavePath, RaidIndex);
					if (Raid.MinWaveIndex < 1) { return Reject(RaidPath + TEXT(".MinWaveIndex"), TEXT("生效波次至少为 1。")); }
					if (Raid.EnemyId.IsNone()) { return Reject(RaidPath + TEXT(".EnemyId"), TEXT("不能为 None。")); }
					if (Raid.Count < 1) { return Reject(RaidPath + TEXT(".Count"), TEXT("数量必须大于零。")); }
					if (!FMath::IsFinite(Raid.TriggerDelay) || Raid.TriggerDelay < 0.0f)
					{
						return Reject(RaidPath + TEXT(".TriggerDelay"), TEXT("触发延迟必须为有限非负数。"));
					}
					if (!FMath::IsFinite(Raid.SpawnInterval) || Raid.SpawnInterval < 0.0f)
					{
						return Reject(RaidPath + TEXT(".SpawnInterval"), TEXT("间隔必须为有限非负数。"));
					}
					if (const TCHAR* Field = Raid.AttributeOverrides.GetInvalidField())
					{
						return Reject(RaidPath + TEXT(".AttributeOverrides.") + Field, TEXT("覆写超出合法范围或血量设置互斥。"));
					}
				}
			}
		}
	}
	return true;
}

bool UCastleDifficultyCurveDataAsset::BuildBattlePlan(const ECastleBattleKind Kind, const int32 KindOrdinal,
	TArray<FCastleCombatWaveDefinition>& OutWaves, FText& OutFailureReason) const
{
	OutWaves.Reset();
	if (!ValidateConfiguration(OutFailureReason)) { return false; }
	if ((Kind != ECastleBattleKind::Normal && Kind != ECastleBattleKind::Elite) || KindOrdinal < 1)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("难度曲线 %s：Kind/KindOrdinal 无效，序号必须从 1 开始。"), *GetPathName()));
		return false;
	}
	const auto& Table = Kind == ECastleBattleKind::Normal ? NormalBattles : EliteBattles;
	const int32 BaseIndex = KindOrdinal <= Table.Num() ? KindOrdinal - 1 : Table.Num() - 1;
	TArray<FCastleCombatWaveDefinition> Plan = Table[BaseIndex].Waves;
	if (KindOrdinal > Table.Num())
	{
		// 始终从末场原值外推突袭数量；虫群另外按连续波次曲线计算，时间与覆写从原表复制。
		const double Scale = 1.0 + static_cast<double>(CountGrowthPerOrdinal) * (KindOrdinal - Table.Num());
		for (int32 WaveIndex = 0; WaveIndex < Plan.Num(); ++WaveIndex)
		{
			auto& Raids = Plan[WaveIndex].RaidEntries;
			for (int32 RaidIndex = 0; RaidIndex < Raids.Num(); ++RaidIndex)
			{
				const double Count = FMath::CeilToDouble(static_cast<double>(Raids[RaidIndex].Count) * Scale);
				if (!FMath::IsFinite(Count) || Count > MAX_int32)
				{
					OutFailureReason = FText::FromString(FString::Printf(
						TEXT("难度曲线 %s：%s[%d].Waves[%d].RaidEntries[%d].Count 外推溢出，KindOrdinal=%d。"),
						*GetPathName(), Kind == ECastleBattleKind::Normal ? TEXT("NormalBattles") : TEXT("EliteBattles"),
						BaseIndex, WaveIndex, RaidIndex, KindOrdinal));
					return false;
				}
				Raids[RaidIndex].Count = static_cast<int32>(Count);
			}
		}
	}
	OutWaves = MoveTemp(Plan);
	return true;
}
