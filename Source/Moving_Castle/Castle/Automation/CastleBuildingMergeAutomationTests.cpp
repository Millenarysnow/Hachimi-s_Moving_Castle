#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingMergeComponent.h"
#include "Components/Animation/Matching/BuildingPartCatalog.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleBuildingTestUtilities.h"
#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
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

/** 刀盾真实拆件模型：两座各 9 件一级塔合并后复用 14 件，4 件移除、4 件新增，形成 18 件二级塔。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleRealBuildingMergeTest,
	"MovingCastle.Components.Animation.BuildingMerge.RealBuilding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleRealBuildingMergeTest::RunTest(const FString& Parameters)
{
	UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_ArcherTower.DA_ArcherTower"));
	if (!TestNotNull(TEXT("正式箭塔定义存在"), Definition)) { return false; }
	if (!TestNotNull(TEXT("正式箭塔零件目录存在"), Definition->PartCatalog.Get())) { return false; }
	UWorld* World = CastleMergeTests::MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
	ACastleBoard* Board = CastleMergeTests::MakeBoard(World, Definition);
	FCastleBuildingDescriptor Card;
	FText Reason;
	Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason);
	ACastleBuildingBase* First = Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Reason);
	if (!TestNotNull(TEXT("放置真实一级箭塔"), First)) { return false; }
	if (!CastleBuildingTests::CompletePlacement(First)) { AddError(TEXT("原有一级箭塔飞入失败。")); return false; }
	// 飞入结束后，常规屋面还有一次 PCG 视觉过渡。先到达稳定状态再核对本用例的合并来源。
	First->BuildingMerge->TickComponent(10.0f, LEVELTICK_All, nullptr);
	if (!TestFalse(TEXT("第一座塔的四分屋顶过渡已结束"), First->BuildingMerge->bIsPlaying)) { return false; }
	const auto CountVisibleParts = [](ACastleBuildingBase& Building)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Building.GetComponents<UStaticMeshComponent>(Meshes);
		return Meshes.FilterByPredicate([](const UStaticMeshComponent* Mesh)
		{
			// 功能楼板不在合并的美术快照中。四分屋角是可见目标的一部分。
			return Mesh->GetStaticMesh() && Mesh->IsVisible() && !Mesh->bHiddenInGame
				&& !Mesh->ComponentHasTag(TEXT("MergeExclude")) && !UBuildingGameplayRootComponent::IsInGameplayHierarchy(Mesh);
		}).Num();
	};
	TestTrue(TEXT("首塔稳定后有可参与合并的美术件"), CountVisibleParts(*First) > 0);
	TArray<UStaticMeshComponent*> FirstMeshes;
	First->GetComponents<UStaticMeshComponent>(FirstMeshes);
	TestEqual(TEXT("首塔稳定后有四片可见屋角"), FirstMeshes.FilterByPredicate([](const UStaticMeshComponent* Mesh)
	{
		return Mesh->IsVisible() && Mesh->ComponentHasTag(TEXT("CastlePCGGroup=RoofQuarter"));
	}).Num(), 4);
	ACastleBuildingBase* Result = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("新建筑先开始原有放置飞入"), Result)) { AddError(Reason.ToString()); return false; }
	TestFalse(TEXT("放置飞入期间不提前合并"), Result->BuildingMerge->bIsPlaying);
	TestEqual(TEXT("飞入期间仍保留两座一级建筑"), Board->GetPlacedBuildingCount(), 2);
	CastleBuildingTests::CompletePlacement(Result);
	Result = CastleBuildingTests::FindPlacedLevel(Board, 2);
	if (!TestNotNull(TEXT("新建筑飞入完成后才触发真实合并"), Result)) { AddError(Reason.ToString()); return false; }
	UBuildingMergeComponent* Merge = Result->BuildingMerge;
	TestTrue(TEXT("合并自动启动原生组件"), Merge->bIsPlaying && Reason.IsEmpty());
	// 棋盘先启动等级合并，再由 PCG 捕获途中快照并接续。此处的来源计数属于当前过渡。
	const int32 SourceCount = Merge->SourcePartCount;
	if (!TestNotNull(TEXT("二级使用持久化 V4 装配"), Result->HachimisLayout.Get())) { return false; }
	TestTrue(TEXT("接续捕获了需要退场的旧屋面与其他美术件"), SourceCount > Merge->TargetPartCount);
	TestTrue(TEXT("目录匹配跨等级同形零件，其中有原位和移动件"), Merge->ReusedPartCount > Merge->MovedPartCount && Merge->MovedPartCount > 0);
	TestTrue(TEXT("新出现的零件从场外飞入"), Merge->AddedPartCount > 0 && Merge->IncomingPartCount >= Merge->AddedPartCount);
	TestEqual(TEXT("原零件复用和退场合计覆盖全部来源"), Merge->ReusedPartCount + Merge->RemovedPartCount, SourceCount);
	TestEqual(TEXT("复用与新增合计覆盖所有目标"), Merge->ReusedPartCount + Merge->AddedPartCount, Merge->TargetPartCount);
	TestFalse(TEXT("旧建筑已经被规则销毁"), IsValid(First));
	TestEqual(TEXT("逻辑只保留一个二级建筑"), Board->GetPlacedBuildingCount(), 1);
	TestEqual(TEXT("仍占两格"), Board->GetOccupiedRoomCellCount(), 2);

	TArray<UStaticMeshComponent*> Meshes;
	Result->GetComponents<UStaticMeshComponent>(Meshes);
	TMap<UStaticMeshComponent*, TPair<FTransform, bool>> Authored;
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		// 动画期间正式美术件会隐藏；用已登记的几何目录辨认这些原件，不能把本就隐藏的辅助 Cube 计入。
		if (Mesh->GetStaticMesh() && Definition->PartCatalog->FindGeometry(Mesh->GetStaticMesh())
			&& !Mesh->HasAnyFlags(RF_Transient) && !Mesh->ComponentHasTag(TEXT("MergeExclude"))
			&& !UBuildingGameplayRootComponent::IsInGameplayHierarchy(Mesh))
		{
			Authored.Add(Mesh, {Mesh->GetRelativeTransform(), Merge->GetStableVisibility(Mesh)});
		}
	}
	TestEqual(TEXT("蓝图所有作者节点继续保留"), Authored.Num(), Result->HachimisLayout->Parts.Num());
	const int32 TargetCount = Merge->TargetPartCount;
	Merge->TickComponent(1.6f, LEVELTICK_All, nullptr);
	Merge->TickComponent(5.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("真实模型合并完成"), Merge->bIsPlaying);
	TestEqual(TEXT("目标件数等于完成后实际可见的模型组件"), CountVisibleParts(*Result), TargetCount);
	for (const auto& Pair : Authored)
	{
		TestTrue(TEXT("真实零件精确保留作者局部变换"), Pair.Key->GetRelativeTransform().Equals(Pair.Value.Key));
		TestEqual(TEXT("真实零件恢复各自的目标显隐"), Pair.Key->IsVisible(), Pair.Value.Value);
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
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
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
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
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
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
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
