#include "CastleMassUnitSubsystem.h"

#include "CastleMassFragments.h"
#include "CastleMassTraits.h"
#include "Engine/World.h"
#include "MassCommands.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "MassMovementFragments.h"
#include "MassSpawnerSubsystem.h"
#include "Mass/CastleMassPatrolFragments.h"

bool UCastleMassUnitSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FMassEntityManager* UCastleMassUnitSubsystem::GetManager() const
{
	if (!IsInGameThread() || !GetWorld()) { return nullptr; }
	UMassEntitySubsystem* Subsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	return Subsystem ? &Subsystem->GetMutableEntityManager() : nullptr;
}

bool UCastleMassUnitSubsystem::CollectHandles(const TArray<FCastleMassUnitHandle>& Units, TArray<FMassEntityHandle>& OutHandles) const
{
	if (!GetManager() || Units.IsEmpty()) { return false; }
	OutHandles.Reserve(Units.Num());
	for (const FCastleMassUnitHandle& Unit : Units)
	{
		// 一个错误世界的句柄会拒绝整批；实体是否仍存活在命令执行时再次检查。
		if (Unit.World.Get() != GetWorld() || !Unit.Entity.IsSet()) { return false; }
		OutHandles.Add(Unit.Entity);
	}
	return true;
}

bool UCastleMassUnitSubsystem::SpawnUnits(UMassEntityConfigAsset* Configuration, const TArray<FTransform>& SpawnTransforms,
	TArray<FCastleMassUnitHandle>& OutUnits, FText& OutFailureReason)
{
	OutUnits.Reset();
	OutFailureReason = FText::GetEmpty();
	FMassEntityManager* Manager = GetManager();
	if (!Manager || Manager->IsProcessing())
	{
		OutFailureReason = FText::FromString(TEXT("请在游戏线程的 Mass Processor 外批量生成单位；Processor 内生成请使用 Context.Defer()。"));
		return false;
	}
	if (!IsValid(Configuration) || !Configuration->FindTrait(UCastleMassUnitTrait::StaticClass()) || SpawnTransforms.IsEmpty())
	{
		OutFailureReason = FText::FromString(TEXT("需要包含 Castle Mass 单位 Trait 的配置，以及至少一个生成变换。"));
		return false;
	}
	for (const FTransform& Transform : SpawnTransforms)
	{
		if (Transform.ContainsNaN() || !Transform.IsRotationNormalized())
		{
			OutFailureReason = FText::FromString(TEXT("生成变换包含无效数值或未归一化旋转，整批生成已取消。"));
			return false;
		}
	}
	if (!ValidatedConfigurations.Contains(Configuration))
	{
		if (!Configuration->GetMutableConfig().ValidateEntityTemplate(*GetWorld()))
		{
			OutFailureReason = FText::FromString(TEXT("Mass 配置验证失败，请检查 Output Log 中的 Trait 或动画配置错误。"));
			return false;
		}
		ValidatedConfigurations.Add(Configuration);
	}
	const FMassEntityTemplate& Template = Configuration->GetOrCreateEntityTemplate(*GetWorld());
	UMassSpawnerSubsystem* Spawner = GetWorld()->GetSubsystem<UMassSpawnerSubsystem>();
	if (!Template.IsValid() || !Spawner)
	{
		OutFailureReason = FText::FromString(TEXT("Mass 模板或生成子系统未就绪。"));
		return false;
	}

	TArray<FMassEntityHandle> Entities;
	// 保持创建上下文直到全部变换与相位写入；Observer 不会提前看到半初始化实体。
	auto CreationContext = Spawner->SpawnEntities(Template, SpawnTransforms.Num(), Entities);
	OutUnits.Reserve(Entities.Num());
	const UCastleMassUnitTrait* Trait = CastChecked<UCastleMassUnitTrait>(Configuration->FindTrait(UCastleMassUnitTrait::StaticClass()));
	for (int32 Index = 0; Index < Entities.Num(); ++Index)
	{
		const FMassEntityHandle Entity = Entities[Index];
		Manager->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform() = SpawnTransforms[Index];
		if (FCastleMassProjectileFragment* Projectile = Manager->GetFragmentDataPtr<FCastleMassProjectileFragment>(Entity))
		{
			Projectile->PreviousLocation = SpawnTransforms[Index].GetLocation();
		}
		if (FCastleMassAnimationFragment* Animation = Manager->GetFragmentDataPtr<FCastleMassAnimationFragment>(Entity))
		{
			Animation->Phase = FMath::Fmod(static_cast<float>(Entity.Index) * 0.61803398875f, 1.0f);
		}
		FCastleMassUnitHandle& Handle = OutUnits.AddDefaulted_GetRef();
		Handle.Entity = Entity;
		Handle.World = GetWorld();
	}
	// 引擎的 UMassRandomVelocityInitializer 会观察所有新 FMassVelocityFragment，
	// 将 X/Y 当作随机速度区间；必须等创建 Observer 完成后再写入真实发射速度。
	CreationContext.Reset();
	for (int32 Index = 0; Index < Entities.Num(); ++Index)
	{
		if (Manager->IsEntityActive(Entities[Index]))
		{
			const bool bProjectile = Manager->GetFragmentDataPtr<FCastleMassProjectileFragment>(Entities[Index]) != nullptr;
			Manager->GetFragmentDataChecked<FMassVelocityFragment>(Entities[Index]).Value = bProjectile
				? SpawnTransforms[Index].GetRotation().GetForwardVector() * Trait->Movement.MaxSpeed : FVector::ZeroVector;
		}
	}
	return true;
}

