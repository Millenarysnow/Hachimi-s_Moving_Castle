// 使用正式刀盾系列资产验证等级映射、独立拆件、分组层级及功能楼板碰撞边界。

#if WITH_DEV_AUTOMATION_TESTS

#include "CastleBuildingTestUtilities.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Components/Building/Effects/Shield/CastleShieldTowerEffectComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

/** 所有目标等级都走正式卡牌与棋盘入口，不能只在编辑器模板上看起来配置正确。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingNormalRoomTemplateTest,
	"MovingCastle.Logic.BuildingNormalRoomTemplateBlueprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingNormalRoomTemplateTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建正式模板放置测试世界"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	Board->GridSize = FIntVector(2, 2, 2);
	FCastleGroundGenerationResult Ground;
	for (int32 X = 0; X < 2; ++X)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
			Cell.Coordinate = FIntVector(X, Y, 0);
			Cell.Color = ECastleTileColor::Red;
			Cell.Number = 3;
		}
	}
	FText Reason;
	if (!TestTrue(TEXT("配置所有等级的支撑地面"), Board->ApplyGroundGenerationResult(Ground, Reason))) { return false; }

	for (const TCHAR* Family : { TEXT("NormalRoom"), TEXT("ArcherTower"), TEXT("CannonTower"), TEXT("Farm"), TEXT("ShieldTower") })
	{
		const FString DefinitionPath = FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), Family, Family);
		UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr, *DefinitionPath);
		if (!TestNotNull(TEXT("加载正式普通房间或功能建筑 DA"), Definition)) { return false; }
		Board->BuildingDefinitions = { Definition };
		const bool bNormalRoom = Definition->BuildingType == ECastleBuildingType::NormalRoom;
		const int32 LevelCount = bNormalRoom ? 1 : 5;
		TestEqual(TEXT("普通房间仅一级，功能建筑保持完整五级"), Definition->Levels.Num(), LevelCount);
		for (int32 Level = 1; Level <= LevelCount; ++Level)
		{
			FCastleBuildingDescriptor Card;
			if (!Definition->CreateDescriptor(Level, ECastleTileColor::Red, 3, Card, Reason)) { AddError(Reason.ToString()); return false; }
			UClass* BuildingClass = Card.GetBuildingClass();
			const FString ExpectedPath = FString::Printf(TEXT("/Game/MovingCastle/BP/BP_%s_lv%d.BP_%s_lv%d_C"), Family, Level, Family, Level);
			TestEqual(TEXT("DA 直接引用对应命名的等级蓝图"), BuildingClass->GetPathName(), ExpectedPath);
			const FString NativePath = bNormalRoom ? TEXT("/Script/Moving_Castle.CastleNormalRoomBuilding")
				: FString::Printf(TEXT("/Script/Moving_Castle.Castle%sLevel%d"), Family, Level);
			TestEqual(TEXT("每级蓝图直接继承对应 C++ 父类"), BuildingClass->GetSuperClass(), LoadObject<UClass>(nullptr, *NativePath));

			ACastleBuildingBase* Building = Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Reason);
			if (!TestNotNull(TEXT("正式等级蓝图实际放置成功"), Building)) { AddError(Reason.ToString()); return false; }
			if (!TestTrue(TEXT("正式模型飞入完成"), CastleBuildingTests::CompletePlacement(Building))) { return false; }
			TestTrue(TEXT("实际实例使用 DA 中的蓝图类"), Building->GetClass() == BuildingClass);
			TestEqual(TEXT("逻辑占用遵循各等级原规则"), Building->GetOccupiedCoordinates().Num(),
				ACastleBuildingBase::GetExpectedFootprintCellCount(Level));
			TestNull(TEXT("实际实例不再有原生占位组件"), Building->GetDefaultSubobjectByName(TEXT("PrototypeMesh")));
			TArray<UStaticMeshComponent*> Meshes;
			Building->GetComponents<UStaticMeshComponent>(Meshes);
			int32 AuthoredMeshes = 0;
			int32 Floors = 0;
			int32 Visuals = 0;
			for (UStaticMeshComponent* Mesh : Meshes)
			{
				if (Mesh->CreationMethod != EComponentCreationMethod::SimpleConstructionScript) { continue; }
				++AuthoredMeshes;
				if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Mesh))
				{
					++Floors;
					TestTrue(TEXT("地板直接挂在功能根下"), Mesh->GetAttachParent() == Building->GameplayRoot);
					TestEqual(TEXT("功能地板保留普通房间模板的实体碰撞"), Mesh->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
					TestEqual(TEXT("功能地板继续阻挡 Pawn"), Mesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
					TestEqual(TEXT("功能地板忽略选中射线"), Mesh->GetCollisionResponseToChannel(ECC_Visibility), ECR_Ignore);
					TestTrue(TEXT("功能地板保留导航参与开关"), Mesh->CanEverAffectNavigation());
					TestTrue(TEXT("功能格面为 500×500 厘米"), Mesh->GetRelativeScale3D().Equals(FVector(5, 5, 0.05)));
				}
				else
				{
					++Visuals;
					const USceneComponent* Group = Mesh->GetAttachParent();
					TestTrue(TEXT("每个美术拆件经 BuildingMeshGroup 挂在视觉根下"), Group != nullptr
						&& Group->GetFName() == TEXT("BuildingMeshGroup") && Group->GetAttachParent() == Building->GetRootComponent());
					TestEqual(TEXT("美术只参与选中查询"), Mesh->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
					TestEqual(TEXT("美术阻挡选中射线"), Mesh->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
					TestEqual(TEXT("美术不阻挡 Pawn"), Mesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
					TestFalse(TEXT("表现组件不参与导航"), Mesh->CanEverAffectNavigation());
					TestFalse(TEXT("Actor 导航边界排除美术"), Building->IsComponentRelevantForNavigation(Mesh));
					TestTrue(TEXT("拆件保持导入后的局部原点"), Mesh->GetRelativeLocation().Equals(FVector::ZeroVector));
					TestTrue(TEXT("表现组件保持模板配置的单位缩放"), Mesh->GetRelativeScale3D().Equals(FVector::OneVector));
				}
			}
			// 件数来自各 FBX 的独立 Geometry，五级复用 LV4 美术但功能仍是单格。
			const int32 StandardCounts[] = { 9, 18, 29, 71, 71 };
			const int32 ShieldCounts[] = { 10, 20, 33, 75, 75 };
			const int32 ExpectedVisuals = bNormalRoom ? 9 : (Definition->BuildingType == ECastleBuildingType::ShieldTower
				? ShieldCounts[Level - 1] : StandardCounts[Level - 1]);
			const int32 ExpectedFloors = ACastleBuildingBase::GetExpectedFootprintCellCount(Level);
			TestEqual(TEXT("全部拆件与功能格面均保留"), AuthoredMeshes, ExpectedVisuals + ExpectedFloors);
			TestEqual(TEXT("每个占格配置一个功能格面"), Floors, ExpectedFloors);
			TestEqual(TEXT("美术独立件数与源 FBX 一致"), Visuals, ExpectedVisuals);
			if (Definition->BuildingType == ECastleBuildingType::ShieldTower)
			{
				// 盾塔使用唯一的原生护盾组件；攻击 DA 仍只属于箭塔和炮塔。
				if (!TestNotNull(TEXT("盾塔保留共同效果组件"), Building->BuildingEffect.Get())) { return false; }
				TestEqual(TEXT("盾塔使用原生护盾效果而非攻击实现"), Building->BuildingEffect->GetClass(), UCastleShieldTowerEffectComponent::StaticClass());
				TestNull(TEXT("盾塔定义不配置攻击 DA"), Definition->AttackData.Get());
				TestTrue(TEXT("盾塔接入统一触发入口"), Building->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
				TestEqual(TEXT("触发反馈来自建筑共同状态"), Building->BuildingEffect->TriggerCount, int64(1));
			}
			if (!Board->RemoveBuilding(Building, Reason)) { AddError(Reason.ToString()); return false; }
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
