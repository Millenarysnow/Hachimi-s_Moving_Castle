#include "CastleMassProcessors.h"

#include "CastleMainTowerComponent.h"
#include "CastleMassFragments.h"
#include "Enemies/Flight/CastleMassFlightSolver.h"
#include "Enemies/CastleMassDeathSplitFragments.h"
#include "Projectiles/CastleMassHomingProjectileFragments.h"
#include "Projectiles/CastleMassEnemyProjectileFragments.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassCommandBuffer.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassRepresentationAnimationProcessor.h"
#include "MassRepresentationFragments.h"
#include "Moving_Castle.h"

namespace CastleMass
{
	/** 只访问当前 Chunk 的值类型数据，可安全在工作线程执行。 */
	void FaceVelocity(FTransform& Transform, const FVector& Velocity, const FCastleMassMovementSettings& Settings, float DeltaTime)
	{
		FVector Direction = Velocity;
		if (Settings.bKeepUpright) { Direction.Z = 0.0; }
		if (!Direction.IsNearlyZero() && Settings.TurnRate > 0.0f)
		{
			const FRotator Rotation = FMath::RInterpConstantTo(Transform.Rotator(), Direction.Rotation(), DeltaTime, Settings.TurnRate);
			Transform.SetRotation(Rotation.Quaternion());
		}
	}
}

UCastleMassMovementProcessor::UCastleMassMovementProcessor() : Query(*this), EnemyQuery(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
	bRequiresGameThreadExecution = true;
}

void UCastleMassMovementProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// NPC 仍沿用本查询的既有移动分支；敌人群体由后续全局快照统一解算。
	Query.AddTagRequirement<FCastleMassNPCTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassSpawnBurstFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassPatrolFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	Query.AddConstSharedRequirement<FCastleMassMovementParameters>();

	EnemyQuery.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All);
	EnemyQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EnemyQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	EnemyQuery.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	EnemyQuery.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	EnemyQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	EnemyQuery.AddRequirement<FCastleMassFlightIntentFragment>(EMassFragmentAccess::ReadWrite);
	EnemyQuery.AddRequirement<FCastleMassSpawnBurstFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EnemyQuery.AddConstSharedRequirement<FCastleMassMovementParameters>();
}

void UCastleMassMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// NPC 保留原有三维目标移动、巡逻排除、加速度和到达逻辑，不进入敌人 Boids 邻域。
	Query.ForEachEntityChunk(Context, [](FMassExecutionContext& Chunk)
	{
		const float DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const FCastleMassMovementSettings& Settings = Chunk.GetConstSharedFragment<FCastleMassMovementParameters>().Settings;
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Patrols = Chunk.GetFragmentView<FCastleMassPatrolFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (Units[It].Health <= 0.0f || (!Patrols.IsEmpty() && (Patrols[It].bEnabled || Patrols[It].bSupportJourney))) { continue; }
			// 按实例出场配置和当前支援效果共同计算速度，不写共享移动参数。
			const float Rate = Units[It].SpawnMovementMultiplier * Units[It].SupportMovementMultiplier;
			const float MaxSpeed = Settings.MaxSpeed * Rate;
			FTransform& Transform = Transforms[It].GetMutableTransform();
			FVector& Velocity = Velocities[It].Value;
			FCastleMassMoveTargetFragment& Target = Targets[It];
			const FVector ToTarget = Target.Target - Transform.GetLocation();
			const double Distance = ToTarget.Size();
			if (Target.bHasTarget && Distance <= Settings.AcceptanceRadius)
			{
				Target.bHasTarget = false;
				Target.DesiredVelocity = FVector::ZeroVector;
				Velocity = FVector::ZeroVector;
				continue;
			}

			FVector Desired = (Target.DesiredVelocity * Rate).GetClampedToMaxSize(MaxSpeed);
			if (Target.bHasTarget)
			{
				const float Speed = Settings.Acceleration > 0.0f
					? FMath::Min(MaxSpeed, FMath::Sqrt(2.0f * Settings.Acceleration * static_cast<float>(FMath::Max(0.0, Distance - Settings.AcceptanceRadius))))
					: MaxSpeed;
				Desired = ToTarget.GetSafeNormal() * Speed;
			}
			Velocity = Settings.Acceleration > 0.0f
				? FMath::VInterpConstantTo(Velocity, Desired, DeltaTime, Settings.Acceleration) : Desired;
			// 减速结算后的首帧就受新上限约束，不等原速度自然降下来。
			if (Rate < 1.0f) { Velocity = Velocity.GetClampedToMaxSize(MaxSpeed); }
			const FVector Step = Velocity * DeltaTime;
			// 跨过目标平面时钳制位置，避免大帧间隔或高速单位在目标两侧振荡。
			if (Target.bHasTarget && FVector::DotProduct(Step, ToTarget.GetSafeNormal()) >= Distance)
			{
				Transform.SetLocation(Target.Target);
				Target.bHasTarget = false;
				Target.DesiredVelocity = FVector::ZeroVector;
				CastleMass::FaceVelocity(Transform, Velocity, Settings, DeltaTime);
				Velocity = FVector::ZeroVector;
			}
			else
			{
				Transform.AddToTranslation(Step);
				CastleMass::FaceVelocity(Transform, Velocity, Settings, DeltaTime);
			}
		}
	});

	UWorld* World = GetWorld();
	UCastleMainTowerSubsystem* TowerSubsystem = World ? World->GetSubsystem<UCastleMainTowerSubsystem>() : nullptr;
	if (!TowerSubsystem)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 飞行解算失败：世界 %s 缺少主塔表面子系统。"), *GetPathNameSafe(World));
		return;
	}

	FCastleTowerPrism TowerPrism;
	const UCastleMainTowerComponent* RegisteredTower = TowerSubsystem->GetRegisteredMainTower();
	const bool bHasLivingTower = IsValid(RegisteredTower) && RegisteredTower->IsAlive();
	const FCastleTowerPrism* TowerGeometry = nullptr;
	if (bHasLivingTower)
	{
		if (!TowerSubsystem->GetMainTowerPrism(TowerPrism)) { return; }
		TowerGeometry = &TowerPrism;
	}

	TArray<FCastleMassFlightBody> Bodies;
	TArray<FTransform*> TransformOwners;
	TArray<FVector*> VelocityOwners;
	TArray<FCastleMassMoveTargetFragment*> TargetOwners;
	TArray<FCastleMassFlightIntentFragment*> IntentOwners;
	TArray<FCastleMassMovementSettings> MovementSettings;
	bool bInvalidBody = false;
	EnemyQuery.ForEachEntityChunk(Context, [&Bodies, &TransformOwners, &VelocityOwners, &TargetOwners,
		&IntentOwners, &MovementSettings, &bInvalidBody](FMassExecutionContext& Chunk)
	{
		const float DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const FCastleMassMovementSettings& Settings = Chunk.GetConstSharedFragment<FCastleMassMovementParameters>().Settings;
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Radii = Chunk.GetFragmentView<FAgentRadiusFragment>();
		const auto Intents = Chunk.GetMutableFragmentView<FCastleMassFlightIntentFragment>();
		const auto Bursts = Chunk.GetFragmentView<FCastleMassSpawnBurstFragment>();

		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (!FMath::IsFinite(Units[It].Health))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("Mass 飞行解算失败：实体 %d 的生命不是有限值。"), Chunk.GetEntity(It).Index);
				bInvalidBody = true;
				continue;
			}
			if (Units[It].Health <= 0.0f) { continue; }

			FTransform& Transform = Transforms[It].GetMutableTransform();
			FMassVelocityFragment& Velocity = Velocities[It];
			FCastleMassMoveTargetFragment& Target = Targets[It];
			FCastleMassFlightIntentFragment& Intent = Intents[It];
			const FVector Scale = Transform.GetScale3D().GetAbs();
			const float EffectiveRadius = Radii[It].Radius * Scale.GetAbsMax();
			const float SpeedMultiplier = Units[It].SpawnMovementMultiplier * Units[It].SupportMovementMultiplier;
			const float MaxSpeed = Settings.MaxSpeed * SpeedMultiplier;
			if (Transform.ContainsNaN() || !FMath::IsFinite(Radii[It].Radius) || Radii[It].Radius < 0.0f
				|| !FMath::IsFinite(EffectiveRadius) || !FMath::IsFinite(MaxSpeed) || MaxSpeed < 0.0f
				|| !FMath::IsFinite(Settings.Acceleration) || Settings.Acceleration < 0.0f
				|| !FMath::IsFinite(Settings.AcceptanceRadius) || Settings.AcceptanceRadius < 0.0f)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("Mass 飞行解算失败：实体 %d 的位置、逻辑半径或移动参数无效。"), Chunk.GetEntity(It).Index);
				bInvalidBody = true;
				continue;
			}

			FCastleMassFlightBody& Body = Bodies.AddDefaulted_GetRef();
			Body.EntityIndex = Chunk.GetEntity(It).Index;
			Body.Location = Transform.GetLocation();
			Body.Velocity = Velocity.Value;
			Body.Target = Target.Target;
			Body.DesiredVelocity = Target.DesiredVelocity * SpeedMultiplier;
			Body.Radius = EffectiveRadius;
			Body.MaxSpeed = MaxSpeed;
			Body.Acceleration = Settings.Acceleration;
			Body.AcceptanceRadius = Settings.AcceptanceRadius;
			Body.AttackRange = Intent.AttackRange;
			Body.IdealStandOff = Intent.IdealStandOff;
			Body.bHasTarget = Target.bHasTarget;
			Body.bTrackingMainTower = Intent.bTrackingMainTower;
			Body.bInAttackRange = Intent.bInAttackRange;
			Body.bStopMovement = Intent.bStopMovement;
			Body.bSpawnBurst = !Bursts.IsEmpty();
			TransformOwners.Add(&Transform);
			VelocityOwners.Add(&Velocity.Value);
			TargetOwners.Add(&Target);
			IntentOwners.Add(&Intent);
			MovementSettings.Add(Settings);
			(void)DeltaTime;
		}
	});
	if (bInvalidBody) { return; }

	TArray<FCastleMassFlightResult> Results;
	FString FailureReason;
	if (!FCastleMassFlightSolver::Solve(Bodies, Context.GetDeltaTimeSeconds(),
		TowerGeometry, Results, FailureReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 飞行解算失败：%s"), *FailureReason);
		return;
	}

	// 先前已经收齐全部实体快照，此处才统一写回新速度和新位置，维持同帧对称性。
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		if (Bodies[Index].bSpawnBurst) { continue; }
		FTransform& Transform = *TransformOwners[Index];
		FVector& Velocity = *VelocityOwners[Index];
		Transform.SetLocation(Results[Index].Location);
		Velocity = Results[Index].Velocity;
		if (Results[Index].bReachedTarget && !IntentOwners[Index]->bTrackingMainTower)
		{
			TargetOwners[Index]->bHasTarget = false;
			TargetOwners[Index]->DesiredVelocity = FVector::ZeroVector;
		}
		CastleMass::FaceVelocity(Transform, Velocity, MovementSettings[Index], Context.GetDeltaTimeSeconds());
	}
}

