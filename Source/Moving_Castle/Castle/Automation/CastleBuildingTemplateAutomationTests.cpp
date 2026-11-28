// 使用正式 V4 与盾塔资产验证等级映射、独立拆件、分组层级及功能楼板碰撞边界。

#if WITH_DEV_AUTOMATION_TESTS

#include "CastleBuildingTestUtilities.h"
#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/DecalComponent.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/Building/Effects/Shield/CastleShieldTowerEffectComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"

/** 所有目标等级都走正式卡牌与棋盘入口，不能只在编辑器模板上看起来配置正确。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingNormalRoomTemplateTest,
	"MovingCastle.Logic.BuildingNormalRoomTemplateBlueprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingNormalRoomTemplateTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(true).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建正式模板放置测试世界"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	// 双层正式模板自带内梯，夹具提供它所要求的动态 Recast 数据。
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!TestTrue(TEXT("建筑模板夹具建立导航数据"), Navigation && Navigation->GetDefaultNavDataInstance(FNavigationSystem::Create))) { return false; }
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	// 锁定美术的实体边长和格间距，防止原生默认值与蓝图楼板各自回到旧尺寸。
	TestTrue(TEXT("棋盘单块美术地面保持 500 厘米"), FMath::IsNearlyEqual(Board->GroundTileSize, 500.0f));
	TestTrue(TEXT("棋盘地面间隙匹配美术的 15 厘米"), FMath::IsNearlyEqual(Board->GroundTileGap, 15.0f));
	TestTrue(TEXT("棋盘相邻格中心距为 515 厘米"), FMath::IsNearlyEqual(Board->CellSize, 515.0f));
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

	// 同时验证所有功能建筑的图集编号，覆盖枚举顺序与作者图集编号不同的类别。
	const TMap<ECastleBuildingType, float> ExpectedIcons = {
		{ECastleBuildingType::NormalRoom, 0}, {ECastleBuildingType::Farm, 1}, {ECastleBuildingType::CannonTower, 2},
		{ECastleBuildingType::ArcherTower, 3}, {ECastleBuildingType::ShieldTower, 4}, {ECastleBuildingType::BalloonTower, 5},
		{ECastleBuildingType::BeeHive, 6}
	};
	for (const TCHAR* Family : { TEXT("NormalRoom"), TEXT("ArcherTower"), TEXT("CannonTower"), TEXT("Farm"), TEXT("ShieldTower"), TEXT("BalloonTower"), TEXT("BeeHive") })
	{
		const FString DefinitionPath = FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), Family, Family);
		UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr, *DefinitionPath);
		if (!TestNotNull(TEXT("加载正式普通房间或功能建筑 DA"), Definition)) { return false; }
		Board->BuildingDefinitions = { Definition };
		const bool bNormalRoom = Definition->BuildingType == ECastleBuildingType::NormalRoom;
		const bool bSupport = Family == FString(TEXT("BalloonTower")) || Family == FString(TEXT("BeeHive"));
		const int32 LevelCount = bNormalRoom ? 1 : (bSupport ? 4 : 5);
		TestEqual(TEXT("原建筑等级表"), Definition->Levels.Num(), LevelCount);
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
			TestNull(TEXT("实际实例不再有头顶测试底板"), Building->GetDefaultSubobjectByName(TEXT("PrototypeLabelPlate")));
			TestNull(TEXT("实际实例不再有头顶测试浮字"), Building->GetDefaultSubobjectByName(TEXT("PrototypeLabel")));
			// 从逻辑占格推导世界空间楼板中心，再反算为建筑局部空间；这样也覆盖绕 Z 轴旋转的竖向合成蓝图。
			// 建筑锚点位于最低占格的底面中心；五厘米厚的楼板中心低于对应层面 2.5 厘米。
			const TArray<FIntVector>& OccupiedCoordinates = Building->GetOccupiedCoordinates();
			if (!TestFalse(TEXT("正式建筑至少占用一个逻辑格"), OccupiedCoordinates.IsEmpty())) { return false; }
			const UCastleHachimisV4Layout* Layout = Building->HachimisLayout;
			const bool bAuthorHatch = Layout && Layout->Grid.Z == 2;
			TArray<FVector> ExpectedGameplayFloorLocations;
			TArray<FVector> ExpectedGameplayFloorScales;
			TArray<FVector> ExpectedFloorDecalLocations;
			for (const FIntVector& Coordinate : OccupiedCoordinates)
			{
				// 同一建筑内部的功能碰撞须覆盖格缝；每侧各承担半个间隙，外周边界仍与 500 厘米美术格面一致。
				const float NegativeXExtension = !Layout && OccupiedCoordinates.Contains(Coordinate + FIntVector(-1, 0, 0)) ? Board->GroundTileGap * 0.5f : 0.0f;
				const float PositiveXExtension = !Layout && OccupiedCoordinates.Contains(Coordinate + FIntVector(1, 0, 0)) ? Board->GroundTileGap * 0.5f : 0.0f;
				const float NegativeYExtension = !Layout && OccupiedCoordinates.Contains(Coordinate + FIntVector(0, -1, 0)) ? Board->GroundTileGap * 0.5f : 0.0f;
				const float PositiveYExtension = !Layout && OccupiedCoordinates.Contains(Coordinate + FIntVector(0, 1, 0)) ? Board->GroundTileGap * 0.5f : 0.0f;
				const FVector FloorCenterWorld = Board->GetGridWorldLocation(Coordinate)
					+ Board->GetActorTransform().TransformVector(FVector(
						(PositiveXExtension - NegativeXExtension) * 0.5f,
						(PositiveYExtension - NegativeYExtension) * 0.5f,
						-Board->LayerHeight * 0.5f - 2.5f + (Layout ? Layout->StandingHeight : 0.0)));
				ExpectedGameplayFloorLocations.Add(Building->GetActorTransform().InverseTransformPosition(FloorCenterWorld));
				const FVector FloorSurfaceWorld = FloorCenterWorld
					+ Board->GetActorTransform().TransformVector(FVector(0.0f, 0.0f, 2.5f));
				ExpectedFloorDecalLocations.Add(Building->GetActorTransform().InverseTransformPosition(FloorSurfaceWorld));
				const FVector LocalXAxis = Building->GetActorTransform().InverseTransformVector(Board->GetActorTransform().TransformVector(
					FVector((Layout ? Board->CellSize : Board->GroundTileSize) + NegativeXExtension + PositiveXExtension, 0.0f, 0.0f)));
				const FVector LocalYAxis = Building->GetActorTransform().InverseTransformVector(Board->GetActorTransform().TransformVector(
					FVector(0.0f, (Layout ? Board->CellSize : Board->GroundTileSize) + NegativeYExtension + PositiveYExtension, 0.0f)));
				ExpectedGameplayFloorScales.Add((LocalXAxis.GetAbs() + LocalYAxis.GetAbs() + FVector(0.0f, 0.0f, 5.0f)) / 100.0f);
			}
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
					if (bAuthorHatch)
					{
						// 实梯楼板保留四个可导航条带。每条的顶面须等于对应作者标高。
						const double Top = Mesh->GetRelativeLocation().Z + Mesh->GetRelativeScale3D().Z * 50.0;
						TestTrue(TEXT("分段楼板顶面匹配作者站立高度"), FMath::IsNearlyEqual(Top, Layout->StandingHeight, 0.1)
							|| FMath::IsNearlyEqual(Top, Layout->StandingHeight + Layout->StoryRise, 0.1));
						continue;
					}
					// 每个预期中心只能匹配一次，同时发现重叠楼板、缺失楼板与旧的 520 厘米摆放。
					const int32 MatchingFloorIndex = ExpectedGameplayFloorLocations.IndexOfByPredicate(
						[Mesh](const FVector& ExpectedLocation)
						{
							return ExpectedLocation.Equals(Mesh->GetRelativeLocation(), 0.01f);
						});
					if (MatchingFloorIndex == INDEX_NONE)
					{
						FString ExpectedLocations;
						for (const FVector& ExpectedLocation : ExpectedGameplayFloorLocations)
						{
							ExpectedLocations += ExpectedLocation.ToString() + TEXT(" ");
						}
						AddError(FString::Printf(TEXT("蓝图 %s 的功能地板 %s 实际局部坐标 %s；尚未匹配的局部坐标为 %s。"),
							*BuildingClass->GetPathName(), *Mesh->GetName(), *Mesh->GetRelativeLocation().ToString(), *ExpectedLocations));
						return false;
					}
					TestTrue(TEXT("功能楼板只向同建筑内部邻格延伸半缝，外周保持美术格面边界"),
						Mesh->GetRelativeScale3D().Equals(ExpectedGameplayFloorScales[MatchingFloorIndex], 0.0001f));
					ExpectedGameplayFloorLocations.RemoveAt(MatchingFloorIndex);
					ExpectedGameplayFloorScales.RemoveAt(MatchingFloorIndex);
				}
				else
				{
					++Visuals;
					const USceneComponent* Group = Mesh->GetAttachParent();
					TestTrue(TEXT("每个美术拆件经 BuildingMeshGroup 挂在视觉根下"), Group != nullptr
						&& Group->GetFName() == TEXT("BuildingMeshGroup") && Group->GetAttachParent() == Building->GetRootComponent());
					// 四分规则隐藏原装屋顶时也撤掉它的射线碰撞，避免射线命中已经收掉的屋檐。
					TestEqual(TEXT("可见美术用于选中，隐藏原件关闭碰撞"), Mesh->GetCollisionEnabled(),
						Mesh->IsVisible() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
					TestEqual(TEXT("美术阻挡选中射线"), Mesh->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
					TestEqual(TEXT("美术不阻挡 Pawn"), Mesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
					TestFalse(TEXT("表现组件不参与导航"), Mesh->CanEverAffectNavigation());
					TestFalse(TEXT("Actor 导航边界排除美术"), Building->IsComponentRelevantForNavigation(Mesh));
					if (Layout)
					{
						const FName* Tag = Mesh->ComponentTags.FindByPredicate([](FName Name){ return Name.ToString().StartsWith(TEXT("CastleV4Part=")); });
						const int32 Index = Tag ? FCString::Atoi(*Tag->ToString().Mid(13)) : INDEX_NONE;
						if (!TestTrue(TEXT("作者节点索引有效"), Layout->Parts.IsValidIndex(Index))) { return false; }
						const FCastleV4Part& Part = Layout->Parts[Index];
						TestTrue(TEXT("保持作者局部变换和网格"), Mesh->GetRelativeTransform().Equals(Part.Local, 0.01) && Mesh->GetStaticMesh() == Part.Mesh);
					}
					else
					{
						TestTrue(TEXT("盾塔拆件保持原局部原点和单位缩放"), Mesh->GetRelativeLocation().IsNearlyZero() && Mesh->GetRelativeScale3D().Equals(FVector::OneVector));
					}
				}
			}
			TArray<UDecalComponent*> Decals;
			Building->GetComponents<UDecalComponent>(Decals);
			int32 FloorDecalCount = 0;
			FLinearColor ExpectedFloorTint;
			TestTrue(TEXT("建筑属性色存在统一格面映射"), CastleTileColors::TryGetGridTint(Building->GetBuildingColor(), ExpectedFloorTint));
			for (UDecalComponent* Decal : Decals)
			{
				if (!Decal->ComponentHasTag(TEXT("CastleBuildingFloorDecal"))) { continue; }
				++FloorDecalCount;
				const bool bIsPrimitive = Decal->GetClass()->IsChildOf(UPrimitiveComponent::StaticClass());
				TestTrue(TEXT("地板贴花挂在功能根下且不是碰撞几何或导航数据源"), Decal->GetAttachParent() == Building->GameplayRoot
					&& !bIsPrimitive && !Decal->CanEverAffectNavigation());
				const FVector ExpectedProjectionDirection = Board->GetActorTransform().GetRotation().RotateVector(FVector(0, 0, -1));
				TestTrue(TEXT("地板贴花投影方向朝向楼板"), FVector::DotProduct(
					Decal->GetComponentTransform().GetUnitAxis(EAxis::X), ExpectedProjectionDirection) > 0.999f);
				const int32 MatchingDecalIndex = ExpectedFloorDecalLocations.IndexOfByPredicate(
					[Decal](const FVector& ExpectedLocation)
					{
						return ExpectedLocation.Equals(Decal->GetRelativeLocation(), 0.01f);
					});
				if (MatchingDecalIndex == INDEX_NONE)
				{
					AddError(FString::Printf(TEXT("建筑 %s 的贴花 %s 未对齐任何占格楼板：局部位置 %s。"),
						*BuildingClass->GetPathName(), *GetNameSafe(Decal), *Decal->GetRelativeLocation().ToString()));
					return false;
				}
				ExpectedFloorDecalLocations.RemoveAt(MatchingDecalIndex);
				UMaterialInstanceDynamic* DecalMaterial = Cast<UMaterialInstanceDynamic>(Decal->GetDecalMaterial());
				if (!TestNotNull(TEXT("每格地板使用独立动态贴花材质"), DecalMaterial)) { return false; }
				TestEqual(TEXT("贴花使用新版父材质"), GetPathNameSafe(DecalMaterial->Parent.Get()),
					FString(TEXT("/Game/ART/Material/MI_Floor_02.MI_Floor_02")));
				TestTrue(TEXT("每格地板数字帧与建筑数字一致"), FMath::IsNearlyEqual(
					DecalMaterial->K2_GetScalarParameterValue(TEXT("Number")), static_cast<float>(Building->GetBuildingNumber() - 1)));
				TestEqual(TEXT("每格地板功能图标匹配建筑类别"), DecalMaterial->K2_GetScalarParameterValue(TEXT("Icon")),
					ExpectedIcons.FindChecked(Building->GetBuildingType()));
				TestTrue(TEXT("每格地板底纹颜色与建筑颜色一致"), DecalMaterial->K2_GetVectorParameterValue(TEXT("BackgroundColor"))
					.Equals(ExpectedFloorTint, 0.001f));
			}
			TestEqual(TEXT("每个占格都有独立楼板贴花"), FloorDecalCount, OccupiedCoordinates.Num());
			TestEqual(TEXT("所有楼板贴花都对齐对应楼板表面"), ExpectedFloorDecalLocations.Num(), 0);
			// V4 取自持久化作者节点表。盾塔继续验证原套件的零件数。
			const int32 ShieldCounts[] = { 10, 20, 33, 75, 75 };
			const int32 ExpectedVisuals = Layout ? Layout->Parts.Num() : ShieldCounts[Level - 1];
			const int32 ExpectedFloors = ACastleBuildingBase::GetExpectedFootprintCellCount(Level) + (bAuthorHatch ? 3 : 0);
			TestEqual(TEXT("全部拆件与功能格面均保留"), AuthoredMeshes, ExpectedVisuals + ExpectedFloors);
			TestEqual(TEXT("每个占格配置一个功能格面"), Floors, ExpectedFloors);
			if (!bAuthorHatch) { TestEqual(TEXT("所有逻辑占格都具有对应中心位置的功能地板"), ExpectedGameplayFloorLocations.Num(), 0); }
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
