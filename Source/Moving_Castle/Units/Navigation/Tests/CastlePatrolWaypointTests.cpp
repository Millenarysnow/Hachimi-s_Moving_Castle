#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Actors/CastleBoard.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Interaction/CastlePatrolWaypointMarker.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

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

	// 同一条射线先经过只响应 Visibility 的非建筑装饰，再经过两个可行走平面。
	// 改变上层平面的碰撞通道后，仍应命中原地面，并保留真实高度。
	AActor* RaisedFloor = World->SpawnActor<AActor>();
	UBoxComponent* RaisedCollision = NewObject<UBoxComponent>(RaisedFloor);
	RaisedFloor->SetRootComponent(RaisedCollision);
	RaisedCollision->SetBoxExtent(FVector(80, 80, 10));
	RaisedCollision->SetCollisionProfileName(TEXT("BlockAll"));
	RaisedCollision->RegisterComponent();
	RaisedFloor->SetActorLocation(Expected + FVector(0, 0, 30));
	FVector RaisedPoint;
	Trace(Expected, RaisedPoint);
	TestTrue(TEXT("两个合法支撑面中优先采用距离较近的顶面"), RaisedPoint.Equals(Expected + FVector(0, 0, 40), 0.1));
	RaisedCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Trace(Expected, RaisedPoint);
	TestTrue(TEXT("仅可见的装饰顶面不截断地面命中"), RaisedPoint.Equals(Expected, 0.1));

	FText Failure;
	TestFalse(TEXT("未命中真实顶面不生成虚拟落点"), ACastlePatrolRoute::TryResolveWaypointFromRay(World, Expected + FVector(0, 0, 200),
		Expected + FVector(0, 0, 100), Outside, Failure));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePatrolWaypointSpeedRoomTest, "MovingCastle.Navigation.PatrolWaypoint.SpeedRoomFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePatrolWaypointSpeedRoomTest::RunTest(const FString& Parameters)
{
	// 通过正式定义和棋盘放置实例化加速房，测试无需改写其蓝图或添加测试碰撞。
	auto* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_SpeedRoom.DA_SpeedRoom"));
	if (!TestNotNull(TEXT("正式加速房定义可加载"), Definition)) { return false; }
	const auto Settings = UWorld::InitializationValues().CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建加速房测试世界"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Board = World->SpawnActor<ACastleBoard>();
	if (!TestNotNull(TEXT("创建加速房测试棋盘"), Board)) { return false; }
	Board->GridSize = FIntVector(1, 1, 1);
	Board->BuildingDefinitions = {Definition};
	FCastleGroundGenerationResult Ground;
	FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
	Cell.Coordinate = Board->GridOrigin;
	Cell.Color = ECastleTileColor::Red;
	Cell.Number = 3;
	FCastleBuildingDescriptor Card;
	FText Reason;
	if (!Board->ApplyGroundGenerationResult(Ground, Reason) || !Definition->CreateDescriptor(1, Cell.Color, Cell.Number, Card, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	// 隐藏真实棋盘地块的点击面，只验证加速房自己的功能楼板。
	Board->SetGroundPresentationHidden(true);
	auto* Room = Board->PlaceBuilding(Cell.Coordinate, Card, false, Reason);
	if (!TestNotNull(TEXT("正式一级加速房可放置"), Room)
		|| !TestTrue(TEXT("正式加速房完成飞入"), CastleBuildingTests::CompletePlacement(Room)))
	{
		AddError(Reason.ToString());
		return false;
	}
	// 旧加速房的准绳是格底 Z=0，直接由棋盘计算期望值。
	TestNull(TEXT("加速房沿用旧模板站立标高"), Room->HachimisLayout.Get());
	const FVector Expected = Board->GetGridWorldLocation(Cell.Coordinate) - FVector(0, 0, Board->LayerHeight * 0.5);
	const FVector Start = Expected + FVector(40, -30, 150);
	const FVector End = Expected + FVector(40, -30, -20);
	FHitResult FloorHit;
	if (!TestTrue(TEXT("加速房有真实功能楼板且阻挡 Pawn"), World->LineTraceSingleByChannel(FloorHit, Start, End, ECC_Pawn)))
	{
		return false;
	}
	UPrimitiveComponent* Floor = FloorHit.GetComponent();
	TestTrue(TEXT("功能楼板顶面位于格底"), FMath::IsNearlyEqual(FloorHit.ImpactPoint.Z, Expected.Z, 0.01));
	TestTrue(TEXT("加速房楼板位于功能层级并参与导航"), Floor && FloorHit.GetActor() == Room
		&& Floor->GetFName() == TEXT("GameplayFloor_0_0_0") && Room->IsComponentRelevantForNavigation(Floor));
	TestTrue(TEXT("加速房功能楼板保持 Visibility 透明"), Floor && Floor->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore);
	FVector Point;
	if (!TestTrue(TEXT("巡逻射线能解析加速房楼板"), ACastlePatrolRoute::TryResolveWaypointFromRay(World, Start, End, Point, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	return TestTrue(TEXT("加速房路径点吸附到格心并保留楼面标高"), Point.Equals(Expected, 0.1));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePatrolWaypointNumberTest, "MovingCastle.Navigation.PatrolWaypoint.NumberModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePatrolWaypointNumberTest::RunTest(const FString& Parameters)
{
	// 直接加载交付蓝图和它引用的网格，验证的不是 C++ 替代夹具。
	UClass* MarkerClass = LoadClass<ACastlePatrolWaypointMarker>(nullptr,
		TEXT("/Game/MovingCastle/BP/BP_CastlePatrolWaypointMarker.BP_CastlePatrolWaypointMarker_C"));
	if (!TestNotNull(TEXT("实际标记蓝图可加载"), MarkerClass)) { return false; }
	const auto Settings = UWorld::InitializationValues().CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	const FVector Ground(1000, -400, 325);
	ACastlePatrolWaypointMarker* Marker = World->SpawnActor<ACastlePatrolWaypointMarker>(MarkerClass, Ground, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("真实蓝图在世界中生成"), Marker)) { World->DestroyWorld(false); return false; }

	TInlineComponentArray<UStaticMeshComponent*> Meshes(Marker);
	TestEqual(TEXT("一个主体、两面各两个数字组件"), Meshes.Num(), 5);
	TestFalse(TEXT("标记 Actor 不开启碰撞"), Marker->GetActorEnableCollision());
	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		TestEqual(TEXT("所有网格组件均关闭碰撞"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("标记网格不会影响 NavMesh"), Mesh->CanEverAffectNavigation());
	}
	const UStaticMeshComponent* Body = Cast<UStaticMeshComponent>(Marker->GetRootComponent());
	TestTrue(TEXT("Actor 根枢轴保持地面坐标；主体最低点悬浮约 51 cm"), Body && Body->GetStaticMesh()
		&& Marker->GetActorLocation().Equals(Ground) && FMath::IsNearlyEqual(Body->Bounds.GetBox().Min.Z, Ground.Z + 51.1, 0.2));

	const auto VerifyDigit = [this, &Meshes, Body](FName Name, int32 Digit, bool bVisible, const FVector& SlotLocation, bool bFront)
	{
		const UStaticMeshComponent* const* Component = Meshes.FindByPredicate([Name](const UStaticMeshComponent* Mesh) { return Mesh->GetFName() == Name; });
		if (!TestTrue(TEXT("数字组件直接挂在主体下"), Component && (*Component)->GetAttachParent() == Body)) { return; }
		const UStaticMeshComponent* Mesh = *Component;
		TestEqual(FString::Printf(TEXT("%s 显隐"), *Name.ToString()), Mesh->IsVisible(), bVisible);
		if (!bVisible) { return; }
		const FString AssetName = FString::Printf(TEXT("SM_MC_Waypoint_Digit_%d"), Digit);
		TestEqual(FString::Printf(TEXT("%s 使用对应的数字网格"), *Name.ToString()), GetNameSafe(Mesh->GetStaticMesh()), AssetName);
		const FTransform& Relative = Mesh->GetRelativeTransform();
		// 位置来自美术槽位；左右以各面的观看方向定义。由局部外法线与向上方向独立检验没有翻转或镜像。
		TestTrue(FString::Printf(TEXT("%s 按单双位摆在正确槽位"), *Name.ToString()), Relative.GetLocation().Equals(SlotLocation, 0.2));
		TestTrue(TEXT("数字面朝外且向上倾斜，数字顶部朝上，未继承 FBX 空物体缩放"),
			Relative.GetRotation().RotateVector(FVector::RightVector).Equals(FVector(0, bFront ? 0.848 : -0.848, 0.530), 0.002)
			&& Relative.GetRotation().RotateVector(FVector::UpVector).Z > 0.8 && Relative.GetScale3D().Equals(FVector::OneVector));
	};
	for (const int32 Number : {0, 9, 10, 42, 99, 8})
	{
		if (!TestTrue(TEXT("有效编号在真实资产上显示成功"), Marker->SetDisplayedNumber(Number))) { break; }
		TestEqual(TEXT("记录当前编号"), Marker->GetDisplayedNumber(), Number);
		const bool bTwoDigits = Number >= 10;
		const double OnesX = bTwoDigits ? 24.5 : 0.0;
		VerifyDigit(TEXT("FrontOnes"), Number % 10, true, FVector(OnesX, 38.0, 179.48), true);
		VerifyDigit(TEXT("BackOnes"), Number % 10, true, FVector(-OnesX, -38.0, 179.48), false);
		VerifyDigit(TEXT("FrontTens"), Number / 10, bTwoDigits, FVector(-24.5, 38.0, 179.48), true);
		VerifyDigit(TEXT("BackTens"), Number / 10, bTwoDigits, FVector(24.5, -38.0, 179.48), false);
	}
	AddExpectedError(TEXT("数字必须介于 0 和 99 之间"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("拒绝负数"), Marker->SetDisplayedNumber(-1));
	TestFalse(TEXT("拒绝三位数"), Marker->SetDisplayedNumber(100));
	TestEqual(TEXT("越界不会改写已显示号码"), Marker->GetDisplayedNumber(), 8);
	VerifyDigit(TEXT("FrontOnes"), 8, true, FVector(0, 38.0, 179.48), true);
	FHitResult Hit;
	TestFalse(TEXT("从面板正面穿过标记的 Visibility 射线未被拦截"), World->LineTraceSingleByChannel(Hit,
		Ground + FVector(0, 200, 179.48), Ground + FVector(0, -200, 179.48), ECC_Visibility));
	World->DestroyWorld(false);
	return true;
}

#endif
