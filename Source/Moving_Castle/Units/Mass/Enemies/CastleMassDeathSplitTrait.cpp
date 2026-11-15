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
}

bool UCastleMassDeathSplitTrait::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext,
	const UWorld& World, FAdditionalTraitRequirements& OutTraitRequirements) const
{
	const bool bValid = Super::ValidateTemplate(BuildContext, World, OutTraitRequirements)
		&& IsValid(ChildConfiguration) && ChildConfiguration->FindTrait(UCastleMassEnemyTrait::StaticClass())
		&& SpawnCount > 0 && FMath::IsFinite(SpawnSpreadRadius) && SpawnSpreadRadius >= 0.0f;
	if (!bValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("死亡分裂配置无效：%s，子配置 %s 必须是敌人配置，数量为正且散布半径有限非负。"), *GetPathName(), *GetPathNameSafe(ChildConfiguration));
	}
	return bValid;
}
