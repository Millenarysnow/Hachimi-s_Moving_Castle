#pragma once

#include "MassEntityConfigAsset.h"
#include "CastleMassUnitConfig.generated.h"

/** 内容浏览器可直接创建三种基础配置；具体兵种可用普通 MassEntityConfigAsset 的 Parent 继承它们。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleMassEnemyConfig : public UMassEntityConfigAsset
{
	GENERATED_BODY()
public:
	UCastleMassEnemyConfig();
};

UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleMassNPCConfig : public UMassEntityConfigAsset
{
	GENERATED_BODY()
public:
	UCastleMassNPCConfig();
};

UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleMassProjectileConfig : public UMassEntityConfigAsset
{
	GENERATED_BODY()
public:
	UCastleMassProjectileConfig();
};
