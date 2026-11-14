#pragma once

#include "CastleMassTraits.h"
#include "CastleMassEnemyProjectileTrait.generated.h"

/** 完整的敌方追踪弹组成；配置无 Parent，避免同时继承普通子弹 Unit Trait。 */
UCLASS(meta=(DisplayName="Castle Mass 敌方追踪弹"))
class MOVING_CASTLE_API UCastleMassEnemyProjectileTrait : public UCastleMassProjectileTrait
{
	GENERATED_BODY()
protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
		FAdditionalTraitRequirements& OutTraitRequirements) const override;
};
