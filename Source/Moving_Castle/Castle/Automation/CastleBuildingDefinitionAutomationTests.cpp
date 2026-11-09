// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CastleBackpackComponent.h"
#include "CastleBuildingTestUtilities.h"
#include "CastleBuildingPreviewSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/** 验证缺等级、冲突模型与非法属性被明确拒绝，空模型仍是合法定义。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingDefinitionValidationTest, "MovingCastle.Logic.BuildingDefinitionValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingDefinitionValidationTest::RunTest(const FString& Parameters)
{
	UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	FText Reason;
	TestTrue(TEXT("所有等级留空模型时定义仍然有效"), Definition->ValidateConfiguration(Reason));
	FCastleBuildingDescriptor Card;
	TestTrue(TEXT("定义工厂创建二级卡牌"), Definition->CreateDescriptor(2, ECastleTileColor::Red, 3, Card, Reason));
	TestTrue(TEXT("卡牌保留准确资产身份与等级类"), Card.Definition == Definition && Card.GetBuildingClass() == ACastleArcherTowerLevel2::StaticClass());
	TestFalse(TEXT("不存在的等级不能以其他等级替代"), Definition->CreateDescriptor(6, ECastleTileColor::Red, 3, Card, Reason));
	Definition->Levels[1].Level = 3;
	TestFalse(TEXT("重复和跳号等级会被拒绝"), Definition->ValidateConfiguration(Reason));
	Definition->Levels[1].Level = 2;
	Definition->Levels[0].StaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Definition->Levels[0].ModelActorClass = AActor::StaticClass();
	TestFalse(TEXT("同一级不能配置两个模型来源"), Definition->ValidateConfiguration(Reason));
	return true;
}

/** 在真实 World 中验证模型、空模型占位、合成升级和回收完整保留 Data Asset。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingDefinitionPlacementTest, "MovingCastle.Logic.BuildingDefinitionPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingDefinitionPlacementTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建真实建造世界"), World)) { return false; }
	// 合成会真实销毁旧 Actor，因此测试也必须提供 WorldContext，避免走无上下文的销毁路径。
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	Board->GridSize = FIntVector(2, 2, 3);
	UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	Definition->Levels[0].StaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Definition->Levels[0].ModelTransform.SetScale3D(FVector(2.0f));
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
	if (!TestTrue(TEXT("配置测试地面"), Board->ApplyGroundGenerationResult(Ground, Reason))) { return false; }
	FCastleBuildingDescriptor Card;
	Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason);
	ACastleBuildingBase* First = Board->PlaceBuilding(FIntVector(0, 0, 0), Card, false, Reason);
	if (!TestNotNull(TEXT("一级模型放置成功"), First)) { AddError(Reason.ToString()); return false; }
	TestFalse(TEXT("配置静态网格时不使用占位"), First->IsUsingPlaceholder());
	bool bFoundConfiguredMesh = false;
	TArray<UStaticMeshComponent*> Meshes;
	First->GetComponents<UStaticMeshComponent>(Meshes);
	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		bFoundConfiguredMesh |= Mesh->GetStaticMesh() == Definition->Levels[0].StaticMesh.Get();
	}
	TestTrue(TEXT("实际 Actor 包含定义中配置的模型"), bFoundConfiguredMesh);
	ACastleBuildingBase* Merged = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("两张一级卡自动合成"), Merged)) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("合成结果为二级"), Merged->GetBuildingLevel(), 2);
	TestTrue(TEXT("升级后仍引用同一建筑定义"), Merged->GetBuildingDefinition() == Definition);
	TestTrue(TEXT("二级没有配置模型时使用立方体"), Merged->IsUsingPlaceholder());
	TestEqual(TEXT("合成建筑占用两格"), Merged->GetOccupiedCoordinates().Num(), 2);
	TestTrue(TEXT("回收成功"), Board->RemoveBuildingToBackpack(Merged, Board->PrototypeBackpack, Reason));
	FCastleBuildingDescriptor Recovered;
	TestTrue(TEXT("回收后的卡牌存在"), Board->PrototypeBackpack->TryGetItemAt(0, Recovered));
	TestTrue(TEXT("背包保留同一 Data Asset、等级与卡牌属性"), Recovered.Definition == Definition
		&& Recovered.Level == 2 && Recovered.Color == Card.Color && Recovered.Number == Card.Number);
	ACastleBuildingBase* Replaced = Board->PlaceBuildingFromBackpack(FIntVector(0, 0, 0), Board->PrototypeBackpack, 0, false, Reason);
	if (!TestNotNull(TEXT("升级卡回收后可按等级占用范围重新放置"), Replaced)) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("二级卡重新放置仍占两格"), Replaced->GetOccupiedCoordinates().Num(), 2);
	TestTrue(TEXT("重新放置仍引用同一定义"), Replaced->GetBuildingDefinition() == Definition);
	Board->RemoveBuilding(Replaced, Reason);

	// 同类逻辑类、颜色和数字相同，但不同资产定义的建筑不能混合升级。
	ACastleBuildingBase* Original = Board->PlaceBuilding(FIntVector(0, 0, 0), Card, false, Reason);
	UCastleBuildingDefinition* OtherDefinition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	OtherDefinition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason);
	ACastleBuildingBase* Other = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	TestTrue(TEXT("不同定义的同类建筑不会误合成"), Original != nullptr && Other != nullptr && Board->GetPlacedBuildingCount() == 2);
	return true;
}

#endif