bool UCastleMassUnitSubsystem::MoveUnitsTo(const TArray<FCastleMassUnitHandle>& Units, FVector Destination)
{
	TArray<FMassEntityHandle> Handles;
	if (Destination.ContainsNaN() || !CollectHandles(Units, Handles)) { return false; }
	GetManager()->Defer().PushCommand<FMassDeferredSetCommand>([Handles = MoveTemp(Handles), Destination](FMassEntityManager& Manager)
	{
		for (const FMassEntityHandle Handle : Handles)
		{
			if (!Manager.IsEntityActive(Handle)) { continue; }
			const auto* Unit = Manager.GetFragmentDataPtr<FCastleMassUnitFragment>(Handle);
			if (!Unit || Unit->Kind == ECastleMassUnitKind::Projectile) { continue; }
			// 显式移动命令解除巡逻，保证同一实体同一帧只有一个移动系统负责位移。
			if (auto* Patrol = Manager.GetFragmentDataPtr<FCastleMassPatrolFragment>(Handle)) { Patrol->bEnabled = false; }
			auto& Target = Manager.GetFragmentDataChecked<FCastleMassMoveTargetFragment>(Handle);
			Target.Target = Destination;
			Target.bHasTarget = true;
			Target.DesiredVelocity = FVector::ZeroVector;
		}
	});
	return true;
}

bool UCastleMassUnitSubsystem::FollowSharedPatrolRoute(const TArray<FCastleMassUnitHandle>& Units, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	TArray<FMassEntityHandle> Handles;
	FMassEntityManager* Manager = GetManager();
	if (!CollectHandles(Units, Handles) || Manager->IsProcessing())
	{
		OutFailureReason = FText::FromString(TEXT("请在 Mass 处理外提交本世界的非空 NPC 批次。"));
		return false;
	}
	for (const FMassEntityHandle Handle : Handles)
	{
		if (!Manager->IsEntityActive(Handle) || !Manager->GetFragmentDataPtr<FCastleMassPatrolFragment>(Handle))
		{
			OutFailureReason = FText::FromString(TEXT("共享巡逻只能接收存活的 Castle Mass NPC；整批操作已拒绝。"));
			return false;
		}
	}
	Manager->Defer().PushCommand<FMassDeferredSetCommand>([Handles = MoveTemp(Handles)](FMassEntityManager& EntityManager)
	{
		for (const FMassEntityHandle Handle : Handles)
		{
			if (!EntityManager.IsEntityActive(Handle)) { continue; }
			auto& Patrol = EntityManager.GetFragmentDataChecked<FCastleMassPatrolFragment>(Handle);
			Patrol = FCastleMassPatrolFragment();
			Patrol.bEnabled = true;
			EntityManager.GetFragmentDataChecked<FCastleMassMoveTargetFragment>(Handle) = FCastleMassMoveTargetFragment();
			EntityManager.GetFragmentDataChecked<FMassVelocityFragment>(Handle).Value = FVector::ZeroVector;
		}
	});
	return true;
}

bool UCastleMassUnitSubsystem::SetUnitsVelocity(const TArray<FCastleMassUnitHandle>& Units, FVector Velocity)
{
	TArray<FMassEntityHandle> Handles;
	if (Velocity.ContainsNaN() || !CollectHandles(Units, Handles)) { return false; }
	GetManager()->Defer().PushCommand<FMassDeferredSetCommand>([Handles = MoveTemp(Handles), Velocity](FMassEntityManager& Manager)
	{
		for (const FMassEntityHandle Handle : Handles)
		{
			if (!Manager.IsEntityActive(Handle) || !Manager.GetFragmentDataPtr<FCastleMassUnitFragment>(Handle)) { continue; }
			if (auto* Patrol = Manager.GetFragmentDataPtr<FCastleMassPatrolFragment>(Handle)) { Patrol->bEnabled = false; }
			auto& Target = Manager.GetFragmentDataChecked<FCastleMassMoveTargetFragment>(Handle);
			Target.bHasTarget = false;
			Target.DesiredVelocity = Velocity;
			if (Manager.GetFragmentDataPtr<FCastleMassProjectileFragment>(Handle))
			{
				Manager.GetFragmentDataChecked<FMassVelocityFragment>(Handle).Value = Velocity;
			}
		}
	});
	return true;
}

