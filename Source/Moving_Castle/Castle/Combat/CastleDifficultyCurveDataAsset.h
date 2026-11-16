#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Castle/Combat/CastleCombatTypes.h"
#include "CastleDifficultyCurveDataAsset.generated.h"

/** 逐场手调数量和出场时刻；表外仅线性增加数量，不缩放生命或攻击。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleDifficultyCurveDataAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="难度曲线")
	TArray<FCastleCombatBattleDefinition> NormalBattles;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="难度曲线")
	TArray<FCastleCombatBattleDefinition> EliteBattles;
	/** 每超出表长一场，在末项原数量上累加的倍率，合法范围 0～10。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="难度曲线")
	float CountGrowthPerOrdinal = 0.25f;

	/** 校验曲线自身结构；名册交叉引用由战斗配置统一校验。失败原因含资产及数组下标。 */
	bool ValidateConfiguration(FText& OutFailureReason) const;
	/** 整体计算到临时数组后才输出；溢出直接拒绝，绝不夹紧或生成半份计划。 */
	bool BuildBattlePlan(ECastleBattleKind Kind, int32 KindOrdinal,
		TArray<FCastleCombatSpawnGroup>& OutGroups, FText& OutFailureReason) const;
};
