#include "CastleMassDeathSplitProcessor.h"
#include "CastleMassEnemyLaserAttackProcessor.h"

#include "CastleMassDeathSplitFragments.h"
#include "CastleMainTowerComponent.h"
#include "CastleMassTraits.h"
#include "CastleMassFragments.h"
#include "CastleMassMeleeAttackProcessor.h"
#include "CastleMassEnemyRangedAttackProcessor.h"
#include "CastleMassProcessors.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "MassCommands.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassEntityConfigAsset.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "Moving_Castle.h"
#include "Projectiles/CastleMassEnemyProjectileProcessor.h"
#include "Projectiles/CastleMassHomingProjectileProcessor.h"

UCastleMassDeathSplitProcessor::UCastleMassDeathSplitProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteAfter.Add(UCastleMassMeleeAttackProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteAfter.Add(UCastleMassEnemyRangedAttackProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteAfter.Add(UCastleMassEnemyLaserAttackProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteAfter.Add(UCastleMassHomingProjectileProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteAfter.Add(UCastleMassEnemyProjectileProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteBefore.Add(UCastleMassLifetimeProcessor::StaticClass()->GetFName());
	bRequiresGameThreadExecution = true;
}

void UCastleMassDeathSplitProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassDeathSplitFragment>(EMassFragmentAccess::ReadWrite);
}

void UCastleMassDeathSplitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = GetWorld();
	UCastleMainTowerSubsystem* TowerSubsystem = World ? World->GetSubsystem<UCastleMainTowerSubsystem>() : nullptr;
	if (!TowerSubsystem)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("死亡分裂失败：世界 %s 缺少主塔表面子系统。"), *GetPathNameSafe(World));
		return;
	}
	FCastleTowerPrism TowerPrism;
	const UCastleMainTowerComponent* TowerComponent = TowerSubsystem->GetRegisteredMainTower();
	const bool bHasLivingTower = IsValid(TowerComponent) && TowerComponent->IsAlive();
	const FCastleTowerPrism* TowerGeometry = nullptr;
	if (bHasLivingTower)
	{
		if (!TowerSubsystem->GetMainTowerPrism(TowerPrism)) { return; }
		TowerGeometry = &TowerPrism;
	}

	Query.ForEachEntityChunk(Context, [this, World, TowerGeometry](FMassExecutionContext& Chunk)
	{
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Transforms = Chunk.GetFragmentView<FTransformFragment>();
		const auto Radii = Chunk.GetFragmentView<FAgentRadiusFragment>();
		const auto Splits = Chunk.GetMutableFragmentView<FCastleMassDeathSplitFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			auto& Split = Splits[It];
			if (Units[It].Health > 0.0f || Split.bHasSplit) { continue; }
			const UCastleMassEnemyTrait* ChildTrait = Split.ChildConfiguration.IsValid()
				? Cast<UCastleMassEnemyTrait>(Split.ChildConfiguration->FindTrait(UCastleMassEnemyTrait::StaticClass())) : nullptr;
			if (!IsValid(ChildTrait) || Split.SpawnCount < 1 || Split.SpawnCount > 64
				|| !FMath::IsFinite(Split.SpawnSpreadRadius) || Split.SpawnSpreadRadius <= 0.0f || Split.SpawnSpreadRadius > 5000.0f
				|| !FMath::IsFinite(Split.BurstDuration) || Split.BurstDuration <= 0.0f || Split.BurstDuration > 5.0f
				|| !FMath::IsFinite(Split.BurstArcHeight) || Split.BurstArcHeight < 0.0f || Split.BurstArcHeight > 1000.0f)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("敌人 %d 死亡分裂失败：子配置 %s、数量或散布半径无效。"), Chunk.GetEntity(It).Index, *GetPathNameSafe(Split.ChildConfiguration.Get()));
				continue;
			}
			if (!FMath::IsFinite(Radii[It].Radius) || Radii[It].Radius < 0.0f
				|| !FMath::IsFinite(ChildTrait->Radius) || ChildTrait->Radius < 0.0f)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("敌人 %d 死亡分裂失败：母体半径或子配置 %s 的子体半径无效。"),
					Chunk.GetEntity(It).Index, *GetPathNameSafe(Split.ChildConfiguration.Get()));
				continue;
			}
			TArray<FTransform> Spawns;
			const FTransform& MotherTransform = Transforms[It].GetTransform();
			const FVector TowerUp = TowerGeometry ? TowerGeometry->GetUpVector() : FVector::UpVector;
			const float MotherRadius = Radii[It].Radius * MotherTransform.GetScale3D().GetAbsMax();
			const float ChildRadius = ChildTrait->Radius * MotherTransform.GetScale3D().GetAbsMax();
			const FVector MotherRoot = MotherTransform.GetLocation();
			const FVector MotherCenter = MotherRoot + TowerUp * MotherRadius;
			if (!FMath::IsFinite(MotherRadius) || !FMath::IsFinite(ChildRadius)
				|| !MotherRoot.ContainsNaN() || !MotherCenter.ContainsNaN())
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("敌人 %d 死亡分裂失败：子配置 %s 的缩放半径或起点无效。"),
					Chunk.GetEntity(It).Index, *GetPathNameSafe(Split.ChildConfiguration.Get()));
				continue;
			}
			FVector BurstStartRoot = MotherRoot;
			FVector BurstArcDirection = FVector::UpVector;
			FVector RadialAxisX = FVector::ForwardVector;
			FVector RadialAxisY = FVector::RightVector;
			if (TowerGeometry)
			{
				FVector SurfacePoint;
				FVector SurfaceNormal;
				float SurfaceDistance = 0.0f;
				if (!TowerGeometry->QuerySurface(MotherCenter, SurfacePoint, SurfaceNormal, SurfaceDistance))
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("敌人 %d 死亡分裂失败：无法查询主塔表面，子配置 %s。"),
						Chunk.GetEntity(It).Index, *Split.ChildConfiguration->GetPathName());
					continue;
				}
				if (SurfaceDistance < Split.SpawnSpreadRadius + Split.BurstArcHeight + ChildRadius)
				{
					BurstArcDirection = SurfaceNormal;
					FVector SphereStart = MotherCenter;
					const float RequiredDistance = ChildRadius + 2.0f;
					if (SurfaceDistance < RequiredDistance)
					{
						SphereStart = SurfacePoint + SurfaceNormal * RequiredDistance;
					}
					BurstStartRoot = SphereStart - TowerUp * ChildRadius;
					const FVector ReferenceAxis = FMath::Abs(FVector::DotProduct(SurfaceNormal, TowerUp)) < 0.95f
						? TowerUp : FVector::ForwardVector;
					RadialAxisX = FVector::CrossProduct(SurfaceNormal, ReferenceAxis).GetSafeNormal();
					RadialAxisY = FVector::CrossProduct(SurfaceNormal, RadialAxisX).GetSafeNormal();
					if (RadialAxisX.IsNearlyZero() || RadialAxisY.IsNearlyZero())
					{
						UE_LOG(LogMoving_Castle, Error, TEXT("敌人 %d 死亡分裂失败：主塔外法线无法构造爆散切平面。"), Chunk.GetEntity(It).Index);
						continue;
					}
				}
			}
			for (int32 Index = 0; Index < Split.SpawnCount; ++Index)
			{
				const double Angle = UE_TWO_PI * Index / Split.SpawnCount;
				FTransform Transform = MotherTransform;
				Transform.SetLocation(BurstStartRoot);
				const FVector RadialDirection = RadialAxisX * FMath::Cos(Angle) + RadialAxisY * FMath::Sin(Angle);
				Transform.SetRotation(RadialDirection.Rotation().Quaternion());
				Spawns.Add(Transform);
			}
			const float BurstDuration = Split.BurstDuration;
			const float BurstArcHeight = Split.BurstArcHeight;
			const float SpreadRadius = Split.SpawnSpreadRadius;
			TWeakObjectPtr<UWorld> WeakWorld = World;
			UMassEntityConfigAsset* Configuration = Split.ChildConfiguration.Get();
			// 分裂标记先落地，生成在安全边界执行；同帧重复查询也不能生成第二批。
			Split.bHasSplit = true;
			Chunk.Defer().PushCommand<FMassDeferredCreateCommand>([WeakWorld, Configuration, Spawns = MoveTemp(Spawns),
				BurstStartRoot, BurstDuration, BurstArcHeight, SpreadRadius, BurstArcDirection,
				RadialAxisX, RadialAxisY, ChildRadius](FMassEntityManager& Manager)
			{
				if (!WeakWorld.IsValid())
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("死亡分裂失败：世界已失效，子配置 %s。"), *GetPathNameSafe(Configuration));
					return;
				}
				TArray<FCastleMassUnitHandle> Children;
				FText Reason;
				if (!WeakWorld->GetSubsystem<UCastleMassUnitSubsystem>()->SpawnUnits(Configuration, Spawns, Children, Reason))
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("死亡分裂失败：子配置 %s：%s。"), *GetPathNameSafe(Configuration), *Reason.ToString());
					return;
				}
				if (Children.Num() != Spawns.Num())
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("死亡分裂失败：配置 %s 请求 %d 只幼骸，实际生成 %d 只。"), *GetPathNameSafe(Configuration), Spawns.Num(), Children.Num());
					return;
				}
				for (int32 Index = 0; Index < Children.Num(); ++Index)
				{
					// 只给本次死亡分裂生成的幼骸添加瞬态状态，直接生成同一配置的幼骸不会被搬回世界原点。
					Manager.AddFragmentToEntity(Children[Index].Entity, FCastleMassSpawnBurstFragment::StaticStruct());
					FCastleMassSpawnBurstFragment* Burst = Manager.GetFragmentDataPtr<FCastleMassSpawnBurstFragment>(Children[Index].Entity);
					if (!Burst)
					{
						UE_LOG(LogMoving_Castle, Error, TEXT("死亡分裂失败：子配置 %s 的实体 %d 缺少爆散状态 Fragment。"), *GetPathNameSafe(Configuration), Children[Index].Entity.Index);
						return;
					}
					const double Angle = UE_TWO_PI * Index / Children.Num();
					const FVector RadialDirection = RadialAxisX * FMath::Cos(Angle) + RadialAxisY * FMath::Sin(Angle);
					Burst->Start = BurstStartRoot;
					Burst->Target = BurstStartRoot + RadialDirection * SpreadRadius;
					Burst->ArcDirection = BurstArcDirection;
					Burst->Elapsed = 0.0f;
					Burst->Duration = BurstDuration;
					Burst->ArcHeight = BurstArcHeight;
				}
				UE_LOG(LogMoving_Castle, Display, TEXT("死亡分裂：%s 生成 %d 只幼骸，爆散半径 %.1f cm。"), *Configuration->GetPathName(), Children.Num(), SpreadRadius);
			});
		}
	});
}