bool UCastleMassUnitSubsystem::StopUnits(const TArray<FCastleMassUnitHandle>& Units)
{
	TArray<FMassEntityHandle> Handles;
	if (!CollectHandles(Units, Handles)) { return false; }
	GetManager()->Defer().PushCommand<FMassDeferredSetCommand>([Handles = MoveTemp(Handles)](FMassEntityManager& Manager)
	{
		for (const FMassEntityHandle Handle : Handles)
		{
			if (!Manager.IsEntityActive(Handle) || !Manager.GetFragmentDataPtr<FCastleMassUnitFragment>(Handle)) { continue; }
			if (auto* Patrol = Manager.GetFragmentDataPtr<FCastleMassPatrolFragment>(Handle)) { Patrol->bEnabled = false; }
			Manager.GetFragmentDataChecked<FCastleMassMoveTargetFragment>(Handle) = FCastleMassMoveTargetFragment();
			Manager.GetFragmentDataChecked<FMassVelocityFragment>(Handle).Value = FVector::ZeroVector;
		}
	});
	return true;
}

bool UCastleMassUnitSubsystem::DamageUnits(const TArray<FCastleMassUnitHandle>& Units, float Damage)
{
	TArray<FMassEntityHandle> Handles;
	if (!FMath::IsFinite(Damage) || Damage < 0.0f || !CollectHandles(Units, Handles)) { return false; }
	GetManager()->Defer().PushCommand<FMassDeferredSetCommand>([Handles = MoveTemp(Handles), Damage](FMassEntityManager& Manager)
	{
		for (const FMassEntityHandle Handle : Handles)
		{
			if (!Manager.IsEntityActive(Handle)) { continue; }
			if (auto* Unit = Manager.GetFragmentDataPtr<FCastleMassUnitFragment>(Handle))
			{
				Unit->Health = FMath::Max(0.0f, Unit->Health - Damage);
			}
		}
	});
	return true;
}

bool UCastleMassUnitSubsystem::DespawnUnits(const TArray<FCastleMassUnitHandle>& Units)
{
	return DamageUnits(Units, TNumericLimits<float>::Max());
}

bool UCastleMassUnitSubsystem::IsUnitValid(const FCastleMassUnitHandle& Unit) const
{
	const FMassEntityManager* Manager = GetManager();
	return Manager && Unit.World.Get() == GetWorld() && Manager->IsEntityActive(Unit.Entity);
}

bool UCastleMassUnitSubsystem::GetUnitSnapshot(const FCastleMassUnitHandle& Unit, FCastleMassUnitSnapshot& OutSnapshot) const
{
	OutSnapshot = FCastleMassUnitSnapshot();
	const FMassEntityManager* Manager = GetManager();
	if (!Manager || Manager->IsProcessing() || !IsUnitValid(Unit)) { return false; }
	const auto* Data = Manager->GetFragmentDataPtr<FCastleMassUnitFragment>(Unit.Entity);
	if (!Data) { return false; }
	OutSnapshot.Kind = Data->Kind;
	OutSnapshot.Team = Data->Team;
	OutSnapshot.Health = Data->Health;
	OutSnapshot.Transform = Manager->GetFragmentDataChecked<FTransformFragment>(Unit.Entity).GetTransform();
	OutSnapshot.Velocity = Manager->GetFragmentDataChecked<FMassVelocityFragment>(Unit.Entity).Value;
	OutSnapshot.bHasMoveTarget = Manager->GetFragmentDataChecked<FCastleMassMoveTargetFragment>(Unit.Entity).bHasTarget;
	if (const auto* Patrol = Manager->GetFragmentDataPtr<FCastleMassPatrolFragment>(Unit.Entity))
	{
		OutSnapshot.bFollowingSharedRoute = Patrol->bEnabled;
		OutSnapshot.bPatrolBlocked = Patrol->bBlocked;
		OutSnapshot.PatrolLeg = Patrol->Leg;
		OutSnapshot.CompletedPatrolLaps = Patrol->CompletedLaps;
	}
	if (const auto* Enemy = Manager->GetFragmentDataPtr<FCastleMassEnemyFragment>(Unit.Entity))
	{
		OutSnapshot.bInAttackRange = Enemy->bInAttackRange;
		OutSnapshot.AttackCount = Enemy->AttackCount;
	}
	return true;
}
