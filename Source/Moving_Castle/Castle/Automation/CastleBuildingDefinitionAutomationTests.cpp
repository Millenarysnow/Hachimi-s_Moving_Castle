// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CastleBackpackComponent.h"
#include "CastleBuildingTestUtilities.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/** 验证缺等级与非法逻辑类被明确拒绝，空模型仍是合法定义。 */
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
	Definition->Levels[0].BuildingClass = ACastleArcherTowerLevel2::StaticClass();
	TestFalse(TEXT("等级逻辑类与配置等级不一致时被拒绝"), Definition->ValidateConfiguration(Reason));
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
	TestFalse(TEXT("首次放置先播放原有飞入"), First->bHasCompletedPlacement);
	TestTrue(TEXT("原有放置飞入完整完成"), CastleBuildingTests::CompletePlacement(First));
	TestTrue(TEXT("没有模型组件的等级使用统一立方体占位"), First->IsUsingPlaceholder());
	ACastleBuildingBase* Merged = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("第二张一级卡先放入棋盘"), Merged)) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("第二座建筑飞入期间仍保持一级"), Board->GetPlacedBuildingCount(), 2);
	CastleBuildingTests::CompletePlacement(Merged);
	Merged = CastleBuildingTests::FindPlacedLevel(Board, 2);
	if (!TestNotNull(TEXT("飞入完成后两张一级卡自动合成"), Merged)) { return false; }
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
	TestTrue(TEXT("重新放置仍播放原有 Assembly Flight"), CastleBuildingTests::CompletePlacement(Replaced));
	TestEqual(TEXT("二级卡重新放置仍占两格"), Replaced->GetOccupiedCoordinates().Num(), 2);
	TestTrue(TEXT("重新放置仍引用同一定义"), Replaced->GetBuildingDefinition() == Definition);
	Board->RemoveBuilding(Replaced, Reason);

	// 同类逻辑类、颜色和数字相同，但不同资产定义的建筑不能混合升级。
	ACastleBuildingBase* Original = Board->PlaceBuilding(FIntVector(0, 0, 0), Card, false, Reason);
	CastleBuildingTests::CompletePlacement(Original);
	UCastleBuildingDefinition* OtherDefinition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	OtherDefinition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason);
	ACastleBuildingBase* Other = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	CastleBuildingTests::CompletePlacement(Other);
	TestTrue(TEXT("不同定义的同类建筑不会误合成"), Original != nullptr && Other != nullptr && Board->GetPlacedBuildingCount() == 2);
	return true;
}

