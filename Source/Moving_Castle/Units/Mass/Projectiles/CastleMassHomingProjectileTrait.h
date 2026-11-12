#pragma once

#include "CastleMassTraits.h"
#include "CastleMassHomingProjectileTrait.generated.h"

/**
 * 子弹基类的追踪子类，拥有完整的单位/子弹组成及独立锁定目标状态。
 * 速度取本 Trait 的 Movement.MaxSpeed，伤害必须由 SpawnHomingProjectiles 逐发提供。
 * 继承基类配置资产时，须在子资产显式停用同类覆盖的基类 Trait，不能同时装配两个位移配置。
 */
UCLASS(meta=(DisplayName="Castle Mass 追踪子弹"))
class MOVING_CASTLE_API UCastleMassHomingProjectileTrait : public UCastleMassProjectileTrait
{
	GENERATED_BODY()

protected:
	/** 复用基类组成，只新增锁定目标 Fragment，不改变普通子弹的模板或运动语义。 */
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

	/** 追踪弹每帧直接朝目标转向，不能同时配置重力弹道。 */
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
		FAdditionalTraitRequirements& OutTraitRequirements) const override;
};
