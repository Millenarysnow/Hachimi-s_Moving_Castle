#include "CastleMassProcessors.h"

#include "CastleMassFragments.h"
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

UCastleMassMovementProcessor::UCastleMassMovementProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
}

void UCastleMassMovementProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassHumanoidTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassPatrolFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	Query.AddConstSharedRequirement<FCastleMassMovementParameters>();
}

void UCastleMassMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	Query.ParallelForEachEntityChunk(Context, [](FMassExecutionContext& Chunk)
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
			if (Units[It].Health <= 0.0f || (!Patrols.IsEmpty() && Patrols[It].bEnabled)) { continue; }
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

			FVector Desired = Target.DesiredVelocity.GetClampedToMaxSize(Settings.MaxSpeed);
			if (Target.bHasTarget)
			{
				const float Speed = Settings.Acceleration > 0.0f
					? FMath::Min(Settings.MaxSpeed, FMath::Sqrt(2.0f * Settings.Acceleration * static_cast<float>(FMath::Max(0.0, Distance - Settings.AcceptanceRadius))))
					: Settings.MaxSpeed;
				Desired = ToTarget.GetSafeNormal() * Speed;
			}
			Velocity = Settings.Acceleration > 0.0f
				? FMath::VInterpConstantTo(Velocity, Desired, DeltaTime, Settings.Acceleration) : Desired;
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
}

UCastleMassProjectileProcessor::UCastleMassProjectileProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
}

void UCastleMassProjectileProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddTagRequirement<FCastleMassProjectileTag>(EMassFragmentPresence::All);
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
		const auto Representations = Chunk.GetMutableFragmentView<FMassRepresentationAnimationFragment>();
		const auto& Parameters = Chunk.GetConstSharedFragment<FCastleMassAnimationParameters>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			FCastleMassAnimationFragment& Animation = Animations[It];
			const bool bMoving = Velocities[It].Value.SizeSquared() > 1.0;
			const int32 Sequence = bMoving ? Parameters.MoveSequence : Parameters.IdleSequence;
			const float Length = FMath::Max(UE_SMALL_NUMBER, bMoving ? Parameters.MoveLength : Parameters.IdleLength);
			const float PlayRate = bMoving ? Parameters.MovePlayRate : 1.0f;
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
