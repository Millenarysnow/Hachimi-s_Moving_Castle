#include "CastleMassUnitSubsystem.h"

#include "CastleMassFragments.h"
#include "CastleMassTraits.h"
#include "Enemies/CastleMassMeleeAttackFragments.h"
#include "Engine/World.h"
#include "MassCommands.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "MassEntityQuery.h"
#include "MassEntityView.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassSpawnerSubsystem.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Projectiles/CastleMassHomingProjectileFragments.h"
#include "Projectiles/CastleMassHomingProjectileTrait.h"

DEFINE_LOG_CATEGORY_STATIC(LogCastleMassHomingProjectile, Log, All);

bool UCastleMassUnitSubsystem::FindNearestEnemy(const FVector& Origin, float Radius, FCastleMassUnitHandle& OutEnemy) const
{
	OutEnemy = FCastleMassUnitHandle();
	FMassEntityManager* Manager = GetManager();
	if (!Manager || Manager->IsProcessing())
	{
		UE_LOG(LogCastleMassHomingProjectile, Error, TEXT("FindNearestEnemy：世界 %s，必须在游戏线程且 Mass 未处理时查询。"), *GetNameSafe(GetWorld()));
		return false;
	}
	if (Origin.ContainsNaN() || !FMath::IsFinite(Radius) || Radius < 0.0f)
	{
		UE_LOG(LogCastleMassHomingProjectile, Error, TEXT("FindNearestEnemy：世界 %s，Origin 必须为有限坐标，Radius 必须为有限非负数（%f）。"), *GetNameSafe(GetWorld()), Radius);
		return false;
	}

	// 查询定义就是存活敌人；NPC、子弹和死亡敌人均不属于候选集合，无目标是正常游戏状态。
	FMassEntityQuery Query(Manager->AsShared());
	Query.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	FMassExecutionContext Context(*Manager, 0.0f, false);
	double NearestSquared = FMath::Square(static_cast<double>(Radius));
	bool bInvalidCandidate = false;
	Query.ForEachEntityChunk(Context, [this, &Origin, &NearestSquared, &OutEnemy, &bInvalidCandidate](FMassExecutionContext& Chunk)
	{
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Transforms = Chunk.GetFragmentView<FTransformFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			const FVector Location = Transforms[It].GetTransform().GetLocation();
			if (!FMath::IsFinite(Units[It].Health) || Location.ContainsNaN() || Units[It].Kind != ECastleMassUnitKind::Enemy)
			{
				UE_LOG(LogCastleMassHomingProjectile, Error, TEXT("FindNearestEnemy：敌人实体 %d 的生命、位置或单位身份无效，查询失败。"), Chunk.GetEntity(It).Index);
				bInvalidCandidate = true;
				return;
			}
			if (Units[It].Health <= 0.0f) { continue; }
			const double DistanceSquared = FVector::DistSquared(Origin, Location);
			if (DistanceSquared <= NearestSquared)
			{
				NearestSquared = DistanceSquared;
				OutEnemy.Entity = Chunk.GetEntity(It);
				OutEnemy.World = GetWorld();
			}
		}
	});
	if (bInvalidCandidate) { OutEnemy = FCastleMassUnitHandle(); return false; }
	return OutEnemy.Entity.IsSet();
}

