#pragma once

#include "CoreMinimal.h"
#include "CastleCombatTypes.generated.h"

class UMassEntityConfigAsset;

/** 普通与精英各自独立编号，序号从 1 开始。 */
UENUM(BlueprintType)
enum class ECastleBattleKind : uint8
{
	Normal,
	Elite
};

/** 名册只描述敌人身份与成簇方式，生命、攻击仍完全归 Mass 配置所有。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleCombatEnemyEntry
{
	GENERATED_BODY()

	/** 出场表引用的唯一标识，不能为 None 或重复。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗")
	FName EnemyId;
	/** 必须包含 UCastleMassEnemyTrait 的 Mass 配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗")
	TObjectPtr<UMassEntityConfigAsset> Configuration;
	/** 每个圆环基准点的实体数量范围；单体敌人显式配为 1～1。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗")
	int32 MinClusterCount = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗")
	int32 MaxClusterCount = 1;
	/** 水平簇半径；多实体簇必须为有限正数，单体允许零。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗", meta=(Units="cm"))
	float ClusterSpreadRadius = 0.0f;
};

/** 一组敌人的圆环基准点数与相对开战的游戏时间，Count 不是群虫实体数。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleCombatSpawnGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗")
	FName EnemyId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗")
	int32 Count = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗", meta=(Units="s"))
	float Delay = 0.0f;
};

/** 一场战斗可以按任意数组顺序配置若干组；调度由各组 Delay 决定。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleCombatBattleDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗")
	TArray<FCastleCombatSpawnGroup> Groups;
};
