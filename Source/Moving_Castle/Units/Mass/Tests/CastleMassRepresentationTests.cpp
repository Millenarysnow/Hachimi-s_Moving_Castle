#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Animation/AnimSequenceTransformProviderData.h"
#include "CastleMassFragments.h"
#include "CastleMassSpawner.h"
#include "CastleMassUnitSubsystem.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "MassCommands.h"
#include "MassCommonFragments.h"
#include "MassEntitySubsystem.h"
#include "MassRepresentationFragments.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace CastleMassTests
{
	/** 在独立 -game 示例关卡中验证真实自动调度、显示批次、动画轨道和销毁 Observer。 */
	class FRuntimeRepresentationCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRuntimeRepresentationCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (Stage == 0)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
				}
				if (!Test->TestTrue(TEXT("在 Mass 示例游戏世界中运行"), World && World->GetMapName().EndsWith(TEXT("L_MassUnitsDemo")))) { return true; }
				UCastleMassUnitSubsystem* Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
				FMassEntityManager& Manager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
				for (TActorIterator<ACastleMassSpawner> It(World); It; ++It)
				{
					TArray<FMassEntityHandle> Entities;
					TArray<FVector> Locations;
					for (int32 Index = 0; Index < It->SpawnedUnits.Num(); ++Index)
					{
						const auto& Handle = It->SpawnedUnits[Index];
						if (!Units->IsUnitValid(Handle)) { continue; }
						if (Manager.GetFragmentDataChecked<FCastleMassUnitFragment>(Handle.Entity).Kind == ECastleMassUnitKind::Projectile) { continue; }
						Handles.Add(Handle);
						Entities.Add(Handle.Entity);
						Locations.Add(It->GetActorTransform().TransformPosition(FVector(Index % It->Columns * It->Spacing, Index / It->Columns * It->Spacing, 0.0)));
					}
					Units->StopUnits(It->SpawnedUnits);
					// 自动化发现测试需要时间，将队列恢复到相机前再检查实际显示。
					Manager.Defer().PushCommand<FMassDeferredSetCommand>([Entities = MoveTemp(Entities), Locations = MoveTemp(Locations)](FMassEntityManager& EntityManager)
					{
						for (int32 Index = 0; Index < Entities.Num(); ++Index)
						{
							if (EntityManager.IsEntityActive(Entities[Index]))
							{
								EntityManager.GetFragmentDataChecked<FTransformFragment>(Entities[Index]).GetMutableTransform().SetLocation(Locations[Index]);
							}
						}
					});
				}
				if (!Test->TestEqual(TEXT("默认流程生成 512 个人形 Entity"), Handles.Num(), 512)) { return true; }
				Units->SetUnitsVelocity(TArray<FCastleMassUnitHandle>(Handles.GetData(), 256), FVector(50.0, 0.0, 0.0));
				NextCheck = FPlatformTime::Seconds() + 1.0;
				Stage = 1;
				return false;
			}
			if (FPlatformTime::Seconds() < NextCheck) { return false; }
			if (Stage == 1)
			{
				int32 Instances = 0;
				int32 Components = 0;
				int32 IdleTracks = 0;
				int32 MovingTracks = 0;
				for (TObjectIterator<UInstancedSkinnedMeshComponent> It; It; ++It)
				{
					if (It->GetWorld() != World || It->GetInstanceCount() == 0) { continue; }
					++Components;
					Instances += It->GetInstanceCount();
					Test->TestNotNull(TEXT("骨骼批次已创建渲染代理"), It->GetSceneProxy());
					auto* Provider = Cast<UAnimSequenceTransformProviderDataInstance>(It->GetTransformProvider());
					if (!Test->TestNotNull(TEXT("批次使用 GPU 序列动画实例提供器"), Provider)) { return true; }
					for (int32 Index = 0; Index < It->GetInstanceCount(); ++Index)
					{
						int32 Track = INDEX_NONE;
						if (!It->GetInstanceAnimationIndex(It->GetInstanceId(Index), Track) || Track == INDEX_NONE) { continue; }
						const int32 Sequence = Provider->GetSequenceIndex(Track, 0);
						IdleTracks += Sequence == 0;
						MovingTracks += Sequence == 1;
						if (!SampleProvider) { SampleProvider = Provider; SampleTrack = Track; Position = Provider->GetPosition(Track, 0).TargetPosition; }
					}
				}
				Test->TestEqual(TEXT("512 实体合并为一个骨骼实例组件"), Components, 1);
				Test->TestEqual(TEXT("渲染批次包含全部人形"), Instances, 512);
				Test->TestEqual(TEXT("独立待机轨道数"), IdleTracks, 256);
				Test->TestEqual(TEXT("独立行走轨道数"), MovingTracks, 256);
				IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("Mass")), true);
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Mass/RuntimeRepresentation.png"), false, false);
				NextCheck = FPlatformTime::Seconds() + 0.4;
				Stage = 2;
				return false;
			}
			if (Stage == 2)
			{
				if (Test->TestNotNull(TEXT("已获得可观测动画轨道"), SampleProvider))
				{
					Test->TestTrue(TEXT("动画轨道随世界时间持续播放"), !FMath::IsNearlyEqual(Position, SampleProvider->GetPosition(SampleTrack, 0).TargetPosition));
				}
				World->GetSubsystem<UCastleMassUnitSubsystem>()->DespawnUnits(Handles);
				NextCheck = FPlatformTime::Seconds() + 1.0;
				Stage = 3;
				return false;
			}
			int32 Remaining = 0;
			for (TObjectIterator<UInstancedSkinnedMeshComponent> It; It; ++It)
			{
				if (It->GetWorld() == World) { Remaining += It->GetInstanceCount(); }
			}
			Test->TestEqual(TEXT("实体销毁后 Observer 清空全部骨骼实例"), Remaining, 0);
			return true;
		}
	private:
		FAutomationTestBase* Test;
		UWorld* World = nullptr;
		TArray<FCastleMassUnitHandle> Handles;
		UAnimSequenceTransformProviderDataInstance* SampleProvider = nullptr;
		int32 SampleTrack = INDEX_NONE;
		int32 Stage = 0;
		float Position = 0.0f;
		double NextCheck = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassRuntimeRepresentationTest, "MovingCastle.MassDemo.RuntimeRepresentation",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassRuntimeRepresentationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleMassTests::FRuntimeRepresentationCommand(this));
	return true;
}

#endif
