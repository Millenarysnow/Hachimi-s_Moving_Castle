#pragma once

#include "CastleMassTraits.h"
#include "CastleMassHomingProjectileTrait.generated.h"

/**
 * 子弹基类的追踪子类，拥有完整的单位/子弹组成及独立锁定目标状态。
 * 速度取本 Trait 的 Movement.MaxSpeed，伤害必须由 SpawnHomingProjectiles 逐发提供。
 * Mass 配置按精确 Trait 类及有效目标标志的交集合并，不按 C++ 继承关系替换。
 * 追踪子弹资产不要继承含 UCastleMassProjectileTrait 的配置，避免同时构建两个子弹 Unit Trait。
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
