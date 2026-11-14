#include "CastleMassEnemyRangedAttackTrait.h"

#include "CastleMassEnemyRangedAttackFragments.h"
#include "CastleMassFragments.h"
#include "CastleMassMeleeAttackFragments.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityTemplateRegistry.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"
#include "Projectiles/CastleMassEnemyProjectileTrait.h"

void UCastleMassEnemyRangedAttackTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.RequireTag<FCastleMassEnemyTag>();
	BuildContext.RequireFragment<FTransformFragment>();
	BuildContext.RequireFragment<FMassVelocityFragment>();
	BuildContext.RequireFragment<FCastleMassMoveTargetFragment>();
	BuildContext.RequireFragment<FCastleMassUnitFragment>();
	auto& State = BuildContext.AddFragment_GetRef<FCastleMassEnemyRangedAttackFragment>();
	State.AttackDamage = AttackDamage;
	State.AttackInterval = AttackInterval;
	State.AttackRange = AttackRange;
	State.ProjectileConfiguration = ProjectileConfiguration.Get();
}

bool UCastleMassEnemyRangedAttackTrait::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext,
	const UWorld& World, FAdditionalTraitRequirements& OutTraitRequirements) const
{
	// 两种攻击处理器都写移动目标，必须在装配阶段阻止一个实体拥有两个战斗位移决策者。
	const bool bValid = Super::ValidateTemplate(BuildContext, World, OutTraitRequirements)
		&& !BuildContext.HasFragment<FCastleMassMeleeAttackFragment>()
		&& FMath::IsFinite(AttackDamage) && AttackDamage >= 0.0f
		&& FMath::IsFinite(AttackInterval) && AttackInterval > 0.0f
		&& FMath::IsFinite(AttackRange) && AttackRange > 0.0f
		&& IsValid(ProjectileConfiguration) && ProjectileConfiguration->FindTrait(UCastleMassEnemyProjectileTrait::StaticClass());
	if (!bValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("敌方远程配置无效：%s，投射物 %s；需有限伤害、正间隔/射程及敌方追踪弹配置，且不能同时装配近战。"), *GetPathName(), *GetPathNameSafe(ProjectileConfiguration));
	}
	return bValid;
}
