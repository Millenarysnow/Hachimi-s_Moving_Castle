#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastlePlayerController.h"
#include "CastleMainTowerComponent.h"
#include "CastleMassEnemySpawner.h"
#include "CastleMassFragments.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedSkinnedMeshComponent.h"
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
	/** 在已有棋盘的 TestMap1 中验收控制台、四色骨骼批次和反伤分裂，不手工调用 Processor。 */
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
				for (TObjectIterator<UInstancedSkinnedMeshComponent> It; It; ++It)
				{
					if (It->GetWorld() != World || It->GetInstanceCount() == 0 || !Colors.Contains(It->GetMaterial(0))) { continue; }
					Test->TestNotNull(TEXT("四色批次已建立真实渲染代理"), It->GetSceneProxy());
					Test->TestTrue(TEXT("两个可见材质槽均使用颜色覆盖"), It->GetMaterial(0) == It->GetMaterial(1));
					RenderedColors.Add(It->GetMaterial(0));
				}
				Test->TestEqual(TEXT("真实调度提交四种独立颜色批次"), RenderedColors.Num(), 4);
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
			Test->TestEqual(TEXT("一只原有幼骸加三只自动分裂幼骸"), Pups, 4);
			World->GetSubsystem<UCastleShieldTowerSubsystem>()->DeactivateShield(Controller);
			Units->DespawnUnits(Children);
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
