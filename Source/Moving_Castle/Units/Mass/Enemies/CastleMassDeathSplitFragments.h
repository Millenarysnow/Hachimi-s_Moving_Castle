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
	UPROPERTY() bool bHasSplit = false;
};
