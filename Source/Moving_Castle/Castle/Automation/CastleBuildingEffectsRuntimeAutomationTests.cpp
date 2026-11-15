#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleBoard.h"
#include "CastleBuildingDefinition.h"
#include "CastleBuildings.h"
#include "CastleEconomyComponent.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleMassSpawner.h"
#include "CastleNavigationSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/Building/Effects/Attack/CastleCannonTowerAttackEffectComponent.h"
#include "Components/Building/Effects/Farm/CastleFarmEffectComponent.h"
#include "Components/Building/Effects/Shield/CastleShieldTowerEffectComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"
#include "GameFramework/PlayerController.h"
#include "MassEntityConfigAsset.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace CastleBuildingEffectsRuntimeTests
{
	/** TestMap1 中走正式放置、真实异步导航和自动 Mass 调度，禁止手工伪造进入事件或直调效果。 */
	class FPatrolCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPatrolCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("TestMap1 四类建筑巡逻联调超时。")); return Finish(); }
			if (Stage == 0)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
				}
				if (!Test->TestTrue(TEXT("在正式 TestMap1 游戏世界验收"), World && World->GetMapName().EndsWith(TEXT("TestMap1")))) { return Finish(); }
				for (TActorIterator<ACastleBoard> It(World); It; ++It) { Board = *It; break; }
				if (!Test->TestNotNull(TEXT("TestMap1 正式棋盘存在"), Board) || !Test->TestEqual(TEXT("测试开始时棋盘没有用户已摆放建筑"), Board->GetPlacedBuildingCount(), 0)) { return Finish(); }
				Player = World->GetFirstPlayerController();
				Economy = Player ? Player->FindComponentByClass<UCastleEconomyComponent>() : nullptr;
				if (!Test->TestNotNull(TEXT("正式控制器有金币账户"), Economy)) { return Finish(); }
				GoldBefore = Economy->GetGold();
				FCastleGroundGenerationResult Ground;
				for (int32 X = 0; X < Board->GridSize.X; ++X)
				{
					for (int32 Y = 0; Y < Board->GridSize.Y; ++Y)
					{
						FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
						Cell.Coordinate = Board->GridOrigin + FIntVector(X, Y, 0);
						Cell.Color = ECastleTileColor::Red;
						Cell.Number = 3;
					}
				}
				FText Reason;
				if (!Board->ApplyGroundGenerationResult(Ground, Reason)) { Test->AddError(Reason.ToString()); return Finish(); }
				// 验收专用输入平台提供连续导航面；它只属于临时游戏世界，不补入正式建筑蓝图。
				Floor = World->SpawnActor<AStaticMeshActor>(FVector(1400, 1400, -25), FRotator::ZeroRotator);
				Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				Floor->SetActorScale3D(FVector(35, 23, 0.5));
				Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
				Floor->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
				const ECastleBuildingType Types[] = {ECastleBuildingType::Farm, ECastleBuildingType::Farm, ECastleBuildingType::NormalRoom,
					ECastleBuildingType::CannonTower, ECastleBuildingType::ArcherTower, ECastleBuildingType::ShieldTower};
				const FIntVector Cells[] = {{1,1,0}, {1,2,0}, {2,1,0}, {2,2,0}, {3,1,0}, {4,1,0}};
				for (int32 Index = 0; Index < UE_ARRAY_COUNT(Types); ++Index)
				{
					FCastleBuildingDescriptor Card;
					auto* Definition = Board->FindBuildingDefinition(Types[Index]);
					if (!Definition || !Definition->CreateDescriptor(Types[Index] == ECastleBuildingType::NormalRoom ? 1 : 5, ECastleTileColor::Red, 3, Card, Reason))
					{
						Test->AddError(Reason.ToString()); return Finish();
					}
					ACastleBuildingBase* Building = Board->PlaceBuilding(Cells[Index], Card, false, Reason);
					if (!Building) { Test->AddError(Reason.ToString()); return Finish(); }
					Buildings.Add(Building);
				}
				Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
				Stage = 1;
				return false;
			}
			if (Stage == 1)
			{
				for (auto* Building : Buildings) { if (!Building->bHasCompletedPlacement) { return false; } }
				FText Reason;
				if (!Navigation->RequestSharedPatrolRoute({FVector(0,700,20), FVector(3000,700,20), FVector(3000,2200,20), FVector(0,2200,20)}, Reason))
				{
					Test->AddError(Reason.ToString()); return Finish();
				}
				Stage = 2;
				return false;
			}
			if (Stage == 2)
			{
				const auto Path = Navigation->AcquireSharedPath();
				if (!Navigation->LastFailure.IsEmpty()) { Test->AddError(Navigation->LastFailure.ToString()); return Finish(); }
				if (!Path || !Path->bValid) { return false; }
				FTransform Transform(FVector(0,700,20));
				Spawner = World->SpawnActorDeferred<ACastleMassSpawner>(ACastleMassSpawner::StaticClass(), Transform);
				Spawner->Configuration = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassNPCBase.DA_MassNPCBase"));
				Spawner->Count = 1;
				Spawner->bFollowSharedPatrolRoute = true;
				Spawner->FinishSpawning(Transform);
				if (!Test->TestEqual(TEXT("正式配置生成一个沿路线巡逻的NPC"), Spawner->SpawnedUnits.Num(), 1)) { return Finish(); }
				Camera = World->SpawnActor<ACameraActor>(FVector(5000,-6500,6000), (FVector(1500,1100,600)-FVector(5000,-6500,6000)).Rotation());
				Camera->GetCameraComponent()->SetFieldOfView(65);
				Player->SetViewTarget(Camera);
				Stage = 3;
				return false;
			}
			if (Stage == 3)
			{
				for (int32 Index : {0,2,3,4,5}) { if (Buildings[Index]->BuildingEffect->TriggerCount == 0) { return false; } }
				Test->TestTrue(TEXT("真实巡逻依次进入农田、普通房间、箭塔和盾塔"), Buildings[0]->BuildingEffect->LastTriggerContext.Source == ECastleBuildingTriggerSource::NPCEntry
					&& Buildings[2]->BuildingEffect->LastTriggerContext.Source == ECastleBuildingTriggerSource::NPCEntry
					&& Buildings[4]->BuildingEffect->LastTriggerContext.Source == ECastleBuildingTriggerSource::NPCEntry);
				Test->TestTrue(TEXT("农田同色连通实际给正式控制器金币"), Economy->GetGold() >= GoldBefore + 6);
				float Bonus = 0;
				Test->TestTrue(TEXT("真实巡逻NPC已携带五级农田增益"), World->GetSubsystem<UCastleMassUnitSubsystem>()->GetNPCFarmBuff(Spawner->SpawnedUnits[0], Bonus) && Bonus >= 0.3f);
				Test->TestTrue(TEXT("普通房间弹药库进入实际联动炮塔"), Buildings[3]->BuildingEffect->TriggerCount > 0);
				auto* Shield = CastChecked<ACastleShieldTowerBuilding>(Buildings[5]);
				FVector2D TextScreen;
				Player->ProjectWorldLocationToScreen(Shield->ShieldRemainingText->GetComponentLocation(), TextScreen);
				Test->AddInfo(FString::Printf(TEXT("护盾文字渲染：注册=%d，代理=%d，字号=%.1f，字体=%s，材质=%s，屏幕=%s。"),
					Shield->ShieldRemainingText->IsRegistered(), Shield->ShieldRemainingText->GetSceneProxy() != nullptr,
					Shield->ShieldRemainingText->WorldSize, *GetPathNameSafe(Shield->ShieldRemainingText->Font),
					*GetPathNameSafe(Shield->ShieldRemainingText->GetMaterial(0)), *TextScreen.ToString()));
				Test->TestTrue(TEXT("真实进入后蓝色球体及剩余文字可见"), Shield->ShieldBubble->IsVisible() && CastChecked<UCastleShieldTowerEffectComponent>(Shield->BuildingEffect)->RemainingBlocks > 0);
				ReadyToFinish = FPlatformTime::Seconds() + 0.5;
				Stage = 4;
				return false;
			}
			if (Stage == 4)
			{
				// TextRender 首次更新会重建渲染代理；等待其提交后再截图，不能只验证逻辑可见标志。
				if (FPlatformTime::Seconds() < ReadyToFinish) { return false; }
				auto* Shield = CastChecked<ACastleShieldTowerBuilding>(Buildings[5]);
				Test->AddInfo(FString::Printf(TEXT("护盾次数文字：%s，位置=%s，旋转=%s，隐藏=%d，可见=%d。"),
					*Shield->ShieldRemainingText->Text.ToString(), *Shield->ShieldRemainingText->GetComponentLocation().ToString(),
					*Shield->ShieldRemainingText->GetComponentRotation().ToString(), Shield->ShieldRemainingText->bHiddenInGame, Shield->ShieldRemainingText->IsVisible()));
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("moving_castle_w3_18/TestMap1-Patrol.png")), false, false);
				ReadyToFinish = FPlatformTime::Seconds() + 0.5;
				Stage = 5;
				return false;
			}
			if (Stage == 5)
			{
				if (FPlatformTime::Seconds() < ReadyToFinish) { return false; }
				// 全景包含相邻大型美术，另保留正俯视近景，验证真实字形而非只读可见属性。
				auto* Shield = CastChecked<ACastleShieldTowerBuilding>(Buildings[5]);
				Camera->SetActorLocation(Shield->ShieldRemainingText->GetComponentLocation() + FVector(0,0,1800));
				Camera->SetActorRotation(FRotator(-90,180,0));
				Camera->GetCameraComponent()->SetFieldOfView(35);
				ReadyToFinish = FPlatformTime::Seconds() + 0.5;
				Stage = 6;
				return false;
			}
			if (Stage == 6)
			{
				if (FPlatformTime::Seconds() < ReadyToFinish) { return false; }
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("moving_castle_w3_18/TestMap1-Counter-Closeup.png")), false, false);
				ReadyToFinish = FPlatformTime::Seconds() + 0.5;
				Stage = 7;
				return false;
			}
			return FPlatformTime::Seconds() < ReadyToFinish ? false : Finish();
		}

	private:
		/** 无论断言失败或成功，都只清理本测试创建的对象；运行场景本身不保存到资产。 */
		bool Finish()
		{
			if (IsValid(Spawner)) { Spawner->Destroy(); }
			if (Navigation) { Navigation->ClearSharedRoute(); }
			for (auto* Building : Buildings) { if (IsValid(Building)) { Building->Destroy(); } }
			if (IsValid(Floor)) { Floor->Destroy(); }
			if (IsValid(Camera)) { Camera->Destroy(); }
			return true;
		}
		FAutomationTestBase* Test;
		UWorld* World = nullptr;
		ACastleBoard* Board = nullptr;
		UCastleEconomyComponent* Economy = nullptr;
		UCastleNavigationSubsystem* Navigation = nullptr;
		APlayerController* Player = nullptr;
		AStaticMeshActor* Floor = nullptr;
		ACastleMassSpawner* Spawner = nullptr;
		ACameraActor* Camera = nullptr;
		TArray<ACastleBuildingBase*> Buildings;
		double Deadline = FPlatformTime::Seconds() + 120;
		double ReadyToFinish = 0;
		int32 Stage = 0;
		int32 GoldBefore = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingEffectsPatrolRuntimeTest, "MovingCastle.BuildingEffectsRuntime.TestMap1Patrol",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingEffectsPatrolRuntimeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleBuildingEffectsRuntimeTests::FPatrolCommand(this));
	return true;
}

#endif
