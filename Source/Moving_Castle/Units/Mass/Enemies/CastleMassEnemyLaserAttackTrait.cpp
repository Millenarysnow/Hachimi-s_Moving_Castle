#include "CastleMassEnemyLaserAttackTrait.h"

#include "CastleMassDeathSplitFragments.h"
#include "CastleMassEnemyLaserAttackFragments.h"
#include "CastleMassFragments.h"
#include "CastleMassMeleeAttackFragments.h"
#include "CastleMassEnemyRangedAttackFragments.h"
#include "CastleMassTraits.h"
#include "Engine/StaticMesh.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassMovementFragments.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"

void UCastleMassEnemyLaserAttackTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.RequireTag<FCastleMassEnemyTag>();
	BuildContext.RequireFragment<FTransformFragment>();
	BuildContext.RequireFragment<FMassVelocityFragment>();
	BuildContext.RequireFragment<FCastleMassMoveTargetFragment>();
	BuildContext.RequireFragment<FCastleMassUnitFragment>();
	FCastleMassEnemyLaserAttackFragment& Attack = BuildContext.AddFragment_GetRef<FCastleMassEnemyLaserAttackFragment>();
	Attack.AttackDamage = AttackDamage;
	Attack.AttackInterval = AttackInterval;
	Attack.AttackRange = AttackRange;
	Attack.MuzzleLocalOffset = MuzzleLocalOffset;
	Attack.BeamThickness = BeamThickness;
	Attack.BeamMesh = BeamMesh;
	Attack.BeamMaterial = BeamMaterial;
}

bool UCastleMassEnemyLaserAttackTrait::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
	FAdditionalTraitRequirements& OutTraitRequirements) const
{
	const bool bValid = Super::ValidateTemplate(BuildContext, World, OutTraitRequirements)
		&& BuildContext.HasTag<FCastleMassEnemyTag>()
		&& !BuildContext.HasFragment<FCastleMassMeleeAttackFragment>()
		&& !BuildContext.HasFragment<FCastleMassEnemyRangedAttackFragment>()
		&& FMath::IsFinite(AttackDamage) && AttackDamage >= 0.0f
		&& FMath::IsFinite(AttackInterval) && AttackInterval > 0.0f
		&& FMath::IsFinite(AttackRange) && AttackRange > 0.0f
		&& !MuzzleLocalOffset.ContainsNaN()
		&& FMath::IsFinite(BeamThickness) && BeamThickness > 0.0f
		&& IsValid(BeamMesh) && IsValid(BeamMaterial);
	if (!bValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("敌方激光配置无效：%s；需敌人身份、无其他攻击 Trait、有限正间隔/射程/粗细、有限偏移及有效光束网格和材质。"), *GetPathName());
	}
	return bValid;
}
