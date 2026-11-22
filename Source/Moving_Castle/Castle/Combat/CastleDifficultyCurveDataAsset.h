#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Castle/Combat/CastleCombatTypes.h"
#include "CastleDifficultyCurveDataAsset.generated.h"

/** 普通和精英分别配置关内波次，表外复用末场结构并线性外推突袭数量。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleDifficultyCurveDataAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="难度曲线")
	TArray<FCastleCombatBattleDefinition> NormalBattles;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="难度曲线")
	TArray<FCastleCombatBattleDefinition> EliteBattles;
	/** 每超出表长一场，在末项突袭原数量上累加的倍率，合法范围 0～10。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="难度曲线")
	float CountGrowthPerOrdinal = 0.25f;

	/** 校验曲线自身结构；名册交叉引用由战斗配置统一校验。失败原因含资产及数组下标。 */
	bool ValidateConfiguration(FText& OutFailureReason) const;
	/** 复制关内所有波次并完成表外外推，成功后才输出。 */
	bool BuildBattlePlan(ECastleBattleKind Kind, int32 KindOrdinal,
		TArray<FCastleCombatWaveDefinition>& OutWaves, FText& OutFailureReason) const;
};
