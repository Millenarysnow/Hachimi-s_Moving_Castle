#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Units/BuildingInteraction/CastleBuildingTriggerPath.h"

#include "CastleBuildingTestUtilities.h"
#include "CastleNavigationSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Units/BuildingInteraction/CastleBuildingTriggerSubsystem.h"

namespace CastleBuildingTriggerTests
{
	/** 构造带真实累计距离的闭环折线，刻意使用稀疏拐点验证没有依赖点采样精度。 */
	FCastleSharedNavigationPath MakePath(const TArray<FVector>& Points)
	{
		FCastleSharedNavigationPath Path;
		Path.bValid = Path.bClosedLoop = true;
		Path.PathRevision = 7;
		Path.PatrolPoints = Points;
		Path.PatrolDistances.Add(0.0);
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Path.Length += FVector::Distance(Points[Index - 1], Points[Index]);
			Path.PatrolDistances.Add(Path.Length);
		}
		return Path;
	}

	/** 将事件收集成实例 ID，既验证次数，也验证单个 NPC 的经过顺序。 */
	TArray<uint64> Collect(const FCastleBuildingTriggerPath& Path, double Start, double Travel)
	{
		TArray<uint64> Result;
		Path.ForEachEntry(Start, Travel, [&Result](const FCastleBuildingEntryMarker& Marker) { Result.Add(Marker.BuildingId); });
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingTriggerIntervalTest, "MovingCastle.BuildingTriggers.ContinuousIntervals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingTriggerIntervalTest::RunTest(const FString& Parameters)
{
	using namespace CastleBuildingTriggerTests;
	const auto Route = MakePath({FVector(-10, 0, 1), FVector(10, 0, 1), FVector(10, 10, 1), FVector(-10, 10, 1), FVector(-10, 0, 1)});
	FCastleBuildingTriggerRegion A;
	A.BuildingId = 11;
	// 三个相邻/重叠盒组成一栋楼，内部边界和重叠不能生成额外触发。
	A.LocalBoxes = {FBox(FVector(-4, -2, 0), FVector(0, 2, 5)), FBox(FVector(0, -2, 0), FVector(2, 2, 5)), FBox(FVector(-1, -2, 0), FVector(1, 2, 5))};
	FCastleBuildingTriggerRegion B;
	B.BuildingId = 22;
	B.LocalBoxes = {FBox(FVector(2, -2, 0), FVector(4, 2, 5))};
	FCastleBuildingTriggerPath Compiled;
	FText Failure;
	if (!TestTrue(TEXT("稀疏折线按连续区域编译"), FCastleBuildingTriggerPath::Compile(Route, 3, {A, B}, Compiled, Failure))) { AddError(Failure.ToString()); return false; }
	TestEqual(TEXT("多格合成楼只有一个进入点，相邻另一栋楼仍独立"), Compiled.Entries.Num(), 2);
	TestEqual(TEXT("首次进入位置不受稀疏采样点影响"), Compiled.Entries[0].Distance, 6.0);
	TestTrue(TEXT("一帧穿过两楼并落在外部仍按顺序触发"), Collect(Compiled, 0, 20) == TArray<uint64>({11, 22}));
	TestEqual(TEXT("停留不触发"), Collect(Compiled, 7, 0).Num(), 0);
	TestEqual(TEXT("在同楼内部移动不重复触发"), Collect(Compiled, 7, 4).Num(), 0);
	TestEqual(TEXT("到达进入边界时只消费一次"), Collect(Compiled, 0, 6).Num(), 1);
	TestEqual(TEXT("从已经消费的边界继续前进不重复"), Collect(Compiled, 6, 1).Num(), 0);
	TestTrue(TEXT("两整圈保留全部四次进入且保持顺序"), Collect(Compiled, 0, Route.Length * 2) == TArray<uint64>({11, 22, 11, 22}));
	TestEqual(TEXT("出生在楼内不凭空补发进入"), Collect(Compiled, 7, 1).Num(), 0);

	// 全程在楼内，及只在闭环首尾楼内，都是过去容易将距离取模误认为重新进入的情况。
	FCastleBuildingTriggerRegion Whole;
	Whole.BuildingId = 33;
	Whole.LocalBoxes = {FBox(FVector(-20, -20, 0), FVector(20, 20, 5))};
	FCastleBuildingTriggerPath Inside;
	FCastleBuildingTriggerPath::Compile(Route, 3, {Whole}, Inside, Failure);
	TestEqual(TEXT("整圈在楼内，任意多圈都没有重新进入"), Collect(Inside, 0, Route.Length * 10).Num(), 0);
	FCastleBuildingTriggerRegion Seam;
	Seam.BuildingId = 44;
	Seam.LocalBoxes = {FBox(FVector(-12, -2, 0), FVector(-8, 2, 5))};
	FCastleBuildingTriggerPath SeamPath;
	FCastleBuildingTriggerPath::Compile(Route, 3, {Seam}, SeamPath, Failure);
	TestEqual(TEXT("首尾同一次停留只有真正从外部进入的标记"), SeamPath.Entries.Num(), 1);
	TestEqual(TEXT("楼内跨过闭环零点不触发"), Collect(SeamPath, Route.Length - 1, 2).Num(), 0);
	TestEqual(TEXT("出楼再进一次只记一次"), Collect(SeamPath, 0, Route.Length).Num(), 1);

	const auto Backtrack = MakePath({FVector(-10, 0, 1), FVector(10, 0, 1), FVector(-10, 0, 1)});
	FCastleBuildingTriggerPath Twice;
	FCastleBuildingTriggerPath::Compile(Backtrack, 3, {A}, Twice, Failure);
	TestEqual(TEXT("同圈往返会两次真正进入同楼"), Collect(Twice, 0, Backtrack.Length).Num(), 2);
	const auto TurnInside = MakePath({FVector(-10, 0, 1), FVector(0, 0, 1), FVector(-10, 0, 1)});
	FCastleBuildingTriggerPath Turn;
	FCastleBuildingTriggerPath::Compile(TurnInside, 3, {A}, Turn, Failure);
	TestEqual(TEXT("在楼内折返未离开，只有一次进入"), Collect(Turn, 0, TurnInside.Length).Num(), 1);

	// 三维区域、上界排除和棋盘变换必须同时成立，不能只用 XY 距离或美术包围盒。
	FCastleBuildingTriggerRegion Upper = A;
	Upper.BuildingId = 55;
	Upper.LocalBoxes = {FBox(FVector(-4, -2, 5), FVector(2, 2, 10))};
	FCastleBuildingTriggerPath UpperPath;
	FCastleBuildingTriggerPath::Compile(Route, 3, {Upper}, UpperPath, Failure);
	TestEqual(TEXT("相同 XY 的其他楼层不触发"), UpperPath.Entries.Num(), 0);
	const auto OnTop = MakePath({FVector(-10, 0, 5), FVector(10, 0, 5), FVector(-10, 0, 5)});
	FCastleBuildingTriggerPath TopPath;
	FCastleBuildingTriggerPath::Compile(OnTop, 3, {A, Upper}, TopPath, Failure);
	TestTrue(TEXT("公共楼层平面只属于下界包含的上层"), Collect(TopPath, 0, OnTop.Length) == TArray<uint64>({55, 55}));
	FCastleBuildingTriggerRegion Moved = A;
	Moved.LocalToWorld = FTransform(FRotator(12, 39, -7), FVector(80, -130, 40), FVector(2, 0.7, 1.3));
	TArray<FVector> WorldPoints;
	for (const FVector& Point : Route.PatrolPoints) { WorldPoints.Add(Moved.LocalToWorld.TransformPosition(Point)); }
	const auto WorldRoute = MakePath(WorldPoints);
	FCastleBuildingTriggerPath MovedPath;
	FCastleBuildingTriggerPath::Compile(WorldRoute, 3, {Moved}, MovedPath, Failure);
	TestEqual(TEXT("旋转倾斜和非均匀缩放不改变逻辑归属"), Collect(MovedPath, 0, WorldRoute.Length).Num(), 1);

	FCastleBuildingTriggerRegion Tangent;
	Tangent.BuildingId = 66;
	Tangent.LocalBoxes = {FBox(FVector(0, 0, 0), FVector(2, 2, 5))};
	const auto Touch = MakePath({FVector(-1, 1, 1), FVector(1, -1, 1), FVector(-1, 1, 1)});
	FCastleBuildingTriggerPath TouchPath;
	FCastleBuildingTriggerPath::Compile(Touch, 3, {Tangent}, TouchPath, Failure);
	TestEqual(TEXT("角点切线接触没有走入区域"), TouchPath.Entries.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingTriggerRegistrationTest, "MovingCastle.BuildingTriggers.BoardRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingTriggerRegistrationTest::RunTest(const FString& Parameters)
{
	using namespace CastleBuildingTriggerTests;
	// 使用正式棋盘放置、完成和删除流程验证登记，纯快照负责提供可重复的几何输入。
	const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	// 正式 Destroy 路径要求世界上下文，否则引擎会拒绝销毁，无法验证 OnDestroyed 注销。
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
	// AActor::ProcessEvent 在未初始化的临时世界中会跳过动态 OnDestroyed 回调，
	// 因此采用实际的 Actor 初始化边界，而不是直接把测试未运行的回调视为生产逻辑失败。
	World->InitializeActorsForPlay(FURL());
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	Board->GridSize = FIntVector(2, 1, 1);
	Board->CellSize = 20;
	Board->LayerHeight = 10;
	Board->WorldBaseHeight = 5;
	UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	FCastleGroundGenerationResult Ground;
	for (int32 X = 0; X < 2; ++X)
	{
		FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
		Cell.Coordinate = FIntVector(X, 0, 0);
		Cell.Color = ECastleTileColor::Red;
		Cell.Number = 3;
	}
	FText Failure;
	Board->ApplyGroundGenerationResult(Ground, Failure);
	FCastleBuildingDescriptor Card;
	Definition->CreateDescriptor(2, ECastleTileColor::Red, 3, Card, Failure);
	ACastleBuildingBase* Building = Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Failure);
	if (!TestNotNull(TEXT("通过棋盘放置双格楼"), Building)) { AddError(Failure.ToString()); return false; }
	auto Route = MakeShared<FCastleSharedNavigationPath, ESPMode::ThreadSafe>(MakePath({FVector(-20, 0, 1), FVector(40, 0, 1), FVector(-20, 0, 1)}));
	auto* Triggers = World->GetSubsystem<UCastleBuildingTriggerSubsystem>();
	const auto DuringAssembly = Triggers->AcquireTriggerPath(Route);
	TestEqual(TEXT("飞入未完成不参加触发编译"), DuringAssembly->Entries.Num(), 0);
	CastleBuildingTests::CompletePlacement(Building);
	const auto Ready = Triggers->AcquireTriggerPath(Route);
	TestEqual(TEXT("完成后双格并成单楼，每方向一次"), Ready->Entries.Num(), 2);
	const uint64 PreviousId = Ready->Entries[0].BuildingId;
	const int32 Count = Triggers->CompilationCount;
	for (int32 Index = 0; Index < 1024; ++Index) { Triggers->AcquireTriggerPath(Route); }
	TestEqual(TEXT("千次批量消费不增加空间编译"), Triggers->CompilationCount, Count);
	Board->RemoveBuilding(Building, Failure);
	TestEqual(TEXT("移除立即注销区域"), Triggers->AcquireTriggerPath(Route)->Entries.Num(), 0);
	TestEqual(TEXT("已发布旧快照不会被原地修改"), Ready->Entries.Num(), 2);
	Building = Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Failure);
	CastleBuildingTests::CompletePlacement(Building);
	const auto Replacement = Triggers->AcquireTriggerPath(Route);
	TestTrue(TEXT("原格重新放置获得不同登记身份"), Replacement->Entries[0].BuildingId != PreviousId);
	Building->Destroy();
	TestEqual(TEXT("直接销毁也撤销登记"), Triggers->AcquireTriggerPath(Route)->Entries.Num(), 0);
	return true;
}

#endif
