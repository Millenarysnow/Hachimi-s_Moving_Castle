#include "CastleMassTraits.h"

#include "Animation/AnimSequenceTransformProviderData.h"
#include "Animation/Skeleton.h"
#include "CastleMassFragments.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Units/BuildingInteraction/Mass/CastleMassBuildingTraversalFragment.h"
#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassEntityUtils.h"
#include "MassMovementFragments.h"
#include "MassMovementTypes.h"
#include "Moving_Castle.h"
#include "Engine/World.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkeletalMesh.h"

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
		BuildContext.AddFragment<FCastleMassNPCFarmBuffFragment>();
		BuildContext.AddTag<FCastleMassHumanoidTag>();
		BuildContext.AddFragment<FCastleMassPatrolFragment>();
		// 所有 NPC 具备进入触发所需的值状态；未启用共享巡逻时不会凭终点位置触发建筑。
		BuildContext.AddFragment<FCastleMassBuildingTraversalFragment>();
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
	// 基类只设置敌人通用默认值；生命 10 等测试平衡数值由具体兵种 DA 覆盖。
	Movement.MaxSpeed = 350.0f;
}

void UCastleMassEnemyTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	Super::BuildTemplate(BuildContext, World);
	// 攻击处理器只提交目标与射程意图，位置和速度由统一飞行解算器负责。
	BuildContext.AddFragment<FCastleMassFlightIntentFragment>();
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
		Mesh.MaterialOverrides = MaterialOverrides;
		Mesh.LocalTransform = MeshOffset;
		Mesh.bCastShadows = bCastShadows;
		Mesh.AnimationMinScreenSize = AnimationMinScreenSize;
		// UE 提供器必须引用自己所驱动的网格；各提供器复用序列顺序和实体变换，骨骼跟随由离线蒙皮保证。
		for (UAnimSequenceTransformProviderData* AdditionalProvider : AdditionalAnimationProviders)
		{
			FMassSkinnedMeshInstanceVisualizationMeshDesc& Attachment = SkinnedMeshInstanceDesc.Meshes.AddDefaulted_GetRef();
			Attachment.Asset = AdditionalProvider ? AdditionalProvider->GetSkinnedAsset() : nullptr;
			Attachment.TransformProvider = AdditionalProvider;
			Attachment.LocalTransform = MeshOffset;
			Attachment.bCastShadows = bCastShadows;
			Attachment.AnimationMinScreenSize = AnimationMinScreenSize;
		}
	}
	Super::BuildTemplate(BuildContext, World);

	BuildContext.RequireTag<FCastleMassHumanoidTag>();
	BuildContext.RequireFragment<FMassVelocityFragment>();
	BuildContext.AddFragment<FMassRepresentationAnimationFragment>();
	BuildContext.AddFragment<FCastleMassAnimationFragment>();
	BuildContext.AddFragment<FCastleMassAttackAnimationFragment>();
	FCastleMassAnimationParameters Parameters;
	Parameters.IdleSequence = IdleSequence;
	Parameters.MoveSequence = MoveSequence;
	Parameters.RunSequence = RunSequence;
	Parameters.RunSpeedThreshold = RunSpeedThreshold;
	Parameters.AttackSequence = AttackSequence;
	Parameters.MovePlayRate = MovePlayRate;
	Parameters.AttackPlayRate = AttackPlayRate;
	Parameters.BlendTime = BlendTime;
	if (AnimationProvider && AnimationProvider->GetSequences().IsValidIndex(IdleSequence)
		&& AnimationProvider->GetSequences().IsValidIndex(MoveSequence))
	{
		Parameters.IdleLength = AnimationProvider->GetSequencePlayLength(IdleSequence);
		Parameters.MoveLength = AnimationProvider->GetSequencePlayLength(MoveSequence);
		// 模板验证单独拒绝非法索引；构建阶段只读取提供器已有的序列时长。
		if (RunSequence != INDEX_NONE && AnimationProvider->GetSequences().IsValidIndex(RunSequence))
		{
			Parameters.RunLength = AnimationProvider->GetSequencePlayLength(RunSequence);
		}
		if (AttackSequence != INDEX_NONE && AnimationProvider->GetSequences().IsValidIndex(AttackSequence))
		{
			Parameters.AttackLength = AnimationProvider->GetSequencePlayLength(AttackSequence);
		}
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
		&& (RunSequence == INDEX_NONE || (AnimationProvider->GetSequences().IsValidIndex(RunSequence)
			&& FMath::IsFinite(RunSpeedThreshold) && RunSpeedThreshold > 0.0f))
		&& FMath::IsFinite(MovePlayRate) && MovePlayRate > 0.0f
		&& (AttackSequence == INDEX_NONE || AnimationProvider->GetSequences().IsValidIndex(AttackSequence))
		&& FMath::IsFinite(AttackPlayRate) && AttackPlayRate > 0.0f && AttackPlayRate <= 10.0f
		&& FMath::IsFinite(BlendTime) && BlendTime >= 0.0f && !MeshOffset.ContainsNaN()
		&& FMath::IsFinite(AnimationMinScreenSize);
	for (int32 Slot = 0; Slot < MaterialOverrides.Num(); ++Slot)
	{
		if (!IsValid(MaterialOverrides[Slot]))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("Mass 材质覆盖无效：%s，材质槽 %d 为空，拒绝模板。"), *GetPathName(), Slot);
			bValid = false;
		}
	}
	if (bValid)
	{
		const USkinnedAsset* Asset = AnimationProvider->GetSkinnedAsset();
		const FSkeletalMeshRenderData* RenderData = Asset->GetResourceForRendering();
		if (!RenderData || !RenderData->HasUnifiedBoneMap())
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
		const USkeletalMesh* MainMesh = Cast<USkeletalMesh>(Asset);
		if (!MainMesh)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("Mass 显示配置 %s 的主网格 %s 必须是骨骼网格。"), *GetPathName(), *GetPathNameSafe(Asset));
			bValid = false;
		}
		// 空覆盖数组明确保留原材质，但原网格的材质槽仍必须有效，不能显示默认棋盘格。
		if (MainMesh && MaterialOverrides.IsEmpty())
		{
			for (const FSkeletalMaterial& Material : MainMesh->GetMaterials())
			{
				if (!IsValid(Material.MaterialInterface))
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("Mass 主网格 %s 的材质槽 %s 引用为空。"), *MainMesh->GetPathName(), *Material.MaterialSlotName.ToString());
					bValid = false;
				}
			}
		}
		for (UAnimSequenceTransformProviderData* AdditionalProvider : AdditionalAnimationProviders)
		{
			USkeletalMesh* AdditionalMesh = AdditionalProvider ? Cast<USkeletalMesh>(AdditionalProvider->GetSkinnedAsset()) : nullptr;
			bool bMeshValid = MainMesh && IsValid(AdditionalProvider) && AdditionalProvider->IsEnabled()
				&& IsValid(AdditionalMesh) && AdditionalMesh->GetSkeleton() == MainMesh->GetSkeleton();
			const FSkeletalMeshRenderData* AdditionalRenderData = AdditionalMesh ? AdditionalMesh->GetResourceForRendering() : nullptr;
			bMeshValid = bMeshValid && AdditionalRenderData && AdditionalRenderData->HasUnifiedBoneMap();
			if (bMeshValid)
			{
				// Mass 向同一实体所有网格提交相同序列索引和时间；不一致会让武器与手部错位。
				const auto& MainSequences = AnimationProvider->GetSequences();
				const auto& ExtraSequences = AdditionalProvider->GetSequences();
				bMeshValid = MainSequences.Num() == ExtraSequences.Num();
				for (int32 Sequence = 0; bMeshValid && Sequence < MainSequences.Num(); ++Sequence)
				{
					bMeshValid = MainSequences[Sequence].Sequence == ExtraSequences[Sequence].Sequence
						&& MainSequences[Sequence].PlayRate == ExtraSequences[Sequence].PlayRate
						&& MainSequences[Sequence].Position == ExtraSequences[Sequence].Position;
				}
				const FReferenceSkeleton& MainBones = MainMesh->GetRefSkeleton();
				const FReferenceSkeleton& ExtraBones = AdditionalMesh->GetRefSkeleton();
				bMeshValid = bMeshValid && MainBones.GetNum() == ExtraBones.GetNum();
				for (int32 Bone = 0; bMeshValid && Bone < MainBones.GetNum(); ++Bone)
				{
					bMeshValid = MainBones.GetBoneName(Bone) == ExtraBones.GetBoneName(Bone)
						&& MainBones.GetParentIndex(Bone) == ExtraBones.GetParentIndex(Bone)
						&& MainBones.GetRefBonePose()[Bone].Equals(ExtraBones.GetRefBonePose()[Bone]);
				}
				for (const FSkeletalMaterial& Material : AdditionalMesh->GetMaterials())
				{
					bMeshValid = bMeshValid && IsValid(Material.MaterialInterface);
				}
			}
			if (!bMeshValid)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("Mass 附属提供器 %s（网格 %s）与主网格 %s 不兼容：需相同参考骨架与序列、有效材质和 Optimize for Instancing。"), *GetPathNameSafe(AdditionalProvider), *GetPathNameSafe(AdditionalMesh), *GetPathNameSafe(MainMesh));
				bValid = false;
			}
		}
	}
	if (!bValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 人形动画配置无效：%s，检查骨骼网格、动画提供器、序列索引与跑步阈值（索引 %d，阈值 %.2f cm/s）。"),
			*GetPathName(), RunSequence, RunSpeedThreshold);
	}
	return bValid;
}
