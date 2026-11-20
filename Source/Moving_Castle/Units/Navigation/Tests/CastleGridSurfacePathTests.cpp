#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "BuildingNavigationActor.h"
#include "Castle/Actors/CastleBoard.h"
#include "CastleNavigationSubsystem.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Demo/CastleNavigationDemo.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"

namespace CastleGridPatrolTests
{
	/** 在演示地图的运行时世界中，用真实功能碰撞和 Recast 验证 L 形格路；不保存地图修改。 */
	class FCornerObstacle final : public IAutomationLatentCommand
	{
	public:
		explicit FCornerObstacle(FAutomationTestBase* InTest) : Test(InTest) {}

		bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(TEXT("格心巡逻：等待实际 NavMesh 刷新超时。"));
				return Finish();
			}
			if (!Demo)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType != EWorldType::Game) { continue; }
					for (TActorIterator<ACastleNavigationDemo> It(Context.World()); It; ++It) { Demo = *It; break; }
				}
				if (!Test->TestNotNull(TEXT("启动 L_NPCNavigationDemo 的独立游戏实例"), Demo)) { return true; }
				World = Demo->GetWorld();
				Nav = World->GetSubsystem<UCastleNavigationSubsystem>();
				System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			}
			if (Stage == 0)
			{
				if (Demo->Phase < 0) { Test->AddError(TEXT("导航演示未能初始化。")); return true; }
				if (Demo->Phase != 1 || !Nav->IsNavigationReady()) { return false; }
				return BuildFixture() ? false : Finish();
			}
			if (!Nav->IsNavigationReady()) { return false; }
			const auto Path = Nav->AcquireSharedPath();
			if (!Test->TestTrue(TEXT("已用真实几何发布完整闭环"), Path && Path->bValid && Path->bClosedLoop))
			{
				Test->AddError(Nav->LastFailure.ToString());
				return Finish();
			}
			const auto* Mesh = CastChecked<ARecastNavMesh>(System->GetDefaultNavDataInstance(FNavigationSystem::DontCreate));
			if (Stage == 1)
			{
				OriginalRouteRevision = Path->RouteRevision;
				CheckCenterline(*Path);
				Test->TestEqual(TEXT("两点闭环仅提交两次原生全局查询"), Nav->PathQueryCount, InitialQueries + 2);
				FScopedCastleNavigationChange Change(World, Building);
				Test->TestFalse(TEXT("开始放置时旧路线立即失效"), Nav->GetSharedPath().bValid);
				Obstacle = AddBox(TEXT("CornerTable30cm"), FVector(0, 4 * CellSize, 65), FVector(30, 30, 130));
				Test->TestTrue(TEXT("置物台在建筑功能分支且真实参与导航"), Building->IsComponentRelevantForNavigation(Obstacle));
				Stage = 2;
				return false;
			}
			if (Stage == 2)
			{
				Test->TestEqual(TEXT("置物台入场后玩家点列未改变"), Path->RouteRevision, OriginalRouteRevision);
				Test->TestEqual(TEXT("动态修改也只在集中阶段重算两段"), Nav->PathQueryCount, InitialQueries + 4);
				CheckCenterline(*Path);
				CheckDetour(*Path, *Mesh, FVector(0, 4 * CellSize, 0), TEXT("(1,5) 转角"));
				FCastleSharedNavigationPath Temporary;
				const FVector TemporaryPoints[] = {Start, Goal};
				FText Reason;
				if (Test->TestTrue(TEXT("临时调动也能绕过同一置物台"), Nav->BuildTemporaryPath(TemporaryPoints, Temporary, Reason)))
				{
					bool bMatches = Path->PatrolPoints.Num() >= Temporary.PatrolPoints.Num();
					for (int32 I = 0; bMatches && I < Temporary.PatrolPoints.Num(); ++I)
					{
						bMatches = Path->PatrolPoints[I].Equals(Temporary.PatrolPoints[I], 0.01);
					}
					Test->TestTrue(TEXT("临时调动与正式闭环的首段轨迹一致"), bMatches);
				}
				else { Test->AddError(Reason.ToString()); }
				Test->AddExpectedMessagePlain(TEXT("显式巡逻路径点 2"), ELogVerbosity::Error);
				Test->TestTrue(TEXT("显式被挡格心的请求进入完整路线校验"), Nav->RequestSharedPatrolRoute({Start, Corner, Goal}, Reason));
				Test->TestFalse(TEXT("明确要站在置物台中心的路径点被拒绝，不横移"), Nav->GetSharedPath().bValid);
				Test->TestTrue(TEXT("失败说明指出显式路径点而非整格不可通行"), Nav->LastFailure.ToString().Contains(TEXT("显式巡逻路径点 2")));
				Test->TestTrue(TEXT("可重新提交不含障碍中心硬点的路线"), Nav->RequestSharedPatrolRoute({Start, Goal}, Reason));
				{
					FScopedCastleNavigationChange Change(World, Building);
					Obstacle->DestroyComponent();
					Obstacle = AddBox(TEXT("StraightTable30cm"), FVector(0, 2 * CellSize, 65), FVector(30, 30, 130));
				}
				Stage = 3;
				return false;
			}
			if (Stage == 3)
			{
				CheckCenterline(*Path);
				CheckDetour(*Path, *Mesh, FVector(0, 2 * CellSize, 0), TEXT("(1,3) 直行"));
				FScopedCastleNavigationChange Change(World, Building);
				Obstacle->DestroyComponent();
				Stage = 4;
				return false;
			}
			CheckCenterline(*Path);
			Test->TestTrue(TEXT("移除置物台后原位恢复为可行走"), ProjectsInPlace(*Mesh, Corner));
			FText Reason;
			Test->TestTrue(TEXT("恢复后可显式指定拐角格心"), Nav->RequestSharedPatrolRoute({Start, Corner, Goal}, Reason));
			const auto HardCorner = Nav->AcquireSharedPath();
			if (Test->TestTrue(TEXT("含硬拐点的路线完整发布"), HardCorner && HardCorner->bValid))
			{
				FVector Position, Direction;
				HardCorner->SamplePatrol(HardCorner->WaypointDistances[1], Position, Direction);
				Test->TestTrue(TEXT("精确经过硬拐点，没有用圆弧跳过"), Position.Equals(HardCorner->Waypoints[1], 0.01));
			}
			return Finish();
		}

	private:
		static constexpr double CellSize = 515.0;
		const FTransform Layout{FQuat(FVector::UpVector, FMath::DegreesToRadians(17.0)), FVector(7000, 1200, 600)};
		FAutomationTestBase* Test;
		const double Deadline = FPlatformTime::Seconds() + 90;
		ACastleNavigationDemo* Demo = nullptr;
		UWorld* World = nullptr;
		UCastleNavigationSubsystem* Nav = nullptr;
		UNavigationSystemV1* System = nullptr;
		ANavMeshBoundsVolume* Bounds = nullptr;
		ABuildingNavigationActor* Building = nullptr;
		UBuildingGameplayRootComponent* Gameplay = nullptr;
		ACastleBoard* Board = nullptr;
		UStaticMesh* Cube = nullptr;
		UStaticMeshComponent* Obstacle = nullptr;
		FTransform OldBounds;
		EComponentMobility::Type OldMobility = EComponentMobility::Static;
		FVector Start;
		FVector Goal;
		FVector Corner;
		int32 OriginalRouteRevision = 0;
		int32 InitialQueries = 0;
		int32 Stage = 0;

		bool BuildFixture()
		{
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It) { Bounds = *It; break; }
			if (!Test->TestNotNull(TEXT("夹具需要引擎基础立方体"), Cube)
				|| !Test->TestNotNull(TEXT("夹具需要地图的真实 Brush 导航体积"), Bounds)) { return false; }
			FScopedCastleNavigationChange Change(World, Demo);
			Demo->bRunPathDemonstration = false;
			OldBounds = Bounds->GetActorTransform();
			OldMobility = Bounds->GetRootComponent()->Mobility;
			Bounds->GetRootComponent()->SetMobility(EComponentMobility::Movable);
			const FVector Extent = Bounds->GetComponentsBoundingBox(true).GetExtent();
			Bounds->SetActorScale3D(OldBounds.GetScale3D() * FVector(2000, 2000, 350) / Extent);
			Bounds->SetActorLocation(Bounds->GetActorLocation() + Layout.TransformPosition(FVector(1030, 1030, 75))
				- Bounds->GetComponentsBoundingBox(true).GetCenter());
			System->OnNavigationBoundsUpdated(Bounds);

			Board = World->SpawnActorDeferred<ACastleBoard>(ACastleBoard::StaticClass(), Layout);
			Board->bGenerateGroundOnBeginPlay = false;
			Board->bSpawnMainTowerOnBeginPlay = Board->bSpawnInitialEnemiesOnBeginPlay = false;
			Board->GridOrigin = FIntVector(1, 1, 0);
			Board->GridSize = FIntVector(5, 5, 1);
			Board->CellSize = CellSize;
			Board->WorldBaseHeight = Board->LayerHeight * 0.5;
			Board->FinishSpawning(Layout);
			Building = World->SpawnActor<ABuildingNavigationActor>();
			Gameplay = NewObject<UBuildingGameplayRootComponent>(Building, TEXT("GameplayRoot"));
			Building->SetRootComponent(Gameplay);
			Building->AddInstanceComponent(Gameplay);
			Gameplay->RegisterComponent();
			Building->SetActorTransform(Layout);
			// 除 L 形楼板外没有可走捷径。第 (1,5) 格有完整地板，拐角通过是路径的物理要求。
			AddBox(TEXT("VerticalFloor"), FVector(0, 2 * CellSize, -25), FVector(CellSize, 5 * CellSize, 50));
			AddBox(TEXT("HorizontalFloor"), FVector(2 * CellSize, 4 * CellSize, -25), FVector(5 * CellSize, CellSize, 50));
			Start = Layout.TransformPosition(FVector(-100, 0, 5));
			Goal = Layout.TransformPosition(FVector(4 * CellSize, 4 * CellSize + 100, 5));
			Corner = Layout.TransformPosition(FVector(0, 4 * CellSize, 5));
			InitialQueries = Nav->PathQueryCount;
			FText Reason;
			Stage = 1;
			const bool bAccepted = Nav->RequestSharedPatrolRoute({Start, Goal}, Reason);
			if (!bAccepted) { Test->AddError(Reason.ToString()); }
			return bAccepted;
		}

		UStaticMeshComponent* AddBox(const FName Name, const FVector& Position, const FVector& Size) const
		{
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Building, Name);
			Mesh->SetupAttachment(Gameplay);
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Cube);
			Mesh->SetRelativeLocation(Position);
			Mesh->SetRelativeScale3D(Size / 100.0);
			Mesh->SetCollisionProfileName(TEXT("BlockAll"));
			Mesh->SetCanEverAffectNavigation(true);
			Building->AddInstanceComponent(Mesh);
			Mesh->RegisterComponent();
			return Mesh;
		}

		bool ProjectsInPlace(const ARecastNavMesh& Mesh, FVector Position) const
		{
			FNavLocation Projected;
			return Mesh.ProjectPoint(Position, Projected, FVector(1, 1, 20)) && FVector::DistSquared2D(Position, Projected.Location) < 0.01;
		}

		void CheckCenterline(const FCastleSharedNavigationPath& Path) const
		{
			for (const FVector Local : {FVector(0, CellSize, 0), FVector(0, 3 * CellSize, 0), FVector(CellSize, 4 * CellSize, 0), FVector(3 * CellSize, 4 * CellSize, 0)})
			{
				const FVector Center = Layout.TransformPosition(Local);
				bool bCrossesCenter = false;
				for (int32 I = 1; I < Path.PatrolPoints.Num(); ++I)
				{
					FVector Probe = Center;
					Probe.Z = Path.PatrolPoints[I].Z;
					if (FVector::DistSquared2D(Probe, FMath::ClosestPointOnSegment(Probe, Path.PatrolPoints[I - 1], Path.PatrolPoints[I])) < 1.0)
					{ bCrossesCenter = true; break; }
				}
				Test->TestTrue(FString::Printf(TEXT("实际折线经过未阻挡格心 %s"), *Local.ToCompactString()), bCrossesCenter);
			}
			Test->TestTrue(TEXT("首尾闭合且两处玩家锚点不被移动"), Path.PatrolPoints[0].Equals(Path.PatrolPoints.Last(), 0.01)
				&& FVector::DistSquared2D(Path.Waypoints[0], Start) < 0.01 && FVector::DistSquared2D(Path.Waypoints[1], Goal) < 0.01);
		}

		void CheckDetour(const FCastleSharedNavigationPath& Path, const ARecastNavMesh& Mesh, FVector LocalCenter, const TCHAR* Label) const
		{
			Test->TestFalse(TEXT("30cm 置物台确实抠掉原位 NavMesh"), ProjectsInPlace(Mesh, Layout.TransformPosition(LocalCenter)));
			int32 Knot = 1;
			// 双向各自验证：返程即使选了另一侧，也必须在同一格内圆滑绕过且不能穿入空洞。
			for (int32 Leg = 1; Leg <= 2; ++Leg)
			{
				double ClosestApproach = TNumericLimits<double>::Max();
				int32 ArcSegments = 0;
				FVector LastDirection = FVector::ZeroVector;
				bool bAllChordsClear = true;
				for (; Knot < Path.PatrolPoints.Num() && Path.PatrolDistances[Knot] <= Path.WaypointDistances[Leg]; ++Knot)
				{
					const FVector A = Path.PatrolPoints[Knot - 1];
					const FVector B = Path.PatrolPoints[Knot];
					const FVector Local = Layout.InverseTransformPosition((A + B) * 0.5) - LocalCenter;
					const FVector Direction = (B - A).GetSafeNormal2D();
					if (FMath::Abs(Local.X) < CellSize * 0.5 && FMath::Abs(Local.Y) < CellSize * 0.5)
					{
						ClosestApproach = FMath::Min(ClosestApproach, Local.Size2D());
						const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(LastDirection, Direction), -1.0, 1.0)));
						if (Angle > 1.0 && Angle < 16.0) { ++ArcSegments; }
					}
					FVector Hit;
					bAllChordsClear &= !ARecastNavMesh::NavMeshRaycast(&Mesh, A, B, Hit, nullptr)
						&& ProjectsInPlace(Mesh, (A + B) * 0.5);
					LastDirection = Direction;
				}
				const FString LegLabel = FString::Printf(TEXT("%s %s"), Label, Leg == 1 ? TEXT("去程") : TEXT("返程"));
				Test->AddInfo(FString::Printf(TEXT("%s 置物台：路径最近 %.1f cm，格内圆弧方向采样 %d 段。"), *LegLabel, ClosestApproach, ArcSegments));
				Test->TestTrue(LegLabel + TEXT(" 近距离经过格内障碍而非提前换格"), ClosestApproach > 30.0 && ClosestApproach < 155.0);
				Test->TestTrue(LegLabel + TEXT(" 有连续小幅转向的圆弧细分"), ArcSegments >= 3);
				Test->TestTrue(LegLabel + TEXT(" 全部实际弦均可走，不切进置物台"), bAllChordsClear);
			}
		}

		bool Finish() const
		{
			if (!World) { return true; }
			FScopedCastleNavigationChange Change(World, Demo);
			Nav->ClearSharedRoute();
			if (Building) { Building->Destroy(); }
			if (Board) { Board->Destroy(); }
			if (Stage != 0 && Bounds)
			{
				Bounds->SetActorTransform(OldBounds);
				Bounds->GetRootComponent()->SetMobility(OldMobility);
				System->OnNavigationBoundsUpdated(Bounds);
			}
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleGridSurfacePathTest, "MovingCastle.Navigation.GridCornerObstacle",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleGridSurfacePathTest::RunTest(const FString& Parameters)
{
	// 真实地图提供运行时导航系统；几何和所有断言由本用例在独立游戏进程中建立。
	ADD_LATENT_AUTOMATION_COMMAND(CastleGridPatrolTests::FCornerObstacle(this));
	return true;
}

#endif
