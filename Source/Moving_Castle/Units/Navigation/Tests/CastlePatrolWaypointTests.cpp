#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Actors/CastleBoard.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePatrolWaypointGridCenterTest, "MovingCastle.Navigation.PatrolWaypoint.GridCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePatrolWaypointGridCenterTest::RunTest(const FString& Parameters)
{
	const auto Settings = UWorld::InitializationValues().CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	// 复现没有实体地块的逻辑棋盘。旋转、非均匀缩放、编号原点和板面高度故意不同于默认值。
	Board->PlacementSurfaceMode = ECastlePlacementSurfaceMode::MainTowerSides;
	Board->GridOrigin = FIntVector(-5, 7, 2);
	Board->GridSize = FIntVector(3, 3, 1);
	const FTransform Layout(FRotator(0, 31, 0), FVector(5000, -2100, 900), FVector(1.25, 0.75, 1.5));
	Board->SetActorTransform(Layout);

	// 单块真实碰撞跨多个格且高于棋盘基准面，验证算法不是依赖地块 Actor 或写死楼面高度。
	AActor* Floor = World->SpawnActor<AActor>();
	UBoxComponent* FloorCollision = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(FloorCollision);
	FloorCollision->SetBoxExtent(FVector(2000, 2000, 10));
	FloorCollision->SetCollisionProfileName(TEXT("BlockAll"));
	FloorCollision->RegisterComponent();
	Floor->SetActorTransform(FTransform(Layout.GetRotation(), Layout.TransformPosition(FVector(0, 0, 240)), Layout.GetScale3D()));

	const auto Trace = [this, World](const FVector& Top, FVector& Result)
	{
		FText Failure;
		const bool bResolved = ACastlePatrolRoute::TryResolveWaypointFromRay(World, Top + FVector(0, 0, 100), Top - FVector(0, 0, 100), Result, Failure);
		if (!TestTrue(TEXT("真实顶面射线成功放点"), bResolved)) { AddError(Failure.ToString()); }
	};
	const double Size = Board->CellSize;
	const FVector Expected = Layout.TransformPosition(FVector(Size, Size, 250));
	FVector First{}, Second{}, Neighbor{}, Outside{};
	Trace(Layout.TransformPosition(FVector(Size - 100, Size + 170, 250)), First);
	Trace(Layout.TransformPosition(FVector(Size + 120, Size - 75, 250)), Second);
	Trace(Layout.TransformPosition(FVector(Size * 1.51, Size, 250)), Neighbor);
	const FVector OutsideHit = Layout.TransformPosition(FVector(-Size, Size + 63, 250));
	Trace(OutsideHit, Outside);
	TestTrue(TEXT("两次点击不同位置也落在同一个水平格心，Z 仍是真实顶面"), First.Equals(Expected, 0.1) && Second.Equals(Expected, 0.1));
	TestTrue(TEXT("跨过半格边界就取相邻格心"), Neighbor.Equals(Layout.TransformPosition(FVector(2 * Size, Size, 250)), 0.1));
	TestTrue(TEXT("棋盘范围外没有逻辑格，保留实际命中位置"), Outside.Equals(OutsideHit, 0.1));
	FText Failure;
	TestFalse(TEXT("未命中真实顶面不生成虚拟落点"), ACastlePatrolRoute::TryResolveWaypointFromRay(World, Expected + FVector(0, 0, 200),
		Expected + FVector(0, 0, 100), Outside, Failure));
	World->DestroyWorld(false);
	return true;
}

#endif
