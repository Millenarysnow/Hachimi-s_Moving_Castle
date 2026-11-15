#include "CastleMassDeathSplitProcessor.h"

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
#include "Moving_Castle.h"
#include "Projectiles/CastleMassEnemyProjectileProcessor.h"
#include "Projectiles/CastleMassHomingProjectileProcessor.h"

UCastleMassDeathSplitProcessor::UCastleMassDeathSplitProcessor() : Query(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteAfter.Add(UCastleMassMeleeAttackProcessor::StaticClass()->GetFName());
	ExecutionOrder.ExecuteAfter.Add(UCastleMassEnemyRangedAttackProcessor::StaticClass()->GetFName());
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
			if (!Split.ChildConfiguration.IsValid() || Split.SpawnCount <= 0
				|| !FMath::IsFinite(Split.SpawnSpreadRadius) || Split.SpawnSpreadRadius < 0.0f)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("敌人 %d 死亡分裂失败：子配置 %s、数量或散布半径无效。"), Chunk.GetEntity(It).Index, *GetPathNameSafe(Split.ChildConfiguration.Get()));
				continue;
			}
			Split.bHasSplit = true;
			TArray<FTransform> Spawns;
			for (int32 Index = 0; Index < Split.SpawnCount; ++Index)
			{
				const double Angle = UE_TWO_PI * Index / Split.SpawnCount;
				FTransform Transform = Transforms[It].GetTransform();
				Transform.AddToTranslation(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Split.SpawnSpreadRadius);
				Spawns.Add(Transform);
			}
			TWeakObjectPtr<UWorld> World = GetWorld();
			UMassEntityConfigAsset* Configuration = Split.ChildConfiguration.Get();
			// 分裂标记先落地，生成在安全边界执行；同帧重复查询也不能生成第二批。
			Chunk.Defer().PushCommand<FMassDeferredCreateCommand>([World, Configuration, Spawns = MoveTemp(Spawns)](FMassEntityManager& Manager)
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
				UE_LOG(LogMoving_Castle, Display, TEXT("死亡分裂：%s 生成 %d 只幼骸。"), *Configuration->GetPathName(), Children.Num());
			});
		}
	});
}
