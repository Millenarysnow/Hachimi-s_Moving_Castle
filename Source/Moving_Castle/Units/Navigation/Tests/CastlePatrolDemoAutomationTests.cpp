#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleMassSpawner.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleNavigationSubsystem.h"
#include "Demo/CastleNavigationDemo.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace CastlePatrolDemoTests
{
	/** 真正使用本地玩家输入栈验证 P/左键/Enter/撤回/清空，并检查实际骨骼 NPC 的运行状态。 */
	class FDemoCommand : public IAutomationLatentCommand
	{
	public:
		explicit FDemoCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("可见 Mass 巡逻演示或输入测试超时。")); return true; }
			if (!World)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
				}
				if (!World) { return false; }
				for (TActorIterator<ACastlePatrolRoute> It(World); It; ++It) { Route = *It; break; }
				for (TActorIterator<ACastleMassSpawner> It(World); It; ++It) { Spawner = *It; break; }
				for (TActorIterator<ACastleNavigationDemo> It(World); It; ++It) { Demo = *It; break; }
				if (!Test->TestTrue(TEXT("已加载包含路径点、真实楼梯和 NPC 生成器的新演示"), Route && Spawner && Demo)) { return true; }
				Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
				Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
				Controller = World->GetFirstPlayerController();
			}
			FCastleMassUnitSnapshot Snapshot;
			switch (Stage)
			{
			case 0:
				if (!Navigation->GetSharedPath().bValid || Spawner->SpawnedUnits.Num() != 32) { return false; }
				Test->TestEqual(TEXT("四个点一次算好完整四段"), Navigation->PathQueryCount, 4);
				Test->TestEqual(TEXT("飞入整轮只构建一次 NavMesh"), Navigation->RefreshCount, 1);
				Units->GetUnitSnapshot(Spawner->SpawnedUnits[0], Snapshot);
				InitialPosition = Snapshot.Transform.GetLocation();
				ReadyTime = World->GetTimeSeconds();
				Stage = 1;
				return false;
			case 1:
				if (World->GetTimeSeconds() - ReadyTime < 1.0) { return false; }
				Units->GetUnitSnapshot(Spawner->SpawnedUnits[0], Snapshot);
				Test->TestTrue(TEXT("实际 NPC 沿共享路线移动"), !Snapshot.Transform.GetLocation().Equals(InitialPosition) && Snapshot.bFollowingSharedRoute && !Snapshot.bPatrolBlocked);
				Test->TestEqual(TEXT("持续模拟没有逐 NPC 查询"), Navigation->PathQueryCount, 4);
				IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("Navigation")), true);
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Navigation/MassPatrol.png"), true, false);
				ReadyTime = World->GetTimeSeconds();
				Stage = 11;
				return false;
			case 11:
				if (World->GetTimeSeconds() - ReadyTime < 0.5) { return false; }
				Press(EKeys::P);
				Stage = 2;
				return false;
			case 2:
				if (!Test->TestTrue(TEXT("P 通过真实输入栈进入编辑"), Route->IsEditing())) { return true; }
				Press(EKeys::BackSpace);
				Stage = 3;
				return false;
			case 3:
				Test->TestEqual(TEXT("草稿撤回不执行任何寻路"), Navigation->PathQueryCount, 4);
				Press(EKeys::P);
				Stage = 4;
				return false;
			case 4:
				Test->TestFalse(TEXT("P 退出编辑并释放左键"), Route->IsEditing());
				Test->TestEqual(TEXT("取消草稿保留四点完整路线"), Navigation->GetSharedPath().Waypoints.Num(), 4);
				Press(EKeys::P);
				Stage = 5;
				return false;
			case 5:
				Press(EKeys::Delete);
				Stage = 6;
				return false;
			case 6:
				Test->TestFalse(TEXT("Delete 立即清空共享路线"), Navigation->GetSharedPath().bValid);
				// 玩家输入可能晚于本帧 Mass 阶段，等待下一轮模拟消费失效快照再检查真实速度。
				Stage = 61;
				return false;
			case 61:
				Units->GetUnitSnapshot(Spawner->SpawnedUnits[0], Snapshot);
				Test->TestTrue(TEXT("清空后 NPC 实际停速"), Snapshot.Velocity.IsNearlyZero());
				Spawner->DespawnUnits();
				Spawner->Count = 1;
				// 将视口中心精确对准实际下层地面，左键仍由正式碰撞射线与输入绑定处理。
				Demo->DemoCamera->SetWorldLocation(FVector(-1200, -300, 800));
				Demo->DemoCamera->SetWorldRotation(FRotator(-90, 0, 0));
				Controller->SetViewTarget(Demo);
				Stage = 7;
				return false;
			case 7:
				Press(EKeys::LeftMouseButton);
				Stage = 8;
				return false;
			case 8:
				Press(EKeys::Enter);
				Stage = 9;
				return false;
			case 9:
				if (!Test->TestTrue(TEXT("左键碰撞落点和 Enter 正式发布单点路线"), Navigation->GetSharedPath().bValid && Navigation->GetSharedPath().Waypoints.Num() == 1)) { return true; }
				Test->TestFalse(TEXT("提交后退出编辑"), Route->IsEditing());
				Test->TestEqual(TEXT("单点待命没有多余查询"), Navigation->PathQueryCount, 4);
				Spawner->SpawnUnits();
				Stage = 10;
				return false;
			case 10:
				if (!Test->TestEqual(TEXT("可以沿玩家新路线重新生成"), Spawner->SpawnedUnits.Num(), 1)) { return true; }
				Units->GetUnitSnapshot(Spawner->SpawnedUnits[0], Snapshot);
				Test->TestTrue(TEXT("单点 NPC 可见状态为巡逻待命"), Snapshot.bFollowingSharedRoute && !Snapshot.bPatrolBlocked && Snapshot.Velocity.IsNearlyZero());
				return true;
			}
			return true;
		}
	private:
		void Press(FKey Key)
		{
			Controller->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Key, IE_Pressed, FPlatformTime::Cycles64()));
			Controller->InputKey(FInputKeyEventArgs(nullptr, INPUTDEVICEID_NONE, Key, IE_Released, FPlatformTime::Cycles64()));
		}
		FAutomationTestBase* Test;
		double Deadline = FPlatformTime::Seconds() + 50.0;
		double ReadyTime = 0.0;
		int32 Stage = 0;
		FVector InitialPosition;
		UWorld* World = nullptr;
		ACastlePatrolRoute* Route = nullptr;
		ACastleMassSpawner* Spawner = nullptr;
		ACastleNavigationDemo* Demo = nullptr;
		APlayerController* Controller = nullptr;
		UCastleNavigationSubsystem* Navigation = nullptr;
		UCastleMassUnitSubsystem* Units = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePatrolDemoTest, "MovingCastle.PatrolDemo.Runtime",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastlePatrolDemoTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastlePatrolDemoTests::FDemoCommand(this));
	return true;
}

#endif
