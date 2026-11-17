#include "CastleMassDeathSplitProcessor.h"
#include "CastleMassEnemyLaserAttackProcessor.h"

#include "CastleMassDeathSplitFragments.h"
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
	Query.AddRequirement<FCastleMassDeathSplitFragment>(EMassFragmentAccess::ReadWrite);
}

void UCastleMassDeathSplitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	Query.ForEachEntityChunk(Context, [this](FMassExecutionContext& Chunk)
	{
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Transforms = Chunk.GetFragmentView<FTransformFragment>();
		const auto Splits = Chunk.GetMutableFragmentView<FCastleMassDeathSplitFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			auto& Split = Splits[It];
			if (Units[It].Health > 0.0f || Split.bHasSplit) { continue; }
			if (!Split.ChildConfiguration.IsValid() || Split.SpawnCount < 1 || Split.SpawnCount > 64
				|| !FMath::IsFinite(Split.SpawnSpreadRadius) || Split.SpawnSpreadRadius <= 0.0f || Split.SpawnSpreadRadius > 5000.0f
				|| !FMath::IsFinite(Split.BurstDuration) || Split.BurstDuration <= 0.0f || Split.BurstDuration > 5.0f
				|| !FMath::IsFinite(Split.BurstArcHeight) || Split.BurstArcHeight < 0.0f || Split.BurstArcHeight > 1000.0f)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("敌人 %d 死亡分裂失败：子配置 %s、数量或散布半径无效。"), Chunk.GetEntity(It).Index, *GetPathNameSafe(Split.ChildConfiguration.Get()));
				continue;
			}
			Split.bHasSplit = true;
			TArray<FTransform> Spawns;
			const FVector Center = Transforms[It].GetTransform().GetLocation();
			for (int32 Index = 0; Index < Split.SpawnCount; ++Index)
			{
				const double Angle = UE_TWO_PI * Index / Split.SpawnCount;
				FTransform Transform = Transforms[It].GetTransform();
				Transform.SetLocation(Center);
				Transform.SetRotation(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0).Rotation().Quaternion());
				Spawns.Add(Transform);
			}
			const float BurstDuration = Split.BurstDuration;
			const float BurstArcHeight = Split.BurstArcHeight;
			const float SpreadRadius = Split.SpawnSpreadRadius;
			TWeakObjectPtr<UWorld> World = GetWorld();
			UMassEntityConfigAsset* Configuration = Split.ChildConfiguration.Get();
			// 分裂标记先落地，生成在安全边界执行；同帧重复查询也不能生成第二批。
			Chunk.Defer().PushCommand<FMassDeferredCreateCommand>([World, Configuration, Spawns = MoveTemp(Spawns), Center, BurstDuration, BurstArcHeight, SpreadRadius](FMassEntityManager& Manager)
			{
				if (!World.IsValid())
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("死亡分裂失败：世界已失效，子配置 %s。"), *GetPathNameSafe(Configuration));
					return;
				}
				TArray<FCastleMassUnitHandle> Children;
				FText Reason;
				if (!World->GetSubsystem<UCastleMassUnitSubsystem>()->SpawnUnits(Configuration, Spawns, Children, Reason))
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
					Burst->Start = Center;
					Burst->Target = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * SpreadRadius;
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
	Query.AddRequirement<FCastleMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FCastleMassSpawnBurstFragment>(EMassFragmentAccess::ReadWrite);
}

void UCastleMassSpawnBurstProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	Query.ForEachEntityChunk(Context, [](FMassExecutionContext& Chunk)
	{
		const float DeltaTime = FMath::Max(0.0f, Chunk.GetDeltaTimeSeconds());
		const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Transforms = Chunk.GetMutableFragmentView<FTransformFragment>();
		const auto Velocities = Chunk.GetMutableFragmentView<FMassVelocityFragment>();
		const auto Targets = Chunk.GetMutableFragmentView<FCastleMassMoveTargetFragment>();
		const auto Bursts = Chunk.GetMutableFragmentView<FCastleMassSpawnBurstFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (Units[It].Health <= 0.0f) { continue; }
			FCastleMassSpawnBurstFragment& Burst = Bursts[It];
			if (Burst.Start.ContainsNaN() || Burst.Target.ContainsNaN() || !FMath::IsFinite(Burst.Duration)
				|| Burst.Duration <= 0.0f || !FMath::IsFinite(Burst.ArcHeight) || Burst.ArcHeight < 0.0f)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("幼骸爆散状态无效：实体 %d 的起止点、时长或弧高非法。"), Chunk.GetEntity(It).Index);
				continue;
			}
			// 爆散中的幼骸若被空投减速，仍沿原抛物线，只改变它的时间进度。
			Burst.Elapsed = FMath::Min(Burst.Duration, Burst.Elapsed + DeltaTime * Units[It].SupportMovementMultiplier);
			const float Alpha = Burst.Elapsed / Burst.Duration;
			const float Arc = 4.0f * Burst.ArcHeight * Alpha * (1.0f - Alpha);
			FTransform& Transform = Transforms[It].GetMutableTransform();
			Transform.SetLocation(FMath::Lerp(Burst.Start, Burst.Target, Alpha) + FVector(0.0f, 0.0f, Arc));
			Velocities[It].Value = FVector::ZeroVector;
			Targets[It] = FCastleMassMoveTargetFragment();
			if (Alpha >= 1.0f)
			{
				Transform.SetLocation(Burst.Target);
				Chunk.Defer().RemoveFragment<FCastleMassSpawnBurstFragment>(Chunk.GetEntity(It));
			}
		}
	});
}
