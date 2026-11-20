#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleMassSpawner.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleNavigationSubsystem.h"
#include "Demo/CastleNavigationDemo.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Interaction/CastlePatrolWaypointMarker.h"
#include "Visualization/CastlePatrolPathVisualizationSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/OutputDeviceNull.h"
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
				Drawing = World->GetSubsystem<UCastlePatrolPathVisualizationSubsystem>();
				if (!Test->TestNotNull(TEXT("游戏世界有巡逻线可视化子系统"), Drawing)) { return true; }
				Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
				Controller = World->GetFirstPlayerController();
			}
			FCastleMassUnitSnapshot Snapshot;
			switch (Stage)
			{
			case 0:
				if (!Navigation->GetSharedPath().bValid || Spawner->SpawnedUnits.Num() != 32) { return false; }
				Test->TestFalse(TEXT("新游戏世界的巡逻线初始关闭"), Drawing->IsPathDrawingEnabled());
				FindLineComponent(0);
				InitialWaypoints = Navigation->GetRequestedWaypoints();
				Test->AddExpectedMessagePlain(TEXT("Castle.ShowPatrolPath：只接受 0 或 1"), ELogVerbosity::Error);
				ExecDrawing(TEXT("Castle.ShowPatrolPath 2"));
				Test->TestFalse(TEXT("非法参数不能改变开关"), Drawing->IsPathDrawingEnabled());
				if (!ToggleDrawing(true)) { return true; }
				CheckLineGeometry();
				CheckMarkers(4);
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
				// 在线路和 NPC 均就绪时关闭，验证渲染被移除但编号路点和共享路线都不变。
				if (!ToggleDrawing(false)) { return true; }
				FindLineComponent(0);
				CheckMarkers(4);
				Units->GetUnitSnapshot(Spawner->SpawnedUnits[0], Snapshot);
				PositionBeforeHidden = Snapshot.Transform.GetLocation();
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Navigation/MassPatrolHidden.png"), true, false);
				ReadyTime = World->GetTimeSeconds();
				Stage = 12;
				return false;
			case 12:
				if (World->GetTimeSeconds() - ReadyTime < 0.5) { return false; }
				Units->GetUnitSnapshot(Spawner->SpawnedUnits[0], Snapshot);
				Test->TestTrue(TEXT("关闭线条后 NPC 仍沿原路线移动"), Snapshot.bFollowingSharedRoute && !Snapshot.bPatrolBlocked
					&& !Snapshot.Transform.GetLocation().Equals(PositionBeforeHidden));
				if (!ToggleDrawing(true)) { return true; }
				CheckLineGeometry();
				Press(EKeys::P);
				Stage = 2;
				return false;
			case 2:
				if (!Test->TestTrue(TEXT("P 通过真实输入栈进入编辑"), Route->IsEditing())) { return true; }
				Press(EKeys::BackSpace);
				Stage = 3;
				return false;
			case 3:
				CheckMarkers(3);
				Test->TestEqual(TEXT("草稿撤回不执行任何寻路"), Navigation->PathQueryCount, 4);
				Press(EKeys::P);
				Stage = 4;
				return false;
			case 4:
				CheckMarkers(4);
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
				CheckMarkers(0);
				Test->TestFalse(TEXT("Delete 立即清空共享路线"), Navigation->GetSharedPath().bValid);
				FindLineComponent(0);
				if (!ToggleDrawing(false) || !ToggleDrawing(true)) { return true; }
				FindLineComponent(0);
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
				CheckMarkers(1);
				Press(EKeys::Enter);
				Stage = 9;
				return false;
			case 9:
				if (!Test->TestTrue(TEXT("左键碰撞落点和 Enter 正式发布单点路线"), Navigation->GetSharedPath().bValid && Navigation->GetSharedPath().Waypoints.Num() == 1)) { return true; }
				CheckMarkers(1);
				Test->TestFalse(TEXT("提交后退出编辑"), Route->IsEditing());
				Test->TestEqual(TEXT("单点待命没有多余查询"), Navigation->PathQueryCount, 4);
				FindLineComponent(0);
				Spawner->SpawnUnits();
				Stage = 10;
				return false;
			case 10:
				if (!Test->TestEqual(TEXT("可以沿玩家新路线重新生成"), Spawner->SpawnedUnits.Num(), 1)) { return true; }
				Units->GetUnitSnapshot(Spawner->SpawnedUnits[0], Snapshot);
				Test->TestTrue(TEXT("单点 NPC 可见状态为巡逻待命"), Snapshot.bFollowingSharedRoute && !Snapshot.bPatrolBlocked && Snapshot.Velocity.IsNearlyZero());
				Route->bShowRoute = false;
				Stage = 20;
				return false;
			case 20:
				CheckMarkers(0);
				Test->TestTrue(TEXT("隐藏标记不清空导航路线"), Navigation->GetSharedPath().bValid);
				Route->bShowRoute = true;
				Stage = 21;
				return false;
			case 21:
			{
				CheckMarkers(1);
				FText Failure;
				// 不再次开关：等待状态要随着真实的闭环重新发布，自动画出全部路段。
				if (!Test->TestTrue(TEXT("重新提交原来的多点闭环"), Route->SetWorldWaypoints(InitialWaypoints, Failure)))
				{
					Test->AddError(Failure.ToString());
					return true;
				}
				Stage = 22;
				return false;
			}
			case 22:
				if (!Navigation->GetSharedPath().bValid) { return false; }
				CheckMarkers(4);
				CheckLineGeometry();
				// 专用测试世界即将结束；销毁入口必须当帧清除标记和正式路线对应的线条。
				Test->TestTrue(TEXT("巡逻入口可销毁"), Route->Destroy());
				CheckMarkers(0);
				FindLineComponent(0);
				return true;
			}
			return true;
		}
	private:
		void ExecDrawing(const TCHAR* Command)
		{
			FOutputDeviceNull Output;
			Test->TestTrue(TEXT("真实的世界级控制台命令已注册"), IConsoleManager::Get().ProcessUserConsoleInput(Command, Output, World));
		}

		bool ToggleDrawing(bool bEnabled)
		{
			const auto Before = Navigation->AcquireSharedPath();
			const int32 Queries = Navigation->PathQueryCount;
			ExecDrawing(bEnabled ? TEXT("Castle.ShowPatrolPath 1") : TEXT("Castle.ShowPatrolPath 0"));
			Test->TestTrue(TEXT("绘制命令不替换共享路线快照"), Navigation->AcquireSharedPath() == Before);
			Test->TestEqual(TEXT("绘制命令不增加导航查询"), Navigation->PathQueryCount, Queries);
			return Test->TestEqual(TEXT("命令设置当前世界的绘制状态"), Drawing->IsPathDrawingEnabled(), bEnabled);
		}

		UInstancedStaticMeshComponent* FindLineComponent(int32 ExpectedCount)
		{
			int32 Count = 0;
			UInstancedStaticMeshComponent* Line = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				auto* Candidate = Cast<UInstancedStaticMeshComponent>(It->GetRootComponent());
				if (Candidate && Candidate->GetFName() == TEXT("CastlePatrolPathSegments"))
				{
					++Count;
					Line = Candidate;
				}
			}
			Test->TestEqual(TEXT("同一世界只拥有预期数量的巡逻线组件"), Count, ExpectedCount);
			return Line;
		}

		void CheckLineGeometry()
		{
			const auto Path = Navigation->AcquireSharedPath();
			auto* Line = FindLineComponent(1);
			if (!Test->TestNotNull(TEXT("正式巡逻线有实例组件"), Line) || !Test->TestNotNull(TEXT("线条网格已加载"), Line->GetStaticMesh().Get())) { return; }
			Test->TestTrue(TEXT("线组件已注册且不参与导航或碰撞"), Line->IsRegistered()
				&& !Line->CanEverAffectNavigation() && Line->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
			Test->TestEqual(TEXT("线条使用工具包圆柱网格"), Line->GetStaticMesh()->GetName(), FString(TEXT("SM_Cylinder_Line")));
			Test->TestTrue(TEXT("线条使用项目的青绿实心材质"), Line->GetMaterial(0) && Line->GetMaterial(0)->GetFName() == TEXT("MI_CastlePatrolLine"));
			if (!Test->TestTrue(TEXT("取得有效的闭环"), Path && Path->bValid && Path->bClosedLoop && Path->PatrolPoints.Num() > 2)
				|| !Test->TestEqual(TEXT("每条实际采样线段（包括返程）都有一个实例"), Line->GetInstanceCount(), Path->PatrolPoints.Num() - 1)) { return; }

			// 这张演示用楼梯而非垂直 NavLink；直接验证高低层样本，不依赖梯子专用的距离区间。
			Test->TestTrue(TEXT("演示闭环确实跨楼层"), Path->PatrolPoints.ContainsByPredicate([&Path](const FVector& Point)
			{
				return FMath::Abs(Point.Z - Path->PatrolPoints[0].Z) > 100.0;
			}));

			// 用原网格的轴向端点反解实例，验证中心不是被放到世界原点、错误楼层或直线抄近路。
			const FBoxSphereBounds Bounds = Line->GetStaticMesh()->GetBounds();
			const FVector AxisRadius(Bounds.BoxExtent.X, 0, 0);
			const FVector Height(0, 0, 12.0);
			for (int32 Index = 0; Index < Line->GetInstanceCount(); ++Index)
			{
				FTransform Transform;
				if (!Test->TestTrue(FString::Printf(TEXT("线段 %d 的两端对齐实际 NPC 路线"), Index + 1),
					Line->GetInstanceTransform(Index, Transform, true)
					&& Transform.TransformPosition(Bounds.Origin - AxisRadius).Equals(Path->PatrolPoints[Index] + Height, 0.01)
					&& Transform.TransformPosition(Bounds.Origin + AxisRadius).Equals(Path->PatrolPoints[Index + 1] + Height, 0.01))) { return; }
			}
		}

		void CheckMarkers(int32 ExpectedCount)
		{
			// 直接读取当前世界里的正式蓝图实例，验证制作、撤回和提交没有叠出两套标记。
			TSet<int32> Numbers;
			int32 Count = 0;
			const TArray<FVector>& Points = Navigation->GetRequestedWaypoints();
			for (TActorIterator<ACastlePatrolWaypointMarker> It(World); It; ++It)
			{
				++Count;
				const int32 Index = It->GetDisplayedNumber() - 1;
				Numbers.Add(Index + 1);
				Test->TestTrue(TEXT("标记归属当前巡逻入口且使用正式蓝图"), It->GetOwner() == Route
					&& It->GetClass()->GetFName() == TEXT("BP_CastlePatrolWaypointMarker_C"));
				if (!Route->IsEditing() && Points.IsValidIndex(Index))
				{
					Test->TestTrue(TEXT("正式标记枢轴与导航请求点一致，悬浮不改站立高度"), It->GetActorLocation().Equals(Points[Index]));
				}
			}
			Test->TestEqual(TEXT("标记数与当前显示路径点数相同"), Count, ExpectedCount);
			for (int32 Number = 1; Number <= ExpectedCount; ++Number)
			{
				Test->TestTrue(TEXT("标记从 1 按巡逻顺序连续编号"), Numbers.Contains(Number));
			}
		}

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
		FVector PositionBeforeHidden;
		TArray<FVector> InitialWaypoints;
		UWorld* World = nullptr;
		ACastlePatrolRoute* Route = nullptr;
		ACastleMassSpawner* Spawner = nullptr;
		ACastleNavigationDemo* Demo = nullptr;
		APlayerController* Controller = nullptr;
		UCastleNavigationSubsystem* Navigation = nullptr;
		UCastleMassUnitSubsystem* Units = nullptr;
		UCastlePatrolPathVisualizationSubsystem* Drawing = nullptr;
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