bool UCastleMassUnitSubsystem::SpawnHomingProjectiles(UMassEntityConfigAsset* Configuration,
	const TArray<FCastleMassHomingProjectileSpawn>& Spawns, TArray<FCastleMassUnitHandle>& OutProjectiles, FText& OutFailureReason)
{
	OutProjectiles.Reset();
	OutFailureReason = FText::GetEmpty();
	const auto Reject = [Configuration, &OutFailureReason](const FString& Reason)
	{
		OutFailureReason = FText::FromString(Reason);
		UE_LOG(LogCastleMassHomingProjectile, Error, TEXT("SpawnHomingProjectiles：配置 %s：%s"), *GetPathNameSafe(Configuration), *Reason);
		return false;
	};
	FMassEntityManager* Manager = GetManager();
	if (!Manager || Manager->IsProcessing()) { return Reject(TEXT("必须在游戏线程且 Mass 未处理时生成，整批拒绝。")); }
	if (!IsValid(Configuration) || Spawns.IsEmpty()) { return Reject(TEXT("需要有效配置及非空发射批次，整批拒绝。")); }
	const auto* Trait = Cast<UCastleMassHomingProjectileTrait>(Configuration->FindTrait(UCastleMassHomingProjectileTrait::StaticClass()));
	if (!Trait) { return Reject(TEXT("配置必须包含 UCastleMassHomingProjectileTrait，整批拒绝。")); }

	// UE 按精确类合并继承 Trait；按子到父解析同类覆盖，显式拒绝同世界的多个单位模板所有者。
	// 子资产停用本地基类覆盖是配置选择，不修改父资产，也不在运行时猜测应采用哪一份移动参数。
	const EProcessorExecutionFlags WorldFlag = GetWorld()->GetNetMode() == NM_Standalone ? EProcessorExecutionFlags::Standalone
		: GetWorld()->GetNetMode() == NM_Client ? EProcessorExecutionFlags::Client : EProcessorExecutionFlags::Server;
	TSet<const UMassEntityConfigAsset*> Visited;
	TSet<const UClass*> ResolvedClasses;
	int32 UnitTraitCount = 0;
	for (const UMassEntityConfigAsset* Asset = Configuration; Asset; Asset = Asset->GetConfig().GetParent())
	{
		if (Visited.Contains(Asset)) { return Reject(TEXT("配置父链存在循环，整批拒绝。")); }
		Visited.Add(Asset);
		for (const UMassEntityTraitBase* ConfiguredTrait : Asset->GetConfig().GetTraits())
		{
			if (!IsValid(ConfiguredTrait)) { return Reject(FString::Printf(TEXT("资产 %s 含无效 Trait，整批拒绝。"), *Asset->GetPathName())); }
			if (ResolvedClasses.Contains(ConfiguredTrait->GetClass())) { continue; }
			ResolvedClasses.Add(ConfiguredTrait->GetClass());
			if (ConfiguredTrait->IsA<UCastleMassUnitTrait>() && EnumHasAnyFlags(ConfiguredTrait->ValidTargetConfig, WorldFlag)) { ++UnitTraitCount; }
		}
	}
	if (UnitTraitCount != 1 || !EnumHasAnyFlags(Trait->ValidTargetConfig, WorldFlag))
	{
		return Reject(TEXT("追踪子弹必须是当前世界唯一启用的单位 Trait；继承基类资产时请在子资产显式停用同类覆盖的基类 Trait，整批拒绝。"));
	}

	TArray<FVector> Directions;
	Directions.Reserve(Spawns.Num());
	for (int32 Index = 0; Index < Spawns.Num(); ++Index)
	{
		const FCastleMassHomingProjectileSpawn& Spawn = Spawns[Index];
		if (Spawn.Origin.ContainsNaN() || !FMath::IsFinite(Spawn.Damage) || Spawn.Damage < 0.0f)
		{
			return Reject(FString::Printf(TEXT("第 %d 发的 Origin 或 Damage 无效，伤害必须为有限非负数，整批拒绝。"), Index));
		}
		if (Spawn.Target.World.Get() != GetWorld() || !Manager->IsEntityActive(Spawn.Target.Entity))
		{
			return Reject(FString::Printf(TEXT("第 %d 发目标 %d 不属于当前世界或已经消失，整批拒绝。"), Index, Spawn.Target.Entity.Index));
		}
		const FMassEntityView TargetView(*Manager, Spawn.Target.Entity);
		const auto* TargetUnit = TargetView.GetFragmentDataPtr<FCastleMassUnitFragment>();
		const auto* TargetTransform = TargetView.GetFragmentDataPtr<FTransformFragment>();
		const auto* TargetRadius = TargetView.GetFragmentDataPtr<FAgentRadiusFragment>();
		if (!TargetView.HasTag<FCastleMassEnemyTag>() || !TargetUnit || TargetUnit->Kind != ECastleMassUnitKind::Enemy
			|| !FMath::IsFinite(TargetUnit->Health) || TargetUnit->Health <= 0.0f || !TargetTransform || !TargetRadius
			|| TargetTransform->GetTransform().ContainsNaN() || !FMath::IsFinite(TargetRadius->Radius) || TargetRadius->Radius < 0.0f)
		{
			return Reject(FString::Printf(TEXT("第 %d 发目标 %d 必须是位置、半径、生命有效的存活敌人，整批拒绝。"), Index, Spawn.Target.Entity.Index));
		}
		Directions.Add(TargetTransform->GetTransform().GetLocation() - Spawn.Origin);
	}
	if (!ValidatedConfigurations.Contains(Configuration))
	{
		if (!Configuration->GetMutableConfig().ValidateEntityTemplate(*GetWorld())) { return Reject(TEXT("Mass 追踪子弹模板校验失败，整批拒绝。")); }
		ValidatedConfigurations.Add(Configuration);
	}
	const FMassEntityTemplate& Template = Configuration->GetOrCreateEntityTemplate(*GetWorld());
	UMassSpawnerSubsystem* Spawner = GetWorld()->GetSubsystem<UMassSpawnerSubsystem>();
	if (!Template.IsValid() || !Spawner) { return Reject(TEXT("Mass 模板或生成子系统未就绪，整批拒绝。")); }

	// 持有创建上下文直到所有逐发数据都写完，使 Observer 不会读到未锁定目标的半初始化子弹。
	TArray<FMassEntityHandle> Entities;
	auto CreationContext = Spawner->SpawnEntities(Template, Spawns.Num(), Entities);
	checkf(Entities.Num() == Spawns.Num(), TEXT("Mass 生成器违反批量数量约定，配置 %s"), *Configuration->GetPathName());
	OutProjectiles.Reserve(Entities.Num());
	for (int32 Index = 0; Index < Entities.Num(); ++Index)
	{
		const FMassEntityHandle Entity = Entities[Index];
		Manager->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform() = FTransform(Spawns[Index].Origin);
		Manager->GetFragmentDataChecked<FCastleMassHomingProjectileFragment>(Entity).Target = Spawns[Index].Target.Entity;
		FCastleMassProjectileFragment& Projectile = Manager->GetFragmentDataChecked<FCastleMassProjectileFragment>(Entity);
		Projectile.PreviousLocation = Spawns[Index].Origin;
		Projectile.Damage = Spawns[Index].Damage;
		FCastleMassUnitHandle& Handle = OutProjectiles.AddDefaulted_GetRef();
		Handle.Entity = Entity;
		Handle.World = GetWorld();
	}
	CreationContext.Reset();
	// 真实初速度必须在随机速度初始化 Observer 完成后写；零距离直接交给首帧重合命中。
	for (int32 Index = 0; Index < Entities.Num(); ++Index)
	{
		const double Distance = Directions[Index].Size();
		Manager->GetFragmentDataChecked<FMassVelocityFragment>(Entities[Index]).Value = Distance > 0.0
			? Directions[Index] * (Trait->Movement.MaxSpeed / Distance) : FVector::ZeroVector;
		if (Distance > 0.0) { Manager->GetFragmentDataChecked<FTransformFragment>(Entities[Index]).GetMutableTransform().SetRotation(Directions[Index].ToOrientationQuat()); }
	}
	return true;
}

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
	if (Configuration->FindTrait(UCastleMassHomingProjectileTrait::StaticClass()))
	{
		// 追踪子类必须逐发给出目标和伤害，不能借通用入口隐式采用 Trait 的模板伤害。
		OutFailureReason = FText::FromString(TEXT("追踪子弹必须通过 SpawnHomingProjectiles 提供逐发目标与伤害，整批生成已取消。"));
		UE_LOG(LogCastleMassHomingProjectile, Error, TEXT("SpawnUnits：配置 %s：%s"), *Configuration->GetPathName(), *OutFailureReason.ToString());
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
	if (const auto* Melee = Manager->GetFragmentDataPtr<FCastleMassMeleeAttackFragment>(Unit.Entity))
	{
		OutSnapshot.bInAttackRange = Melee->bInAttackRange;
		OutSnapshot.AttackCount = Melee->AttackCount;
	}
	return true;
}
