#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleMassSpawner.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleNavigationSubsystem.h"
#include "Demo/CastleNavigationDemo.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Interaction/CastlePatrolWaypointMarker.h"
#include "Visualization/CastlePatrolPathVisualizationSubsystem.h"
#include "Algo/AllOf.h"
#include "Camera/CameraComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"
#include "Castle/Input/CastleControlMode.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/OutputDeviceNull.h"
#include "UnrealClient.h"

namespace CastlePatrolDemoTests
{
	/** 使用通用玩家控制器验证经典右键、路线快捷键和实际骨骼 NPC 的运行状态。 */
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
				if (!Test->TestNotNull(TEXT("独立巡逻演示有真实玩家控制器"), Controller)) { return true; }
			}
			FCastleMassUnitSnapshot Snapshot;
			switch (Stage)
			{
			case 0:
				if (!Navigation->GetSharedPath().bValid || Spawner->SpawnedUnits.Num() != 32) { return false; }
				Test->TestFalse(TEXT("新游戏世界的巡逻线初始关闭"), Drawing->IsPathDrawingEnabled());
				FindLineActor(0);
				InitialWaypoints = Navigation->GetRequestedWaypoints();
				Test->AddExpectedMessagePlain(TEXT("Castle.ShowPatrolPath：只接受 0 或 1"), ELogVerbosity::Error);
				ExecDrawing(TEXT("Castle.ShowPatrolPath 2"));
				Test->TestFalse(TEXT("非法参数不能改变开关"), Drawing->IsPathDrawingEnabled());
				if (!ToggleDrawing(true)) { return true; }
				CheckLineGeometry(true);
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
				// 保持相机位置不变再取一帧，供对比虚线是否在滚动而不是只画成了静态虚线。
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Navigation/MassPatrolAnimated.png"), true, false);
				ReadyTime = World->GetTimeSeconds();
				Stage = 13;
				return false;
			case 13:
				if (World->GetTimeSeconds() - ReadyTime < 0.25) { return false; }
				// 在线路和 NPC 均就绪时关闭，验证渲染被移除但编号路点和共享路线都不变。
				if (!ToggleDrawing(false)) { return true; }
				FindLineActor(0);
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
				CheckLineGeometry(true);
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
				Test->TestFalse(TEXT("P 退出编辑并释放主交互键"), Route->IsEditing());
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
				FindLineActor(0);
				if (!ToggleDrawing(false) || !ToggleDrawing(true)) { return true; }
				FindLineActor(0);
				// 玩家输入可能晚于本帧 Mass 阶段，等待下一轮模拟消费失效快照再检查真实速度。
				Stage = 61;
				return false;
			case 61:
				Units->GetUnitSnapshot(Spawner->SpawnedUnits[0], Snapshot);
				Test->TestTrue(TEXT("清空后 NPC 实际停速"), Snapshot.Velocity.IsNearlyZero());
				Spawner->DespawnUnits();
				Spawner->Count = 1;
				// 将视口中心精确对准下层地面，经典主交互仍由实际碰撞射线与输入绑定处理。
				Demo->DemoCamera->SetWorldLocation(FVector(-1200, -300, 800));
				Demo->DemoCamera->SetWorldRotation(FRotator(-90, 0, 0));
				Controller->SetViewTarget(Demo);
				Stage = 7;
				return false;
			case 7:
			{
				// 独立演示和城堡关卡共用经典语义映射，确认演示实际收到右键。
				const auto& Bindings = CastleControlInput::GetBindings(ECastleControlMode::Classic);
				const FKey& Key = Bindings.Get(ECastleWorldInputAction::PrimaryInteract);
				if (!Test->TestEqual(TEXT("独立巡逻演示使用经典右键放点"), Key, EKeys::RightMouseButton)) { return true; }
				Press(Key);
				Stage = 8;
				return false;
			}
			case 8:
				CheckMarkers(1);
				Press(EKeys::Enter);
				Stage = 9;
				return false;
			case 9:
				// 等待真实输入栈完成 Enter 派发后读取已发布的路线。
				if (Route->IsEditing()) { return false; }
				if (!Test->TestTrue(TEXT("右键碰撞落点和 Enter 正式发布单点路线"), Navigation->GetSharedPath().bValid && Navigation->GetSharedPath().Waypoints.Num() == 1)) { return true; }
				CheckMarkers(1);
				Test->TestFalse(TEXT("提交后退出编辑"), Route->IsEditing());
				Test->TestEqual(TEXT("单点待命没有多余查询"), Navigation->PathQueryCount, 4);
				FindLineActor(0);
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
				CheckLineGeometry(true);
				// 在同一块下层楼板提交确定原路折返的两点，重现两套虚线叠在一个轴线上的问题。
				return !SubmitRetrace(300.0, 23);
			case 23:
				if (!Navigation->GetSharedPath().bValid) { return false; }
				CheckMarkers(2);
				CheckLineGeometry(false);
				CheckRetracedLanes(false);
				// 短于采样步长时单程仅有一个采样段，不能因两端都接回中心而再次完全重合。
				return !SubmitRetrace(6.0, 24);
			case 24:
				if (!Navigation->GetSharedPath().bValid) { return false; }
				CheckMarkers(2);
				CheckRetracedLanes(true);
				// 专用测试世界即将结束；销毁入口必须当帧清除标记和正式路线对应的线条。
				Test->TestTrue(TEXT("巡逻入口可销毁"), Route->Destroy());
				CheckMarkers(0);
				FindLineActor(0);
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

		AActor* FindLineActor(int32 ExpectedCount)
		{
			int32 Count = 0;
			AActor* Line = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				const USceneComponent* Root = It->GetRootComponent();
				if (Root && Root->GetFName() == TEXT("CastlePatrolPathSegments"))
				{
					++Count;
					Line = *It;
				}
			}
			Test->TestEqual(TEXT("当前世界只拥有预期数量的巡逻线 Actor"), Count, ExpectedCount);
			return Line;
		}

		bool CheckDashMaterial(const USplineMeshComponent& Segment, double Start, double End)
		{
			// 不能通过变速、反转材质或逐段重播来掩盖两条线叠加；保持原材质，并按 NPC 行程衔接相位。
			const auto* Material = Cast<UMaterialInstanceDynamic>(Segment.GetMaterial(0));
			const auto ScalarEquals = [Material](FName Name, float Expected)
			{
				float Actual;
				return Material->GetScalarParameterValue(Name, Actual) && FMath::IsNearlyEqual(Actual, Expected);
			};
			return Test->TestTrue(TEXT("黄虚线沿用 PathTracer_08 材质与速度，且相位跨采样延续"), Material
				&& Material->GetMaterial()->GetFName() == TEXT("M_PT_DottedLine_Anim")
				&& ScalarEquals(TEXT("AnimSpeed"), -0.5f)
				&& ScalarEquals(TEXT("GapSize"), 0.1f)
				&& ScalarEquals(TEXT("ScaleUV"), static_cast<float>((End - Start) / 50.0))
				&& ScalarEquals(TEXT("Offset"), static_cast<float>(FMath::Fmod(Start / 50.0, 1.0))));
		}

		USplineMeshComponent* FindDash(AActor& Actor, int32 Index)
		{
			return FindObjectFast<USplineMeshComponent>(&Actor, FName(*FString::Printf(TEXT("CastlePatrolDash_%d"), Index)));
		}

		void CheckLineGeometry(bool bCrossesFloors)
		{
			const auto Path = Navigation->AcquireSharedPath();
			AActor* LineActor = FindLineActor(1);
			if (!Test->TestNotNull(TEXT("正式巡逻线有渲染 Actor"), LineActor)
				|| !Test->TestTrue(TEXT("取得有效的闭环"), Path && Path->bValid && Path->bClosedLoop && Path->PatrolPoints.Num() > 2)) { return; }
			TInlineComponentArray<USplineMeshComponent*> Segments(LineActor);
			if (!Test->TestEqual(TEXT("所有采样线段均显示，未丢弃任何返程"), Segments.Num(), Path->PatrolPoints.Num() - 1)) { return; }

			// 演示使用楼梯跨层；专门的折返用例都在同一楼板上。
			Test->TestEqual(TEXT("渲染前的真实样本符合用例中的楼层跨度"), Path->PatrolPoints.ContainsByPredicate([&Path](const FVector& Point)
			{
				return FMath::Abs(Point.Z - Path->PatrolPoints[0].Z) > 100.0;
			}), bCrossesFloors);

			const FVector Height(0, 0, 12.0);
			FVector FirstStart, PreviousEnd;
			for (int32 Index = 0; Index < Segments.Num(); ++Index)
			{
				const auto* Segment = FindDash(*LineActor, Index);
				if (!Test->TestNotNull(TEXT("每个采样线段都有对应的圆柱组件"), Segment)) { return; }
				Test->TestTrue(TEXT("线条使用原包圆柱且不参与导航或碰撞"), Segment->GetStaticMesh()
					&& Segment->GetStaticMesh()->GetFName() == TEXT("SM_Cylinder_Line") && Segment->IsRegistered()
					&& !Segment->CanEverAffectNavigation() && Segment->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
				// 显示可在折返附近向侧面错开一条半线宽，但不能离开实际楼层、楼梯高度或在相邻采样间留断口。
				const FTransform& Transform = Segment->GetComponentTransform();
				const FVector Start = Transform.TransformPosition(Segment->GetStartPosition());
				const FVector End = Transform.TransformPosition(Segment->GetEndPosition());
				const FVector StartOffset = Start - (Path->PatrolPoints[Index] + Height);
				const FVector EndOffset = End - (Path->PatrolPoints[Index + 1] + Height);
				if (!Test->TestTrue(FString::Printf(TEXT("线段 %d 贴着实际 NPC 路线，仅允许有限的水平侧移"), Index + 1),
					StartOffset.Size2D() < 13.0 && EndOffset.Size2D() < 13.0
					&& FMath::IsNearlyZero(StartOffset.Z, 0.01) && FMath::IsNearlyZero(EndOffset.Z, 0.01))) { return; }
				if (Index == 0) { FirstStart = Start; }
				else if (!Test->TestTrue(TEXT("显示端点连续通过每个采样接缝"), Start.Equals(PreviousEnd, 0.01))) { return; }
				PreviousEnd = End;
				if (!CheckDashMaterial(*Segment, Path->PatrolDistances[Index], Path->PatrolDistances[Index + 1])) { return; }
			}
			Test->TestTrue(TEXT("显示路线首尾闭合"), PreviousEnd.Equals(FirstStart, 0.01));
		}

		bool SubmitRetrace(double HalfLength, int32 NextStage)
		{
			FText Failure;
			if (!Route->SetWorldWaypoints({FVector(-1200, -HalfLength, 5), FVector(-1200, HalfLength, 5)}, Failure))
			{
				Test->AddError(FString::Printf(TEXT("无法提交测试折返路线：%s"), *Failure.ToString()));
				return false;
			}
			Stage = NextStage;
			return true;
		}

		void CheckRetracedLanes(bool bShort)
		{
			const auto Path = Navigation->AcquireSharedPath();
			AActor* LineActor = FindLineActor(1);
			if (!Test->TestNotNull(TEXT("折返的显示 Actor"), LineActor)
				|| !Test->TestTrue(TEXT("同楼层测试夹具只产生共线的原路折返"), Path && Path->Waypoints.Num() == 2
					&& Path->PatrolPoints.Num() == (bShort ? 3 : 61) && Path->PatrolPoints[0].Equals(Path->PatrolPoints.Last(), 0.01)
					&& Algo::AllOf(Path->PatrolPoints, [](const FVector& P) { return FMath::IsNearlyEqual(P.X, -1200.0, 0.01); }))) { return; }
			if (!bShort)
			{
				const auto* Outbound = FindDash(*LineActor, 15);
				const auto* Inbound = FindDash(*LineActor, 45);
				if (!Test->TestTrue(TEXT("原地重合的去返采样都保留"), Outbound && Inbound)) { return; }
				const FVector A = Outbound->GetComponentLocation();
				const FVector B = Inbound->GetComponentLocation();
				Test->TestTrue(TEXT("同一个源点分别在两个方向的右侧画线"), A.X < -1210.0 && B.X > -1190.0
					&& FMath::IsNearlyEqual(A.Y, B.Y, 0.01) && FMath::IsNearlyEqual(A.Z, B.Z, 0.01));
				Test->TestTrue(TEXT("去返两条线各自沿本段行进方向提供增长的 UV"),
					Outbound->GetComponentTransform().TransformPosition(Outbound->GetEndPosition()).Y > A.Y
					&& Inbound->GetComponentTransform().TransformPosition(Inbound->GetEndPosition()).Y < B.Y);
				return;
			}

			// 同一短段的两个方向各拆成两条渲染片，首尾仍在玩家点上，显示中点在相反两侧。
			TInlineComponentArray<USplineMeshComponent*> Segments(LineActor);
			if (!Test->TestEqual(TEXT("两个短采样各有两片连续的可见线"), Segments.Num(), 4)) { return; }
			const FVector Start = Path->PatrolPoints[0] + FVector(0, 0, 12.0);
			const FVector End = Path->PatrolPoints[1] + FVector(0, 0, 12.0);
			const FVector Center = (Start + End) * 0.5;
			const FVector Knots[] = {Start, Center + FVector(-12, 0, 0), End, Center + FVector(12, 0, 0), Start};
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const auto* Segment = FindDash(*LineActor, Index);
				if (!Test->TestNotNull(TEXT("存在短折返渲染片"), Segment)) { return; }
				Test->TestTrue(TEXT("短折返线片在两侧绕开，并在路点接回原轴线"), Segment->GetComponentLocation().Equals(Knots[Index], 0.1)
					&& Segment->GetComponentTransform().TransformPosition(Segment->GetEndPosition()).Equals(Knots[Index + 1], 0.1));
				const double HalfSpan = Path->PatrolDistances[1] * 0.5;
				if (!CheckDashMaterial(*Segment, Index * HalfSpan, (Index + 1) * HalfSpan)) { return; }
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
				// 用真实放点、撤回、清空流程验证每个编号都带恰好一个圆圈，且不依赖巡逻线开关。
				const auto* Circle = FindObjectFast<UStaticMeshComponent>(*It, TEXT("WaypointGroundCircle"));
				Test->TestTrue(TEXT("每个显示中的路点使用工具包的 04 圆圈，贴在该点地面且不参与导航或碰撞"), Circle
					&& Circle->IsVisible() && Circle->IsRegistered() && Circle->GetMaterial(0)
					&& Circle->GetMaterial(0)->GetFName() == TEXT("MI_PT_Circle_Preview_04")
					&& Circle->GetComponentLocation().Equals(It->GetActorLocation() + FVector(0, 0, 2.0), 0.01)
					&& !Circle->CanEverAffectNavigation() && Circle->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
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
