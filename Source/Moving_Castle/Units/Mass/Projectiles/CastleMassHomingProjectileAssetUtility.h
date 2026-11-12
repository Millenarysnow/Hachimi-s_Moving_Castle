#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "CastleMassHomingProjectileAssetUtility.generated.h"

class UMassEntityConfigAsset;

/** 追踪子弹配置资产的编辑器辅助操作；仅修改传入子资产的本地 Trait。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassHomingProjectileAssetUtility : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	/**
	 * 在继承 DA_MassProjectileBase 的派生资产中停用本地精确类 UCastleMassProjectileTrait。
	 * 派生 Trait 自己建立完整子弹组成；显式停用本地基类模板可避免重复 Fragment 和移动共享参数。
	 * 未找到本地覆盖或收到无效配置时写 Error 并返回 false，不会追溯修改父资产。
	 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass|追踪子弹", meta=(DevelopmentOnly))
	static bool DisableLocalBaseProjectileTrait(UMassEntityConfigAsset* DerivedConfiguration);
#endif
};
