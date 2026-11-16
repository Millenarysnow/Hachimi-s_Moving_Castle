#include "CastleMassDeathSplitTrait.h"

#include "CastleMassDeathSplitFragments.h"
#include "CastleMassFragments.h"
#include "CastleMassTraits.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityTemplateRegistry.h"
#include "Moving_Castle.h"

void UCastleMassDeathSplitTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.RequireTag<FCastleMassEnemyTag>();
	BuildContext.RequireFragment<FCastleMassUnitFragment>();
	BuildContext.RequireFragment<FTransformFragment>();
	auto& State = BuildContext.AddFragment_GetRef<FCastleMassDeathSplitFragment>();
	State.ChildConfiguration = ChildConfiguration.Get();
	State.SpawnCount = SpawnCount;
	State.SpawnSpreadRadius = SpawnSpreadRadius;
	State.BurstDuration = BurstDuration;
	State.BurstArcHeight = BurstArcHeight;
}

bool UCastleMassDeathSplitTrait::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext,
	const UWorld& World, FAdditionalTraitRequirements& OutTraitRequirements) const
{
	const bool bValid = Super::ValidateTemplate(BuildContext, World, OutTraitRequirements)
		&& IsValid(ChildConfiguration) && ChildConfiguration->FindTrait(UCastleMassEnemyTrait::StaticClass())
		&& SpawnCount >= 1 && SpawnCount <= 64
		&& FMath::IsFinite(SpawnSpreadRadius) && SpawnSpreadRadius > 0.0f && SpawnSpreadRadius <= 5000.0f
		&& FMath::IsFinite(BurstDuration) && BurstDuration > 0.0f && BurstDuration <= 5.0f
		&& FMath::IsFinite(BurstArcHeight) && BurstArcHeight >= 0.0f && BurstArcHeight <= 1000.0f;
	if (!bValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("死亡分裂配置无效：%s，子配置 %s 必须是敌人配置，数量须在 [1,64]、半径在 (0,5000] cm、时长在 (0,5] s、弧高在 [0,1000] cm。"), *GetPathName(), *GetPathNameSafe(ChildConfiguration));
	}
	return bValid;
}
