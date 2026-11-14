#include "CastleMassEnemyProjectileTrait.h"

#include "CastleMassEnemyProjectileFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "Moving_Castle.h"

void UCastleMassEnemyProjectileTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	Super::BuildTemplate(BuildContext, World);
	BuildContext.AddFragment<FCastleMassEnemyProjectileFragment>();
}

bool UCastleMassEnemyProjectileTrait::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext,
	const UWorld& World, FAdditionalTraitRequirements& OutTraitRequirements) const
{
	const bool bValid = Super::ValidateTemplate(BuildContext, World, OutTraitRequirements) && Gravity == 0.0f && Movement.MaxSpeed > 0.0f;
	if (!bValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("敌方追踪弹配置无效：%s，必须无重力且速度为有限正数。"), *GetPathName());
	}
	return bValid;
}