/**
 * 使用正式定义与正式箭塔蓝图验证完整生成链路。
 * 先保存蓝图直接生成时的组件变换，再与棋盘合成、回收重新放置的结果比较，
 * 防止未来再次由生成流程覆盖模型作者在蓝图里填写的缩放或局部变换。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectBuildingBlueprintTest, "MovingCastle.Logic.DirectBuildingBlueprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDirectBuildingBlueprintTest::RunTest(const FString& Parameters)
{
	UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_ArcherTower.DA_ArcherTower"));
	if (!TestNotNull(TEXT("加载正式箭塔定义"), Definition)) { return false; }
	FText Reason;
	if (!TestTrue(TEXT("正式定义配置有效"), Definition->ValidateConfiguration(Reason))) { AddError(Reason.ToString()); return false; }
	const FCastleBuildingLevelDefinition* Level2 = Definition->FindLevel(2);
	if (!TestNotNull(TEXT("定义存在二级"), Level2)) { return false; }
	// 从正式等级定义取得蓝图，资产移动到 BP 目录后仍验证真实生成类，不依赖旧导入目录。
	UClass* BlueprintClass = Level2->BuildingClass.Get();
	if (!TestNotNull(TEXT("加载二级建筑蓝图"), BlueprintClass)) { return false; }
	TestTrue(TEXT("Data Asset 直接选择二级蓝图"), BlueprintClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint));
	TestTrue(TEXT("二级蓝图继承对应 C++ 等级类"), BlueprintClass->IsChildOf(ACastleArcherTowerLevel2::StaticClass()));

	const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建蓝图回归测试世界"), World)) { return false; }
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

	// 原生 SpawnActor 的结果作为模型作者的配置基线，不把 0.1 等具体美术数值硬编码进测试。
	ACastleBuildingBase* Reference = World->SpawnActor<ACastleBuildingBase>(BlueprintClass);
	if (!TestNotNull(TEXT("直接生成蓝图基线 Actor"), Reference)) { return false; }
	TArray<USceneComponent*> ReferenceComponents;
	Reference->GetComponents<USceneComponent>(ReferenceComponents);
	TMap<FName, FTransform> AuthoredTransforms;
	for (const USceneComponent* Component : ReferenceComponents)
	{
		if (Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript)
		{
			AuthoredTransforms.Add(Component->GetFName(), Component->GetRelativeTransform());
		}
	}
	TestTrue(TEXT("蓝图包含直接模型组件"), !AuthoredTransforms.IsEmpty());
	const FVector AuthoredRootScale = Reference->GetActorScale3D();
	Reference->Destroy();

	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	Board->GridSize = FIntVector(2, 2, 3);
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
	if (!TestTrue(TEXT("配置正式蓝图测试地面"), Board->ApplyGroundGenerationResult(Ground, Reason))) { return false; }
	FCastleBuildingDescriptor Card;
	Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason);
	ACastleBuildingBase* FirstPlacement = Board->PlaceBuilding(FIntVector(0, 0, 0), Card, false, Reason);
	if (!TestNotNull(TEXT("放置第一座一级箭塔"), FirstPlacement)) { return false; }
	if (!CastleBuildingTests::CompletePlacement(FirstPlacement)) { AddError(TEXT("第一座箭塔飞入失败。")); return false; }
	ACastleBuildingBase* Merged = Board->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("第二座正式蓝图先进入棋盘"), Merged)) { AddError(Reason.ToString()); return false; }
	CastleBuildingTests::CompletePlacement(Merged);
	Merged = CastleBuildingTests::FindPlacedLevel(Board, 2);
	if (!TestNotNull(TEXT("两座正式蓝图在飞入完成后合成为二级"), Merged)) { AddError(Reason.ToString()); return false; }

	const auto CheckDirectBlueprint = [this, &AuthoredTransforms, BlueprintClass, AuthoredRootScale](ACastleBuildingBase* Building)
	{
		TestTrue(TEXT("实际建筑自身就是等级蓝图"), Building->GetClass() == BlueprintClass);
		TestFalse(TEXT("正式蓝图没有使用立方体"), Building->IsUsingPlaceholder());
		TestTrue(TEXT("原生根缩放未被覆盖"), Building->GetActorScale3D().Equals(AuthoredRootScale));
		TArray<UChildActorComponent*> ChildActors;
		Building->GetComponents<UChildActorComponent>(ChildActors);
		TestEqual(TEXT("建筑不再创建模型 Child Actor"), ChildActors.Num(), 0);
		TArray<USceneComponent*> Components;
		Building->GetComponents<USceneComponent>(Components);
		int32 Compared = 0;
		for (const USceneComponent* Component : Components)
		{
			if (const FTransform* Authored = AuthoredTransforms.Find(Component->GetFName()))
			{
				TestTrue(FString::Printf(TEXT("组件 %s 保留蓝图局部变换"), *Component->GetName()),
					Component->GetRelativeTransform().Equals(*Authored));
				++Compared;
			}
		}
		TestEqual(TEXT("全部蓝图模型组件得到保留"), Compared, AuthoredTransforms.Num());
	};
	CheckDirectBlueprint(Merged);
	TestTrue(TEXT("二级建筑回收到背包"), Board->RemoveBuildingToBackpack(Merged, Board->PrototypeBackpack, Reason));
	ACastleBuildingBase* Replaced = Board->PlaceBuildingFromBackpack(FIntVector(0, 0, 0), Board->PrototypeBackpack, 0, false, Reason);
	if (!TestNotNull(TEXT("背包二级卡重新直接生成蓝图"), Replaced)) { AddError(Reason.ToString()); return false; }
	CastleBuildingTests::CompletePlacement(Replaced);
	CheckDirectBlueprint(Replaced);
	Board->RemoveBuilding(Replaced, Reason);

	FirstPlacement = Board->PlaceBuilding(FIntVector(0, 0, 0), Card, false, Reason);
	CastleBuildingTests::CompletePlacement(FirstPlacement);
	ACastleBuildingBase* MergedAlongY = Board->PlaceBuilding(FIntVector(0, 1, 0), Card, true, Reason);
	if (!TestNotNull(TEXT("沿 Y 放置后启动飞入"), MergedAlongY)) { AddError(Reason.ToString()); return false; }
	CastleBuildingTests::CompletePlacement(MergedAlongY);
	MergedAlongY = CastleBuildingTests::FindPlacedLevel(Board, 2);
	if (!TestNotNull(TEXT("沿 Y 飞入完成后仍生成二级蓝图"), MergedAlongY)) { AddError(Reason.ToString()); return false; }
	CheckDirectBlueprint(MergedAlongY);
	TestTrue(TEXT("沿 Y 轴时整体旋转 90 度"), MergedAlongY->GetActorRotation().Equals(FRotator(0.0f, 90.0f, 0.0f)));
	Board->RemoveBuilding(MergedAlongY, Reason);

	// 三级仍按用户确认保留平面四格占位，四级则直接生成自身的八格等级蓝图。
	Definition->CreateDescriptor(3, ECastleTileColor::Red, 3, Card, Reason);
	ACastleBuildingBase* Level3Building = Board->PlaceBuilding(FIntVector(0, 0, 0), Card, false, Reason);
	if (!TestNotNull(TEXT("直接放置三级卡"), Level3Building)) { AddError(Reason.ToString()); return false; }
	CastleBuildingTests::CompletePlacement(Level3Building);
	TestTrue(TEXT("三级继续使用四格占位"), Level3Building->IsUsingPlaceholder()
		&& Level3Building->GetOccupiedCoordinates().Num() == 4);
	Board->RemoveBuilding(Level3Building, Reason);
	Definition->CreateDescriptor(4, ECastleTileColor::Red, 3, Card, Reason);
	ACastleBuildingBase* Level4Building = Board->PlaceBuilding(FIntVector(0, 0, 0), Card, false, Reason);
	if (!TestNotNull(TEXT("直接放置四级卡"), Level4Building)) { AddError(Reason.ToString()); return false; }
	CastleBuildingTests::CompletePlacement(Level4Building);
	TestTrue(TEXT("四级直接使用定义中的蓝图并占八格"), Level4Building->GetClass() == Definition->FindLevel(4)->BuildingClass
		&& Level4Building->GetClass()->IsChildOf(ACastleArcherTowerLevel4::StaticClass())
		&& !Level4Building->IsUsingPlaceholder() && Level4Building->GetOccupiedCoordinates().Num() == 8);
	TArray<UStaticMeshComponent*> Level4Meshes;
	Level4Building->GetComponents<UStaticMeshComponent>(Level4Meshes);
	const int32 Level4PartCount = Level4Meshes.FilterByPredicate([](const UStaticMeshComponent* Component)
	{
		return Component->GetName().StartsWith(TEXT("Mesh_")) && Component->GetStaticMesh() != nullptr;
	}).Num();
	TestEqual(TEXT("四级蓝图保留 288 个模型零件"), Level4PartCount, 288);
	return true;
}

#endif