UCastleMassSpawnBurstProcessor::UCastleMassSpawnBurstProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
	ExecutionOrder.ExecuteAfter.Add(UCastleMassMovementProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteBefore.Add(UCastleMassMeleeAttackProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteBefore.Add(UCastleMassEnemyRangedAttackProcessor::StaticClass()->GetFName());
	bRequiresGameThreadExecution = true;
}

void UCastleMassSpawnBurstProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassSpawnBurstFragment>(EMassFragmentAccess::ReadWrite);
}

void UCastleMassSpawnBurstProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = GetWorld();
	UCastleMainTowerSubsystem* TowerSubsystem = World ? World->GetSubsystem<UCastleMainTowerSubsystem>() : nullptr;
	if (!TowerSubsystem)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("幼骸爆散失败：世界 %s 缺少主塔表面子系统。"), *GetPathNameSafe(World));
		return;
	}
	FCastleTowerPrism TowerPrism;
	const UCastleMainTowerComponent* TowerComponent = TowerSubsystem->GetRegisteredMainTower();
	const bool bHasLivingTower = IsValid(TowerComponent) && TowerComponent->IsAlive();
	const FCastleTowerPrism* TowerGeometry = nullptr;
	if (bHasLivingTower)
	{
		if (!TowerSubsystem->GetMainTowerPrism(TowerPrism)) { return; }
		TowerGeometry = &TowerPrism;
	}

	Query.ForEachEntityChunk(Context, [TowerGeometry](FMassExecutionContext& Chunk)
	{
		const float DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Radii = Chunk.GetFragmentView<FAgentRadiusFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
		const auto Bursts = Chunk.GetMutableFragmentView<FCastleMassSpawnBurstFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (Units[It].Health <= 0.0f) { continue; }
			FCastleMassSpawnBurstFragment& Burst = Bursts[It];
			if (Burst.Start.ContainsNaN() || Burst.Target.ContainsNaN() || Burst.ArcDirection.ContainsNaN()
				|| Burst.ArcDirection.IsNearlyZero() || !FMath::IsFinite(Burst.Duration)
				|| Burst.Duration <= 0.0f || !FMath::IsFinite(Burst.ArcHeight) || Burst.ArcHeight < 0.0f
				|| !FMath::IsFinite(Radii[It].Radius) || Radii[It].Radius < 0.0f)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("幼骸爆散状态无效：实体 %d 的起止点、时长或弧高非法。"), Chunk.GetEntity(It).Index);
				continue;
			}
			// 爆散中的幼骸若被空投减速，仍沿原抛物线，只改变它的时间进度。
			Burst.Elapsed = FMath::Min(Burst.Duration, Burst.Elapsed + DeltaTime * Units[It].SupportMovementMultiplier);
			const float Alpha = Burst.Elapsed / Burst.Duration;
			const float Arc = 4.0f * Burst.ArcHeight * Alpha * (1.0f - Alpha);
			FTransform& Transform = Transforms[It].GetMutableTransform();
			FVector Location = FMath::Lerp(Burst.Start, Burst.Target, Alpha) + Burst.ArcDirection.GetSafeNormal() * Arc;
			if (TowerGeometry)
			{
				const float Radius = Radii[It].Radius * Transform.GetScale3D().GetAbsMax();
				if (!FMath::IsFinite(Radius) || Radius < 0.0f)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("幼骸爆散失败：实体 %d 的逻辑半径无效。"), Chunk.GetEntity(It).Index);
					continue;
				}
				FVector SphereCenter = Location + TowerGeometry->GetUpVector() * Radius;
				FVector NoVelocity = FVector::ZeroVector;
				if (!TowerGeometry->ProjectSphereOutside(SphereCenter, Radius, NoVelocity))
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("幼骸爆散失败：实体 %d 的主塔表面投影失败。"), Chunk.GetEntity(It).Index);
					continue;
				}
				Location = SphereCenter - TowerGeometry->GetUpVector() * Radius;
				FVector Local = TowerGeometry->ToLocalPoint(Location);
				if (Local.Z < 0.0f)
				{
					Local.Z = 0.0f;
					Location = TowerGeometry->BottomCenter + TowerGeometry->Rotation.RotateVector(Local);
				}
			}
			Transform.SetLocation(Location);
			Velocities[It].Value = FVector::ZeroVector;
			Targets[It] = FCastleMassMoveTargetFragment();
			if (Alpha >= 1.0f)
			{
				Burst.Target = Location;
				Transform.SetLocation(Burst.Target);
				Chunk.Defer().RemoveFragment<FCastleMassSpawnBurstFragment>(Chunk.GetEntity(It));
			}
		}
	});
}
