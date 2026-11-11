#include "CastleMassTraits.h"

#include "Animation/AnimSequenceTransformProviderData.h"
#include "Animation/Skeleton.h"
#include "CastleMassFragments.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassEntityUtils.h"
#include "MassMovementFragments.h"
#include "MassMovementTypes.h"
#include "Moving_Castle.h"
#include "Engine/World.h"
#include "Rendering/SkeletalMeshRenderData.h"

void UCastleMassUnitTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddTag<FCastleMassUnitTag>();
	BuildContext.AddTag<FMassCustomMovementTag>();
	BuildContext.AddFragment<FTransformFragment>();
	BuildContext.AddFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FMassActorFragment>();
	BuildContext.AddFragment<FCastleMassMoveTargetFragment>();
	BuildContext.AddFragment_GetRef<FAgentRadiusFragment>().Radius = FMath::Max(0.0f, Radius);

	FCastleMassUnitFragment& Unit = BuildContext.AddFragment_GetRef<FCastleMassUnitFragment>();
	Unit.Kind = GetKind();
	Unit.Team = Team;
	Unit.Health = MaxHealth;
	Unit.Lifetime = Lifetime;

	FCastleMassMovementParameters Parameters;
	Parameters.Settings = Movement;
	BuildContext.AddConstSharedFragment(UE::Mass::Utils::GetEntityManagerChecked(World).GetOrCreateConstSharedFragment(Parameters));

	switch (GetKind())
	{
	case ECastleMassUnitKind::Enemy:
		BuildContext.AddTag<FCastleMassEnemyTag>();
		BuildContext.AddTag<FCastleMassHumanoidTag>();
		break;
	case ECastleMassUnitKind::NPC:
		BuildContext.AddTag<FCastleMassNPCTag>();
		BuildContext.AddTag<FCastleMassHumanoidTag>();
		BuildContext.AddFragment<FCastleMassPatrolFragment>();
		break;
	case ECastleMassUnitKind::Projectile:
		BuildContext.AddTag<FCastleMassProjectileTag>();
		break;
	}
}

bool UCastleMassUnitTrait::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
	FAdditionalTraitRequirements& OutTraitRequirements) const
{
	const bool bValid = Super::ValidateTemplate(BuildContext, World, OutTraitRequirements)
		&& FMath::IsFinite(MaxHealth) && MaxHealth > 0.0f && FMath::IsFinite(Lifetime) && Lifetime >= 0.0f
		&& FMath::IsFinite(Radius) && Radius >= 0.0f
		&& FMath::IsFinite(Movement.MaxSpeed) && Movement.MaxSpeed >= 0.0f
		&& FMath::IsFinite(Movement.Acceleration) && Movement.Acceleration >= 0.0f
		&& FMath::IsFinite(Movement.AcceptanceRadius) && Movement.AcceptanceRadius >= 0.0f
		&& FMath::IsFinite(Movement.TurnRate) && Movement.TurnRate >= 0.0f;
	if (!bValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 单位配置无效：%s，请检查生命、寿命、半径和移动参数。"), *GetPathName());
	}
	return bValid;
}

UCastleMassEnemyTrait::UCastleMassEnemyTrait()
{
	Team = 1;
	Movement.MaxSpeed = 350.0f;
}

UCastleMassNPCTrait::UCastleMassNPCTrait()
{
	Movement.MaxSpeed = 200.0f;
}

UCastleMassProjectileTrait::UCastleMassProjectileTrait()
{
	MaxHealth = 1.0f;
	Radius = 8.0f;
	Lifetime = 5.0f;
	Movement.MaxSpeed = 1800.0f;
	Movement.Acceleration = 0.0f;
	Movement.bKeepUpright = false;
}

void UCastleMassProjectileTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	Super::BuildTemplate(BuildContext, World);
	FCastleMassProjectileFragment& Projectile = BuildContext.AddFragment_GetRef<FCastleMassProjectileFragment>();
	Projectile.Gravity = FMath::Max(0.0f, Gravity);
	Projectile.Damage = FMath::Max(0.0f, Damage);
}

bool UCastleMassProjectileTrait::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
	FAdditionalTraitRequirements& OutTraitRequirements) const
{
	const bool bValid = Super::ValidateTemplate(BuildContext, World, OutTraitRequirements)
		&& FMath::IsFinite(Gravity) && Gravity >= 0.0f && FMath::IsFinite(Damage) && Damage >= 0.0f;
	if (!bValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 子弹配置无效：%s，重力和伤害必须为有限非负数。"), *GetPathName());
	}
	return bValid;
}

