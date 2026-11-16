#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastlePlayerController.h"
#include "CastleMainTowerComponent.h"
#include "CastleMassEnemySpawner.h"
#include "CastleMassFragments.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Animation/AnimSequenceTransformProviderData.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Enemies/CastleMassEnemyLaserAttackFragments.h"
#include "Enemies/CastleMassEnemyLaserBeamSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"
#include "MassActorSubsystem.h"
#include "MassEntityConfigAsset.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "MassEntitySubsystem.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Subsystems/CastleShieldTowerSubsystem.h"
#include "UnrealClient.h"

namespace CastleConcreteRuntimeTests
{
	/** 在已有棋盘的 TestMap1 中验收控制台、怪物主体/剑的真实动画批次和反伤分裂，不手工调用 Processor。 */
	class FConcreteEnemiesCommand : public IAutomationLatentCommand
	{
	public:
		explicit FConcreteEnemiesCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (Stage == 0)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
				}
				if (!Test->TestTrue(TEXT("在 TestMap1 游戏世界运行"), World && World->GetMapName().EndsWith(TEXT("TestMap1")))) { return true; }
				Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
				if (!Test->TestNotNull(TEXT("使用正式玩家控制器"), Controller)) { return true; }
				FVector Center;
				if (!Test->TestTrue(TEXT("正式主塔已注册"), World->GetSubsystem<UCastleMainTowerSubsystem>()->GetMainTowerTargetLocation(Center))) { return true; }
				auto& Manager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
				if (!Test->TestFalse(TEXT("摆放验收队列时 Mass 已结束本帧处理"), Manager.IsProcessing())) { return true; }
				const TCHAR* Types[] = {TEXT("LavaGoliath"), TEXT("LavaPup"), TEXT("MiasmaWelp"), TEXT("ShadowSwarm")};
				for (int32 Index = 0; Index < 4; ++Index)
				{
					// 真实 Exec 解析路径必须正确接受省略 WaveCount 的控制台输入。
					Controller->ConsoleCommand(FString(TEXT("CastleSpawnEnemy ")) + Types[Index]);
					const FString Path = FString(TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_")) + Types[Index];
					auto* Asset = LoadObject<UMassEntityConfigAsset>(nullptr, *Path);
					ACastleMassEnemySpawner* Match = nullptr;
					for (TActorIterator<ACastleMassEnemySpawner> It(World); It; ++It) { if (It->Configuration == Asset) { Match = *It; break; } }
					if (!Test->TestNotNull(TEXT("控制台实际创建对应生成器"), Match)) { return true; }
					Spawners.Add(Match);
					// 只为图像验收将已通过真实命令生成的单位排到相机前；之后完全交给自动处理器。
					for (int32 Member = 0; Member < Match->SpawnedUnits.Num(); ++Member)
					{
						const auto Entity = Match->SpawnedUnits[Member].Entity;
						Manager.GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(
							Center + FVector(2000, Index * 300 - 450 + Member * 35, 0));
						Manager.GetFragmentDataChecked<FMassVelocityFragment>(Entity).Value = FVector::ZeroVector;
					}
					if (Index == 3) { Test->TestTrue(TEXT("真实命令一次生成六至八只群虫"), Match->SpawnedUnits.Num() >= 6 && Match->SpawnedUnits.Num() <= 8); }
					else { Test->TestEqual(TEXT("非群虫命令默认一只"), Match->SpawnedUnits.Num(), 1); }
					const auto* Visual = CastChecked<UCastleMassHumanoidVisualizationTrait>(Asset->FindTrait(UCastleMassHumanoidVisualizationTrait::StaticClass()));
					Colors.Add(Visual->MaterialOverrides[0]);
					Handles.Append(Match->SpawnedUnits);
					if (Index == 0 && !Match->SpawnedUnits.IsEmpty()) { Giant = Match->SpawnedUnits[0]; }
				}
				Camera = World->SpawnActor<ACameraActor>(Center + FVector(2400, -1300, 650),
					(Center + FVector(1800, 0, 100) - (Center + FVector(2400, -1300, 650))).Rotation());
				Camera->GetCameraComponent()->SetFieldOfView(65.0f);
				Controller->SetViewTarget(Camera);
				NextCheck = FPlatformTime::Seconds() + 1.5;
				Stage = 1;
				return false;
			}
			if (FPlatformTime::Seconds() < NextCheck) { return false; }
			UCastleMassUnitSubsystem* Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
			FMassEntityManager& Manager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
			if (Stage == 1)
			{
				TSet<UMaterialInterface*> RenderedColors;
				int32 SpecterBodies = 0, SpecterSwords = 0;
				for (TObjectIterator<UInstancedSkinnedMeshComponent> It; It; ++It)
				{
					if (It->GetWorld() != World || It->GetInstanceCount() == 0 || !Colors.Contains(It->GetMaterial(0))) { continue; }
					Test->TestNotNull(TEXT("怪物批次已建立真实渲染代理"), It->GetSceneProxy());
					RenderedColors.Add(It->GetMaterial(0));
					const FString MeshName = It->GetSkinnedAsset()->GetName();
					if (MeshName == TEXT("SpecterSK") || MeshName == TEXT("SKM_SpecterSword"))
					{
						// 不能只验证剑存在：实际提供器必须驱动自身网格，且每个实例已收到移动动画轨道。
						const auto* Provider = Cast<UAnimSequenceTransformProviderDataInstance>(It->GetTransformProvider());
						if (!Test->TestNotNull(TEXT("主体与剑均使用实例动画提供器"), Provider)) { return true; }
						Test->TestTrue(TEXT("提供器网格与实际组件完全一致"), Provider->GetSkinnedAsset() == It->GetSkinnedAsset());
						for (int32 Instance = 0; Instance < It->GetInstanceCount(); ++Instance)
						{
							int32 Track = INDEX_NONE;
							Test->TestTrue(TEXT("主体与剑均已分配动画轨道"), It->GetInstanceAnimationIndex(It->GetInstanceId(Instance), Track));
							Test->TestEqual(TEXT("主体与剑同步使用移动序列"), Provider->GetSequenceIndex(Track, 0), 1);
						}
						if (MeshName == TEXT("SpecterSK")) { SpecterBodies += It->GetInstanceCount(); }
						else { SpecterSwords += It->GetInstanceCount(); }
					}
				}
				Test->TestEqual(TEXT("真实调度提交巨骸幼骸异色和原包材质批次"), RenderedColors.Num(), 3);
				Test->TestEqual(TEXT("每只群虫都有一把参与真实渲染的剑"), SpecterSwords, Spawners[3]->SpawnedUnits.Num());
				Test->TestEqual(TEXT("主体与剑实例数量一致"), SpecterBodies, SpecterSwords);
				for (const auto& Handle : Handles)
				{
					Test->TestTrue(TEXT("刷怪实体仍存活"), Units->IsUnitValid(Handle));
					if (Units->IsUnitValid(Handle)) { Test->TestNull(TEXT("每只敌人均无独立 Actor"), Manager.GetFragmentDataChecked<FMassActorFragment>(Handle.Entity).Get()); }
				}
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("moving_castle_w2_17/ConcreteEnemies.png")), false, false);
				// 截图在本帧末执行，必须等它完成后才触发反伤，否则巨骸会先被回收而缺席截图。
				NextCheck = FPlatformTime::Seconds() + 0.4;
				Stage = 2;
				return false;
			}
			if (Stage == 2)
			{
				FCastleMassUnitSnapshot Snapshot;
				if (!Test->TestTrue(TEXT("巨骸可查询"), Units->GetUnitSnapshot(Giant, Snapshot))) { return true; }
				World->GetSubsystem<UCastleShieldTowerSubsystem>()->UpsertActiveShield(Controller, Snapshot.Transform.GetLocation(), 1500, 1, 200);
				NextCheck = FPlatformTime::Seconds() + 1.0;
				Stage = 3;
				return false;
			}
			if (Stage == 3)
			{
			Test->TestFalse(TEXT("真实调度按序完成反伤死亡与巨骸回收"), Units->IsUnitValid(Giant));
			int32 Pups = 0;
			FMassEntityQuery Query(Manager.AsShared());
			Query.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All);
			Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
			FMassExecutionContext Context(Manager, 0, false);
			Query.ForEachEntityChunk(Context, [&Pups, this](FMassExecutionContext& Chunk)
			{
				const auto Data = Chunk.GetFragmentView<FCastleMassUnitFragment>();
				for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
				{
					if (Data[It].Health == 20.0f)
					{
						++Pups;
						auto& Handle = Children.AddDefaulted_GetRef();
						Handle.World = World;
						Handle.Entity = Chunk.GetEntity(It);
					}
				}
			});
			Test->TestEqual(TEXT("一只原有幼骸加六只自动分裂幼骸"), Pups, 7);
			World->GetSubsystem<UCastleShieldTowerSubsystem>()->DeactivateShield(Controller);
			FVector Center;
			if (!Test->TestTrue(TEXT("激光渲染验收仍有存活主塔"), World->GetSubsystem<UCastleMainTowerSubsystem>()->GetMainTowerTargetLocation(Center))) { return true; }
			// 将已经正常落地的七只幼骸排入主塔射程，之后由真实调度建立光束和攻击动画。
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				Manager.GetFragmentDataChecked<FTransformFragment>(Children[Index].Entity).GetMutableTransform().SetLocation(Center + FVector(600, Index * 60 - 180, 0));
			}
			Camera->SetActorLocation(Center + FVector(1100,-1000,600));
			Camera->SetActorRotation((Center + FVector(300,0,100) - Camera->GetActorLocation()).Rotation());
			NextCheck = FPlatformTime::Seconds() + 0.5;
			Stage = 4;
			return false;
			}
			if (Stage == 4)
			{
				Test->TestEqual(TEXT("真实调度为七只幼骸显示七束激光"), World->GetSubsystem<UCastleMassEnemyLaserBeamSubsystem>()->GetActiveBeamCount(), 7);
				int32 RenderedBeams = 0;
				for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
				{
					if (It->GetWorld() != World || !It->GetStaticMesh() || It->GetStaticMesh()->GetName() != TEXT("SM_EnemyLaserBeam")) { continue; }
					RenderedBeams += It->GetInstanceCount();
					Test->TestNotNull(TEXT("激光批次已建立真实渲染代理"), It->GetSceneProxy());
				}
				Test->TestEqual(TEXT("七束激光均进入真实渲染批次"), RenderedBeams, 7);
				for (const auto& Child : Children)
				{
					if (!Test->TestTrue(TEXT("激光幼骸仍存活"), Units->IsUnitValid(Child))) { return true; }
					Test->TestTrue(TEXT("幼骸已进入持续攻击态"), Manager.GetFragmentDataChecked<FCastleMassEnemyLaserAttackFragment>(Child.Entity).bInAttackRange);
					Test->TestEqual(TEXT("幼骸实际提交02RPT攻击序列"), Manager.GetFragmentDataChecked<FMassRepresentationAnimationFragment>(Child.Entity).AnimData.SequenceIndex, 3);
				}
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("moving_castle_w2_17/EnemyLasers.png")), false, false);
				NextCheck = FPlatformTime::Seconds() + 0.3;
				Stage = 5;
				return false;
			}
			if (Stage == 5)
			{
				for (const auto& Child : Children) { Units->ApplyImmediateDamageToEnemy(Child, 1000); }
				NextCheck = FPlatformTime::Seconds() + 0.2;
				Stage = 6;
				return false;
			}
			Test->TestEqual(TEXT("所有幼骸死亡后真实渲染光束全部清空"), World->GetSubsystem<UCastleMassEnemyLaserBeamSubsystem>()->GetActiveBeamCount(), 0);
			for (ACastleMassEnemySpawner* Spawner : Spawners) { Spawner->Destroy(); }
			Camera->Destroy();
			return true;
		}
	private:
		FAutomationTestBase* Test;
		UWorld* World = nullptr;
		ACastlePlayerController* Controller = nullptr;
		ACameraActor* Camera = nullptr;
		TArray<ACastleMassEnemySpawner*> Spawners;
		TArray<FCastleMassUnitHandle> Handles, Children;
		TSet<UMaterialInterface*> Colors;
		FCastleMassUnitHandle Giant;
		double NextCheck = 0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleConcreteEnemyRuntimeTest, "MovingCastle.MassDemo.ConcreteEnemies",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleConcreteEnemyRuntimeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleConcreteRuntimeTests::FConcreteEnemiesCommand(this));
	return true;
}

#endif
