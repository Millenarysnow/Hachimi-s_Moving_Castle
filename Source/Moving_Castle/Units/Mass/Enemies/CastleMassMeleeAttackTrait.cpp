#include "CastleMassMeleeAttackTrait.h"

#include "CastleMassFragments.h"
#include "CastleMassMeleeAttackFragments.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"

void UCastleMassMeleeAttackTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// 能力不重复创建通用 Fragment，Mass 会校验这些依赖确实由敌人基础 Trait 提供。
	BuildContext.RequireTag<FCastleMassEnemyTag>();
	BuildContext.RequireFragment<FTransformFragment>();
	BuildContext.RequireFragment<FMassVelocityFragment>();
	BuildContext.RequireFragment<FCastleMassMoveTargetFragment>();
	BuildContext.RequireFragment<FCastleMassUnitFragment>();
	FCastleMassMeleeAttackFragment& Melee = BuildContext.AddFragment_GetRef<FCastleMassMeleeAttackFragment>();
	Melee.AttackDamage = AttackDamage;
	Melee.AttackInterval = AttackInterval;
	Melee.AttackRange = AttackRange;
}

bool UCastleMassMeleeAttackTrait::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
	FAdditionalTraitRequirements& OutTraitRequirements) const
{
	const bool bValid = Super::ValidateTemplate(BuildContext, World, OutTraitRequirements)
		&& FMath::IsFinite(AttackDamage) && AttackDamage >= 0.0f
		&& FMath::IsFinite(AttackInterval) && AttackInterval > 0.0f
		&& FMath::IsFinite(AttackRange) && AttackRange >= 0.0f;
	if (!bValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 近战配置无效：%s，伤害必须非负，间隔必须为正，范围必须非负。"), *GetPathName());
	}
	return bValid;
}