UCastleMassHumanoidVisualizationTrait::UCastleMassHumanoidVisualizationTrait()
{
	// 所有可见 LOD 都不生成逐单位 Actor；骨骼网格自身负责渲染 LOD。
	for (int32 LOD = 0; LOD < EMassLOD::Max; ++LOD)
	{
		Params.LODRepresentation[LOD] = LOD == EMassLOD::Off ? EMassRepresentationType::None : EMassRepresentationType::SkinnedMeshInstance;
		LODParams.LODMaxCount[LOD] = MAX_int32;
	}
	Params.bKeepLowResActors = false;
	bRegisterStaticMeshDesc = false;
#if WITH_EDITORONLY_DATA
	bRequireValidStaticMeshInstanceDesc = false;
#endif
}

void UCastleMassHumanoidVisualizationTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	if (World.IsNetMode(NM_DedicatedServer) && !bAllowServerSideVisualization && !BuildContext.IsInspectingData())
	{
		return;
	}

	SkinnedMeshInstanceDesc.Reset();
	if (AnimationProvider)
	{
		FMassSkinnedMeshInstanceVisualizationMeshDesc& Mesh = SkinnedMeshInstanceDesc.Meshes.AddDefaulted_GetRef();
		Mesh.Asset = AnimationProvider->GetSkinnedAsset();
		Mesh.TransformProvider = AnimationProvider;
		Mesh.LocalTransform = MeshOffset;
		Mesh.bCastShadows = bCastShadows;
		Mesh.AnimationMinScreenSize = AnimationMinScreenSize;
	}
	Super::BuildTemplate(BuildContext, World);

	BuildContext.RequireTag<FCastleMassHumanoidTag>();
	BuildContext.RequireFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FMassRepresentationAnimationFragment>();
	BuildContext.AddFragment<FCastleMassAnimationFragment>();
	FCastleMassAnimationParameters Parameters;
	Parameters.IdleSequence = IdleSequence;
	Parameters.MoveSequence = MoveSequence;
	Parameters.MovePlayRate = MovePlayRate;
	Parameters.BlendTime = BlendTime;
	if (AnimationProvider && AnimationProvider->GetSequences().IsValidIndex(IdleSequence)
		&& AnimationProvider->GetSequences().IsValidIndex(MoveSequence))
	{
		Parameters.IdleLength = AnimationProvider->GetSequencePlayLength(IdleSequence);
		Parameters.MoveLength = AnimationProvider->GetSequencePlayLength(MoveSequence);
	}
	BuildContext.AddConstSharedFragment(UE::Mass::Utils::GetEntityManagerChecked(World).GetOrCreateConstSharedFragment(Parameters));
}

bool UCastleMassHumanoidVisualizationTrait::ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
	FAdditionalTraitRequirements& OutTraitRequirements) const
{
	bool bValid = Super::ValidateTemplate(BuildContext, World, OutTraitRequirements)
		&& AnimationProvider && AnimationProvider->GetSkinnedAsset() && AnimationProvider->IsEnabled()
		&& AnimationProvider->GetSequences().IsValidIndex(IdleSequence)
		&& AnimationProvider->GetSequences().IsValidIndex(MoveSequence)
		&& FMath::IsFinite(MovePlayRate) && MovePlayRate > 0.0f
		&& FMath::IsFinite(BlendTime) && BlendTime >= 0.0f && !MeshOffset.ContainsNaN()
		&& FMath::IsFinite(AnimationMinScreenSize);
	if (bValid)
	{
		const USkinnedAsset* Asset = AnimationProvider->GetSkinnedAsset();
		const FSkeletalMeshRenderData* RenderData = Asset->GetResourceForRendering();
		if (RenderData && !RenderData->HasUnifiedBoneMap())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("Mass 骨骼网格 %s 需要开启 Optimize for Instancing 并重新构建。"), *Asset->GetPathName());
			bValid = false;
		}
		for (const auto& Entry : AnimationProvider->GetSequences())
		{
			if (!Entry.Sequence || !Entry.Sequence->GetSkeleton() || !Entry.Sequence->GetSkeleton()->IsCompatibleMesh(Asset))
			{
				bValid = false;
				break;
			}
		}
	}
	if (!bValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 人形动画配置无效：%s，需要骨骼网格匹配的动画提供器和有效待机/移动序列。"), *GetPathName());
	}
	return bValid;
}
