// 使用正式炮塔、农田与盾塔资产验证等级映射、双组件结构及放置后的碰撞边界。

#if WITH_DEV_AUTOMATION_TESTS

#include "CastleBuildingTestUtilities.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

/** 所有目标等级都走正式卡牌与棋盘入口，不能只在编辑器模板上看起来配置正确。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingRoadTemplateTest,
	"MovingCastle.Logic.BuildingRoadTemplateBlueprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingRoadTemplateTest::RunTest(const FString& Parameters)
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

	for (const TCHAR* Family : { TEXT("CannonTower"), TEXT("Farm"), TEXT("ShieldTower") })
	{
		const FString DefinitionPath = FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), Family, Family);
		UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr, *DefinitionPath);
		if (!TestNotNull(TEXT("加载正式炮塔、农田或盾塔 DA"), Definition)) { return false; }
		Board->BuildingDefinitions = { Definition };
		TestEqual(TEXT("每种模板建筑都配置完整五级"), Definition->Levels.Num(), 5);
		for (int32 Level = 1; Level <= 5; ++Level)
		{
			FCastleBuildingDescriptor Card;
			if (!Definition->CreateDescriptor(Level, ECastleTileColor::Red, 3, Card, Reason)) { AddError(Reason.ToString()); return false; }
			UClass* BuildingClass = Card.GetBuildingClass();
			const FString ExpectedPath = FString::Printf(TEXT("/Game/MovingCastle/BP/BP_%s_lv%d.BP_%s_lv%d_C"), Family, Level, Family, Level);
			TestEqual(TEXT("DA 直接引用对应命名的等级蓝图"), BuildingClass->GetPathName(), ExpectedPath);
			const FString NativePath = FString::Printf(TEXT("/Script/Moving_Castle.Castle%sLevel%d"), Family, Level);
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
					TestEqual(TEXT("功能地板保留道路模板的实体碰撞"), Mesh->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
					TestEqual(TEXT("功能地板继续阻挡 Pawn"), Mesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
				}
				else
				{
					++Visuals;
					TestTrue(TEXT("表现组件直接挂在视觉根下"), Mesh->GetAttachParent() == Building->GetRootComponent());
					TestEqual(TEXT("初始化与飞入后表现组件仍无碰撞"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
					TestFalse(TEXT("表现组件不参与导航"), Mesh->CanEverAffectNavigation());
					TestTrue(TEXT("表现组件保持模板配置的局部位置"), Mesh->GetRelativeLocation().Equals(FVector(0, 0, 320)));
					TestTrue(TEXT("表现组件保持模板配置的单位缩放"), Mesh->GetRelativeScale3D().Equals(FVector::OneVector));
				}
			}
			TestEqual(TEXT("蓝图恰好新增两个网格组件"), AuthoredMeshes, 2);
			TestEqual(TEXT("只有一个功能地板"), Floors, 1);
			TestEqual(TEXT("只有一个纯表现模型"), Visuals, 1);
			if (Definition->BuildingType == ECastleBuildingType::ShieldTower)
			{
				// 盾塔当前只复用共同触发计数；不能误继承攻击组件或复制炮塔的攻击数据。
				if (!TestNotNull(TEXT("盾塔保留共同效果组件"), Building->BuildingEffect.Get())) { return false; }
				TestEqual(TEXT("盾塔使用基础效果而非攻击实现"), Building->BuildingEffect->GetClass(), UCastleBuildingEffectComponent::StaticClass());
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