UCastleMassProjectileProcessor::UCastleMassProjectileProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
}

void UCastleMassProjectileProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassProjectileTag>(EMassFragmentPresence::All);
	// 追踪子类由专用处理器拥有位移，普通子弹仍保持原有的速度和重力弹道语义。
	Query.AddRequirement<FCastleMassHomingProjectileFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
	// 敌方追踪弹同样有专属位移所有者，不能再被普通弹道推进一次。
	Query.AddRequirement<FCastleMassEnemyProjectileFragment>(EMassFragmentAccess::None, EMassFragmentPresence::None);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassProjectileFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddConstSharedRequirement<FCastleMassMovementParameters>();
}

void UCastleMassProjectileProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	Query.ParallelForEachEntityChunk(Context, [](FMassExecutionContext& Chunk)
	{
		const float DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Projectiles = Chunk.GetMutableFragmentView<FCastleMassProjectileFragment>();
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto& Settings = Chunk.GetConstSharedFragment<FCastleMassMovementParameters>().Settings;
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (Units[It].Health <= 0.0f) { continue; }
			FTransform& Transform = Transforms[It].GetMutableTransform();
			FVector& Velocity = Velocities[It].Value;
			FCastleMassProjectileFragment& Projectile = Projectiles[It];
			Projectile.PreviousLocation = Transform.GetLocation();
			const FVector Acceleration(0.0, 0.0, -Projectile.Gravity);
			Transform.AddToTranslation(Velocity * DeltaTime + Acceleration * (0.5f * DeltaTime * DeltaTime));
			Velocity += Acceleration * DeltaTime;
			CastleMass::FaceVelocity(Transform, Velocity, Settings, DeltaTime);
		}
	});
}

UCastleMassAnimationProcessor::UCastleMassAnimationProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Representation;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::VisualizationProcessing);
	ExecutionOrder.ExecuteBefore.Add(UMassConsumeInstancedSkinnedMeshAnimationProcessor::StaticClass()->GetFName());
}

void UCastleMassAnimationProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassHumanoidTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassAnimationFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassAttackAnimationFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FMassRepresentationAnimationFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddConstSharedRequirement<FCastleMassAnimationParameters>();
}

void UCastleMassAnimationProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	Query.ParallelForEachEntityChunk(Context, [](FMassExecutionContext& Chunk)
	{
		const float DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const auto Velocities = Chunk.GetFragmentView<FMassVelocityFragment>();
		const auto Animations = Chunk.GetMutableFragmentView<FCastleMassAnimationFragment>();
		const auto AttackStates = Chunk.GetFragmentView<FCastleMassAttackAnimationFragment>();
		const auto Representations = Chunk.GetMutableFragmentView<FMassRepresentationAnimationFragment>();
		const auto& Parameters = Chunk.GetConstSharedFragment<FCastleMassAnimationParameters>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			FCastleMassAnimationFragment& Animation = Animations[It];
			const bool bAttacking = AttackStates[It].bInAttackState && Parameters.AttackSequence != INDEX_NONE;
			const bool bHasRun = Parameters.RunSequence != INDEX_NONE;
			// 仅启用走跑的地面角色使用水平速度。敌人的三维移动判定与固定倍率保持原语义。
			const FVector& Velocity = Velocities[It].Value;
			const double SpeedSquared = bHasRun ? Velocity.SizeSquared2D() : Velocity.SizeSquared();
			const bool bMoving = SpeedSquared > 1.0;
			const bool bRunning = bHasRun && bMoving && SpeedSquared > FMath::Square(static_cast<double>(Parameters.RunSpeedThreshold));
			int32 Sequence = Parameters.IdleSequence;
			float Length = Parameters.IdleLength;
			float PlayRate = 1.0f;
			if (bAttacking)
			{
				Sequence = Parameters.AttackSequence;
				Length = Parameters.AttackLength;
				PlayRate = Parameters.AttackPlayRate;
			}
			else if (bMoving)
			{
				Sequence = bRunning ? Parameters.RunSequence : Parameters.MoveSequence;
				Length = bRunning ? Parameters.RunLength : Parameters.MoveLength;
				PlayRate = Parameters.MovePlayRate;
				if (bHasRun)
				{
					// 走路以正式 NPC 的常速为基准，跑步以切入阈值为基准；限制步频范围以避免画面抖动。
					const float ReferenceSpeed = bRunning ? Parameters.RunSpeedThreshold : 200.0f;
					PlayRate *= FMath::Clamp(static_cast<float>(FMath::Sqrt(SpeedSquared)) / ReferenceSpeed, 0.75f, 1.8f);
				}
			}
			Length = FMath::Max(UE_SMALL_NUMBER, Length);
			const bool bChanged = Sequence != Animation.CurrentSequence;
			Animation.Position = bChanged ? Animation.Phase * Length
				: FMath::Fmod(Animation.Position + DeltaTime * PlayRate, Length);
			Animation.CurrentSequence = Sequence;
			FAnimSequenceTrackAutoPlayData& Data = Representations[It].AnimData;
			Data.SequenceIndex = Sequence;
			Data.Position = Animation.Position;
			Data.PlayRate = PlayRate;
			// 每次提交都保留过渡参数，离屏后首次创建动画轨道时也能正确恢复。
			Data.BlendTime = Parameters.BlendTime;
			Data.LoopMode = EAnimSequenceTrackLoopMode::Loop;
		}
	});
}

UCastleMassLifetimeProcessor::UCastleMassLifetimeProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
}

void UCastleMassLifetimeProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassUnitTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadWrite);
}

void UCastleMassLifetimeProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 销毁走 Mass 批量延迟命令；Observer 会同步清理实例化显示和动画轨道。
	Query.ForEachEntityChunk(Context, [](FMassExecutionContext& Chunk)
	{
		const float DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const auto Units = Chunk.GetMutableFragmentView<FCastleMassUnitFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			FCastleMassUnitFragment& Unit = Units[It];
			Unit.Age += DeltaTime;
			if (Unit.Health <= 0.0f || (Unit.Lifetime > 0.0f && Unit.Age >= Unit.Lifetime))
			{
				Chunk.Defer().DestroyEntity(Chunk.GetEntity(It));
			}
		}
	});
}
