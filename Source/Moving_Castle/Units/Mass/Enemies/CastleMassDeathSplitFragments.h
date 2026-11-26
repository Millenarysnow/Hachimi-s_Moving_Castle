#pragma once

#include "MassEntityTypes.h"
#include "CastleMassDeathSplitFragments.generated.h"

class UMassEntityConfigAsset;

/** 死亡分裂是一次性的生命结算能力；标记与生命同属实体，不通过 Actor 的销毁回调实现。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassDeathSplitFragment : public FMassFragment
{
	GENERATED_BODY()
	/** Trait 及单位子系统保有配置强引用；实体只存可直接复制的弱引用。 */
	UPROPERTY() TWeakObjectPtr<UMassEntityConfigAsset> ChildConfiguration;
	UPROPERTY() int32 SpawnCount = 3;
	UPROPERTY() float SpawnSpreadRadius = 80.0f;
	UPROPERTY() float BurstDuration = 0.35f;
	UPROPERTY() float BurstArcHeight = 60.0f;
	UPROPERTY() bool bHasSplit = false;
};

/** 子单位爆散期间由专用处理器拥有 Transform；落点后处理器移除该 Fragment。 */
USTRUCT()
struct MOVING_CASTLE_API FCastleMassSpawnBurstFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY() FVector Start = FVector::ZeroVector;
	UPROPERTY() FVector Target = FVector::ZeroVector;
	/** 抛物线弧高方向；贴墙分裂时沿塔面外法线，普通分裂时为世界上方向。 */
	UPROPERTY() FVector ArcDirection = FVector::UpVector;
	UPROPERTY() float Elapsed = 0.0f;
	UPROPERTY() float Duration = 0.35f;
	UPROPERTY() float ArcHeight = 60.0f;
};
