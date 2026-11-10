#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingMergeComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleBuildingTestUtilities.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace CastleMergeTests
{
	/** 以正式放置接口建立固定红色 3 号地面，测试不跳过放置匹配或合成规则。 */
	ACastleBoard* MakeBoard(UWorld* World, UCastleBuildingDefinition* Definition)
	{
		ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
		Board->GridSize = FIntVector(2, 2, 2);
		Board->BuildingDefinitions = { Definition };
		FCastleGroundGenerationResult Ground;
		for (int32 X = 0; X < 2; ++X)
		{
			for (int32 Y = 0; Y < 2; ++Y)
			{
				FCastleGroundCell Cell;
				Cell.Coordinate = FIntVector(X, Y, 0);
				Cell.Color = ECastleTileColor::Red;
				Cell.Number = 3;
				Ground.Cells.Add(Cell);
			}
		}
		FText Reason;
		check(Board->ApplyGroundGenerationResult(Ground, Reason));
		return Board;
	}

	/** 必须是游戏世界才能播放原生组件；WorldContext 让真实 Actor 销毁路径与 PIE 一致。 */
	UWorld* MakeWorld()
	{
		const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
			.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		return World;
	}
}

/** 真实拆件模型：旧建筑已经销毁，114 件输入零件仍能汇聚，并精确恢复作者模型。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleRealBuildingMergeTest,
	"MovingCastle.Components.Animation.BuildingMerge.RealBuilding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleRealBuildingMergeTest::RunTest(const FString& Parameters)
{
	UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_ArcherTower.DA_ArcherTower"));
	if (!TestNotNull(TEXT("正式箭塔定义存在"), Definition)) { return false; }
	UWorld* World = CastleMergeTests::MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	ACastleBoard* Board = CastleMergeTests::MakeBoard(World, Definition);
	FCastleBuildingDescriptor Card;
	FText Reason;
	Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason);
	ACastleBuildingBase* First = Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Reason);
	if (!TestNotNull(TEXT("放置真实一级箭塔"), First)) { return false; }
	if (!CastleBuildingTests::CompletePlacement(First)) { AddError(TEXT("原有一级箭塔飞入失败。")); return false; }
	TArray<UStaticMeshComponent*> FirstMeshes;
	First->GetComponents<UStaticMeshComponent>(FirstMeshes);
	const int32 SourceCount = FirstMeshes.FilterByPredicate([](const UStaticMeshComponent* Mesh)
	{
		return Mesh->GetStaticMesh() && Mesh->IsVisible() && !Mesh->ComponentHasTag(TEXT("MergeExclude"));
	}).Num() * 2;
	ACastleBuildingBase* Result = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("新建筑先开始原有放置飞入"), Result)) { AddError(Reason.ToString()); return false; }
	TestFalse(TEXT("放置飞入期间不提前合并"), Result->BuildingMerge->bIsPlaying);
	TestEqual(TEXT("飞入期间仍保留两座一级建筑"), Board->GetPlacedBuildingCount(), 2);
	CastleBuildingTests::CompletePlacement(Result);
	Result = CastleBuildingTests::FindPlacedLevel(Board, 2);
	if (!TestNotNull(TEXT("新建筑飞入完成后才触发真实合并"), Result)) { AddError(Reason.ToString()); return false; }
	UBuildingMergeComponent* Merge = Result->BuildingMerge;
	TestTrue(TEXT("合并自动启动原生组件"), Merge->bIsPlaying && Reason.IsEmpty());
	TestTrue(TEXT("使用大量真实旧网格而非立方体或粒子"), Merge->SourcePartCount == SourceCount && SourceCount >= 100);
	TestEqual(TEXT("跨导入资产的零件全部建立对应"), Merge->ReusedPartCount, SourceCount);
	TestEqual(TEXT("放置飞入完成后不重复从场外飞入"), Merge->IncomingPartCount, 0);
	TestEqual(TEXT("两座已落位建筑的对应件在合并阶段局部移动"), Merge->MovedPartCount, SourceCount);
	TestEqual(TEXT("没有把可复用件误判成新增"), Merge->AddedPartCount, 0);
	TestEqual(TEXT("没有把可复用件误判成移除"), Merge->RemovedPartCount, 0);
	TestFalse(TEXT("旧建筑已经被规则销毁"), IsValid(First));
	TestEqual(TEXT("逻辑只保留一个二级建筑"), Board->GetPlacedBuildingCount(), 1);
	TestEqual(TEXT("仍占两格"), Board->GetOccupiedRoomCellCount(), 2);

	TArray<UStaticMeshComponent*> Meshes;
	Result->GetComponents<UStaticMeshComponent>(Meshes);
	TMap<UStaticMeshComponent*, FTransform> Authored;
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetStaticMesh() && !Mesh->HasAnyFlags(RF_Transient) && !Mesh->ComponentHasTag(TEXT("MergeExclude")))
		{
			Authored.Add(Mesh, Mesh->GetRelativeTransform());
		}
	}
	TestEqual(TEXT("目标件数等于建筑的全部模型组件"), Merge->TargetPartCount, Authored.Num());
	Merge->TickComponent(1.6f, LEVELTICK_All, nullptr);
	Merge->TickComponent(5.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("真实模型合并完成"), Merge->bIsPlaying);
	for (const auto& Pair : Authored)
	{
		TestTrue(TEXT("真实零件精确保留作者局部变换"), Pair.Key->GetRelativeTransform().Equals(Pair.Value));
		TestTrue(TEXT("真实零件恢复可见"), Pair.Key->IsVisible());
	}
	return true;
}

/** 一次放置连续升级，以及下一次放置消耗仍在动画中的建筑，均不能丢件或残留中间 Actor。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleChainBuildingMergeTest,
	"MovingCastle.Components.Animation.BuildingMerge.ChainAndRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleChainBuildingMergeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CastleMergeTests::MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	ACastleBoard* Board = CastleMergeTests::MakeBoard(World, Definition);
	FCastleBuildingDescriptor Card;
	FText Reason;
	Definition->CreateDescriptor(2, ECastleTileColor::Red, 3, Card, Reason);
	ACastleBuildingBase* Upper = Board->PlaceBuilding(FIntVector(0, 1, 0), Card, false, Reason);
	Card.Level = 1;
	ACastleBuildingBase* Lower = Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Reason);
	CastleBuildingTests::CompletePlacement(Upper);
	CastleBuildingTests::CompletePlacement(Lower);
	ACastleBuildingBase* Result = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("一次放置连续升级先开始放置飞入"), Result)) { AddError(Reason.ToString()); return false; }
	CastleBuildingTests::CompletePlacement(Result);
	Result = CastleBuildingTests::FindPlacedLevel(Board, 3);
	if (!TestNotNull(TEXT("新建筑飞入完成后连续升级到三级"), Result)) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("直接合成到三级"), Result->GetBuildingLevel(), 3);
	TestEqual(TEXT("只抓取三座真实输入建筑，不抓取中间二级"), Result->BuildingMerge->SourcePartCount, 3);
	TestEqual(TEXT("没有残留中间等级占用"), Board->GetPlacedBuildingCount(), 1);
	TestTrue(TEXT("动画中可直接移除结果"), Board->RemoveBuilding(Result, Reason));
	TestEqual(TEXT("移除后清空四格占用"), Board->GetOccupiedRoomCellCount(), 0);

	Lower = Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Reason);
	CastleBuildingTests::CompletePlacement(Lower);
	ACastleBuildingBase* FirstMerge = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("先合成一对"), FirstMerge)) { return false; }
	TestTrue(TEXT("新放置建筑先播放 Assembly Flight"), FirstMerge->AssemblyFlight->bIsPlaying);
	CastleBuildingTests::CompletePlacement(FirstMerge);
	FirstMerge = CastleBuildingTests::FindPlacedLevel(Board, 2);
	TestTrue(TEXT("飞入结束后播放局部合并"), FirstMerge && FirstMerge->BuildingMerge->bIsPlaying);
	ACastleBuildingBase* Third = Board->PlaceBuilding(FIntVector(0, 1, 0), Card, false, Reason);
	CastleBuildingTests::CompletePlacement(Third);
	Result = Board->PlaceBuilding(FIntVector(1, 1, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("进行中的建筑可继续接收新放置"), Result)) { AddError(Reason.ToString()); return false; }
	TestTrue(TEXT("再次放置期间仍播放 Assembly Flight"), Result->AssemblyFlight->bIsPlaying);
	CastleBuildingTests::CompletePlacement(Result);
	Result = CastleBuildingTests::FindPlacedLevel(Board, 3);
	if (!TestNotNull(TEXT("第二次飞入完成后再升级到三级"), Result)) { return false; }
	TestTrue(TEXT("交接动画成功且没有表现错误"), Result->BuildingMerge->bIsPlaying && Reason.IsEmpty());
	TestTrue(TEXT("连续操作没有漏掉已完成放置的零件"), Result->BuildingMerge->SourcePartCount > 0);
	TestEqual(TEXT("交接后仍只保留一个最终建筑"), Board->GetPlacedBuildingCount(), 1);
	Result->BuildingMerge->TickComponent(10.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("交接后正常完成"), Result->BuildingMerge->bIsPlaying);
	return true;
}

/** 后放置的建筑先完成也不能提前升级；等待邻居飞入落稳后再检查合并。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDeferredPlacementMergeTest,
	"MovingCastle.Components.Animation.BuildingMerge.WaitForAllPlacementFlights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDeferredPlacementMergeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CastleMergeTests::MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	ACastleBoard* Board = CastleMergeTests::MakeBoard(World, Definition);
	FCastleBuildingDescriptor Card;
	FText Reason;
	Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason);
	ACastleBuildingBase* Earlier = Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Reason);
	ACastleBuildingBase* Later = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("较早放置的建筑存在"), Earlier) || !TestNotNull(TEXT("要求自动合成的新建筑存在"), Later)) { return false; }
	TestTrue(TEXT("两座新建筑都播放原有 Assembly Flight"), Earlier->AssemblyFlight->bIsPlaying && Later->AssemblyFlight->bIsPlaying);
	TestEqual(TEXT("飞入期间新建筑仍保持一级"), Later->GetBuildingLevel(), 1);
	CastleBuildingTests::CompletePlacement(Later);
	TestEqual(TEXT("后放置的建筑先落稳也必须等待另一座"), Board->GetPlacedBuildingCount(), 2);
	TestNull(TEXT("等待期间不出现二级 Actor"), CastleBuildingTests::FindPlacedLevel(Board, 2));
	TestTrue(TEXT("较早放置的邻居仍在飞入"), Earlier->AssemblyFlight->bIsPlaying);
	CastleBuildingTests::CompletePlacement(Earlier);
	ACastleBuildingBase* Result = CastleBuildingTests::FindPlacedLevel(Board, 2);
	TestNotNull(TEXT("最后一座邻居落稳后才生成二级结果"), Result);
	TestEqual(TEXT("两座来源替换成一个结果"), Board->GetPlacedBuildingCount(), 1);
	TestTrue(TEXT("局部合并从已完成的放置动画之后开始"), Result && Result->BuildingMerge->bIsPlaying);
	return true;
}

/** 取消放置飞入明确丢弃升级请求，不把半装好的建筑升级成正式结果。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCancelledPlacementMergeTest,
	"MovingCastle.Components.Animation.BuildingMerge.CancelledPlacementDoesNotMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCancelledPlacementMergeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CastleMergeTests::MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	ACastleBoard* Board = CastleMergeTests::MakeBoard(World, Definition);
	FCastleBuildingDescriptor Card;
	FText Reason;
	Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason);
	ACastleBuildingBase* First = Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Reason);
	CastleBuildingTests::CompletePlacement(First);
	ACastleBuildingBase* Cancelled = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("新建筑进入放置动画"), Cancelled)) { return false; }
	AddExpectedMessage(TEXT("放置中止，不触发合并"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1, false);
	Cancelled->AssemblyFlight->ResetAssembly();
	TestFalse(TEXT("取消后不能标记放置完成"), Cancelled->bHasCompletedPlacement);
	TestEqual(TEXT("取消不会触发合成"), Board->GetPlacedBuildingCount(), 2);
	TestNull(TEXT("场上没有升级结果"), CastleBuildingTests::FindPlacedLevel(Board, 2));
	TestTrue(TEXT("取消恢复新建筑的原始模型"), Cancelled->GetVisualBounds().IsValid != 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
