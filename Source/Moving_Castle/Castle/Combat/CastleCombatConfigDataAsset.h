#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Castle/Combat/CastleCombatTypes.h"
#include "CastleCombatConfigDataAsset.generated.h"

class UCastleDifficultyCurveDataAsset;

/** 正式战斗入口配置，与旧的主塔生命/开局测试兵 DA 分开维护。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleCombatConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗")
	TArray<FCastleCombatEnemyEntry> Roster;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗")
	TObjectPtr<UCastleDifficultyCurveDataAsset> DifficultyCurve;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗|出生", meta=(Units="cm"))
	float SpawnInnerRadius = 1800.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗|出生", meta=(Units="cm"))
	float SpawnOuterRadius = 2300.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗|出生", meta=(Units="cm"))
	float SpawnHeightOffset = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗|出生")
	int32 SpawnSeed = 20261116;

	/** 校验整份名册、两张曲线表及交叉引用；失败写清资产、字段和具体数组下标。 */
	UFUNCTION(BlueprintPure, Category="移动城堡|战斗")
	bool ValidateConfiguration(FText& OutFailureReason) const;
};
