#include "CastleMassHomingProjectileAssetUtility.h"

#if WITH_EDITOR

#include "CastleMassTraits.h"
#include "MassEntityConfigAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogCastleMassHomingProjectileAsset, Log, All);

bool UCastleMassHomingProjectileAssetUtility::DisableLocalBaseProjectileTrait(UMassEntityConfigAsset* DerivedConfiguration)
{
	if (!IsValid(DerivedConfiguration))
	{
		UE_LOG(LogCastleMassHomingProjectileAsset, Error, TEXT("无法配置追踪箭矢：收到的 Mass 配置资产无效。"));
		return false;
	}

	// 只检查当前子资产的本地 Trait 列表；不能用 FindTrait 沿父链搜索后误改 DA_MassProjectileBase。
	for (UMassEntityTraitBase* Trait : DerivedConfiguration->GetMutableConfig().GetTraits())
	{
		if (Trait != nullptr && Trait->GetClass() == UCastleMassProjectileTrait::StaticClass())
		{
			DerivedConfiguration->Modify();
			Trait->Modify();
			Trait->ValidTargetConfig = EProcessorExecutionFlags::None;
			Trait->PostEditChange();
			DerivedConfiguration->MarkPackageDirty();
			return true;
		}
	}

	UE_LOG(LogCastleMassHomingProjectileAsset, Error,
		TEXT("无法配置 Mass 资产 %s：追踪子弹必须先在子资产中添加精确类 CastleMassProjectileTrait，再显式停用其模板。"),
		*DerivedConfiguration->GetPathName());
	return false;
}

#endif
