#pragma once

#include "CoreMinimal.h"
#include "Units/Mass/CastleMassTypes.h"
#include "CastleCombatTypes.generated.h"

class UMassEntityConfigAsset;

/** 普通与精英使用各自的波次表。 */
UENUM(BlueprintType)
enum class ECastleBattleKind : uint8
{
	Normal,
	Elite
};

namespace CastleCombat
{
	/** 导演节点与正式战斗共用精英关规则；调用前须校验配置中的精英关间隔。 */
	inline ECastleBattleKind GetBattleKindForStage(const int32 Stage, const int32 EliteInterval)
	{
		return Stage % EliteInterval == 0 ? ECastleBattleKind::Elite : ECastleBattleKind::Normal;
	}
}

/** 名册提供共享敌人模板和成簇方式；出场条目可以单独指定实例属性。 */
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

/** 一波的幽影群虫以圆环基准点为单位，名册负责把每点展开成一簇。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleBugCurveConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="群虫曲线")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="群虫曲线")
	FName BugEnemyId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="群虫曲线")
	int32 BugBase = 4;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="群虫曲线")
	float CurveK = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="群虫曲线")
	int32 BugMaxCap = 16;
	/** 首批立即出场，之后每隔此秒数生成下一批圆环基准点。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="群虫曲线", meta=(Units="s"))
	float SpawnInterval = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="群虫曲线")
	int32 BatchSize = 1;

	/** 已通过配置校验时计算本波总基准点数。 */
	int32 GetSpawnPointCount(int64 GlobalWaveIndex) const;
};

/** 一条独立定时的出场配置；普通突袭和精英条目使用相同字段。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleRaidSpawnEntry
{
	GENERATED_BODY()

	/** 生效的波次下限，波次从 1 开始。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="突袭")
	int32 MinWaveIndex = 1;
	/** 定时锚点是本波开始时刻，和虫群出场顺序无关。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="突袭", meta=(Units="s"))
	float TriggerDelay = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="突袭")
	FName EnemyId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="突袭")
	int32 Count = 1;
	/** 正数时每批一个基准点，零时本条的所有基准点同批生成。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="突袭", meta=(Units="s"))
	float SpawnInterval = 0.5f;
	/** 空标签明确表示本条无需解锁；其余标签在本场开战时判断。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="突袭")
	FName RequiredUnlockTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="突袭")
	FCastleMassEnemyAttributeOverrides AttributeOverrides;
};

/** 波内可同时安排曲线虫群及多条突袭，配置下一波的进入条件。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleCombatWaveDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="波次")
	FCastleBugCurveConfig BugCurve;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="波次")
	TArray<FCastleRaidSpawnEntry> RaidEntries;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="波次")
	bool bAdvanceOnClear = true;
	/** 定时模式的时间锚点是本波开始时刻。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="波次", meta=(Units="s", EditCondition="!bAdvanceOnClear"))
	float WaveDuration = 20.0f;
};

/** 一场战斗按数组顺序推进一个或多个波次。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleCombatBattleDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="战斗")
	TArray<FCastleCombatWaveDefinition> Waves;
};
