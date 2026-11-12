#include "CastleMassHomingProjectileTrait.h"

#include "CastleMassHomingProjectileFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "Moving_Castle.h"

void UCastleMassHomingProjectileTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	Super::BuildTemplate(BuildContext, World);
	BuildContext.AddFragment<FCastleMassHomingProjectileFragment>();
}

bool UCastleMassHomingProjectileTrait::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
	FAdditionalTraitRequirements& OutTraitRequirements) const
{
	const bool bBaseValid = Super::ValidateTemplate(BuildContext, World, OutTraitRequirements);
	if (Gravity != 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 追踪子弹配置无效：%s，Gravity 必须为 0；追踪能力不叠加重力弹道。"), *GetPathName());
		return false;
	}
	return bBaseValid;
}
