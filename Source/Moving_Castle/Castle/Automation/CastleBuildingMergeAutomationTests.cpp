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
	TArray<UStaticMeshComponent*> FirstMeshes;
	First->GetComponents<UStaticMeshComponent>(FirstMeshes);
	const int32 SourceCount = FirstMeshes.FilterByPredicate([](const UStaticMeshComponent* Mesh)
	{
		return Mesh->GetStaticMesh() && Mesh->IsVisible() && !Mesh->ComponentHasTag(TEXT("MergeExclude"));
	}).Num() * 2;
	ACastleBuildingBase* Result = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("真实合并成功"), Result)) { AddError(Reason.ToString()); return false; }
	UBuildingMergeComponent* Merge = Result->BuildingMerge;
	TestTrue(TEXT("合并自动启动原生组件"), Merge->bIsPlaying && Reason.IsEmpty());
	TestTrue(TEXT("使用大量真实旧网格而非立方体或粒子"), Merge->SourcePartCount == SourceCount && SourceCount >= 100);
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
	Board->PlaceBuilding(FIntVector(0, 1, 0), Card, false, Reason);
	Card.Level = 1;
	Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Reason);
	ACastleBuildingBase* Result = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("一次放置连续升级有结果"), Result)) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("直接合成到三级"), Result->GetBuildingLevel(), 3);
	TestEqual(TEXT("只抓取三座真实输入建筑，不抓取中间二级"), Result->BuildingMerge->SourcePartCount, 3);
	TestEqual(TEXT("没有残留中间等级占用"), Board->GetPlacedBuildingCount(), 1);
	TestTrue(TEXT("动画中可直接移除结果"), Board->RemoveBuilding(Result, Reason));
	TestEqual(TEXT("移除后清空四格占用"), Board->GetOccupiedRoomCellCount(), 0);

	Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Reason);
	ACastleBuildingBase* FirstMerge = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("先合成一对"), FirstMerge)) { return false; }
	TestTrue(TEXT("前一对还在动画中"), FirstMerge->BuildingMerge->bIsPlaying);
	Board->PlaceBuilding(FIntVector(0, 1, 0), Card, false, Reason);
	Result = Board->PlaceBuilding(FIntVector(1, 1, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("进行中的建筑可继续参与合并"), Result)) { AddError(Reason.ToString()); return false; }
	TestTrue(TEXT("交接动画成功且没有表现错误"), Result->BuildingMerge->bIsPlaying && Reason.IsEmpty());
	TestEqual(TEXT("抓取当前画面中的四件输入网格"), Result->BuildingMerge->SourcePartCount, 4);
	TestEqual(TEXT("交接后仍只保留一个最终建筑"), Board->GetPlacedBuildingCount(), 1);
	Result->BuildingMerge->TickComponent(10.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("交接后正常完成"), Result->BuildingMerge->bIsPlaying);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
