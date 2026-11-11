#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "BuildingNavigationActor.h"
#include "CastleBuildings.h"
#include "CastleNavigationSubsystem.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Demo/CastleNavigationDemo.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"

namespace BuildingNavigationGeometryTests
{
	/** 在真正的导航世界里检查碰撞导出结果，避免只调用资格函数而漏掉引擎接入问题。 */
	class FGeometryBoundaryCommand : public IAutomationLatentCommand
	{
	public:
		explicit FGeometryBoundaryCommand(FAutomationTestBase* InTest)
			: Test(InTest), Deadline(FPlatformTime::Seconds() + 60.0) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(TEXT("建筑导航边界测试超时，请检查演示地图与异步导航构建。"));
				return true;
			}
			if (!Demo)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType != EWorldType::Game) { continue; }
					for (TActorIterator<ACastleNavigationDemo> It(Context.World()); It; ++It) { Demo = *It; break; }
				}
				if (!Test->TestNotNull(TEXT("已打开真实导航演示地图"), Demo)) { return true; }
				Navigation = Demo->GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
			}
			if (Demo->Phase < 0) { Test->AddError(TEXT("导航演示自身失败。")); return true; }
			if (Demo->Phase != 3 || !Navigation->IsNavigationReady()) { return false; }

			if (Stage == 0)
			{
				InitialRefreshCount = Navigation->RefreshCount;
				// 同步几何变更沿用正式导航锁，组件构建过程中不会提交中间状态。
				const FScopedCastleNavigationChange Change(Demo->GetWorld(), Demo);
				for (int32 Index = 0; Index < 2; ++Index)
				{
					UClass* Class = Index == 0 ? ABuildingNavigationActor::StaticClass() : ACastleArcherTowerLevel1::StaticClass();
					ABuildingNavigationActor* Building = Demo->GetWorld()->SpawnActor<ABuildingNavigationActor>(Class);
					Buildings.Add(Building);
					if (!Building->GetRootComponent()) { AddScene<USceneComponent>(Building, TEXT("Root"), nullptr); }
					ACastleBuildingBase* PlacedBuilding = Cast<ACastleBuildingBase>(Building);
					UBuildingGameplayRootComponent* Gameplay = PlacedBuilding ? PlacedBuilding->GameplayRoot.Get()
						: AddScene<UBuildingGameplayRootComponent>(Building, TEXT("GameplayRoot"), Building->GetRootComponent());
					USceneComponent* NestedGroup = AddScene<USceneComponent>(Building, TEXT("NestedFloorGroup"), Gameplay);
					const double X = -1100.0 + Index * 800.0;
					// 高于演示地面数百厘米，严格的投影范围不会误命中下方地面。
					const FVector Outside = Demo->GetActorTransform().TransformPosition(FVector(X, -500.0, 1025.0));
					const FVector Inside = Demo->GetActorTransform().TransformPosition(FVector(X, 500.0, 1025.0));
					AddFloor(Building, Building->GetRootComponent(), TEXT("VisualCollision"), Outside);
					AddFloor(Building, NestedGroup, TEXT("GameplayCollision"), Inside);
					OutsidePoints.Add(Outside + FVector(0.0, 0.0, 25.0));
					InsidePoints.Add(Inside + FVector(0.0, 0.0, 25.0));
				}
				Stage = 1;
				return false;
			}

			UNavigationSystemV1* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Demo->GetWorld());
			FNavLocation Projected;
			if (Stage == 1)
			{
				Test->TestEqual(TEXT("两座建筑的全部几何只提交一轮刷新"), Navigation->RefreshCount, InitialRefreshCount + 1);
				for (int32 Index = 0; Index < Buildings.Num(); ++Index)
				{
					Test->TestFalse(TEXT("分支外美术碰撞没有实际 NavMesh 顶面"),
						System->ProjectPointToNavigation(OutsidePoints[Index], Projected, FVector(20.0, 20.0, 20.0)));
					Test->TestTrue(TEXT("功能分支的隐藏楼板生成实际 NavMesh 顶面"),
						System->ProjectPointToNavigation(InsidePoints[Index], Projected, FVector(20.0, 20.0, 20.0)));
				}
				const FScopedCastleNavigationChange Change(Demo->GetWorld(), Demo);
				for (ABuildingNavigationActor* Building : Buildings) { Building->Destroy(); }
				Buildings.Reset();
				Stage = 2;
				return false;
			}
			Test->TestEqual(TEXT("统一移除测试建筑只再刷新一轮"), Navigation->RefreshCount, InitialRefreshCount + 2);
			for (const FVector& Point : InsidePoints)
			{
				Test->TestFalse(TEXT("建筑销毁后功能楼板没有遗留导航几何"),
					System->ProjectPointToNavigation(Point, Projected, FVector(20.0, 20.0, 20.0)));
			}
			return true;
		}

	private:
		/** 建立完整的注册层级，再添加碰撞；不会借助 ConfigureBuildingVisual 禁用美术网格。 */
		template<typename T>
		T* AddScene(AActor* Owner, FName Name, USceneComponent* Parent)
		{
			T* Component = NewObject<T>(Owner, Name);
			Component->SetMobility(EComponentMobility::Movable);
			if (Parent) { Component->SetupAttachment(Parent); }
			else { Owner->SetRootComponent(Component); }
			Owner->AddInstanceComponent(Component);
			Component->RegisterComponent();
			return Component;
		}

		/** 内外楼板使用完全相同的可行走碰撞和导航开关，区别只有附着父级链。 */
		void AddFloor(AActor* Owner, USceneComponent* Parent, FName Name, const FVector& Center)
		{
			UStaticMeshComponent* Floor = AddScene<UStaticMeshComponent>(Owner, Name, Parent);
			Floor->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Floor->SetWorldLocation(Center);
			Floor->SetWorldScale3D(FVector(5.0, 4.0, 0.5));
			Floor->SetCollisionProfileName(TEXT("BlockAll"));
			Floor->SetCanEverAffectNavigation(true);
			Floor->SetHiddenInGame(true);
		}

		FAutomationTestBase* Test;
		ACastleNavigationDemo* Demo = nullptr;
		UCastleNavigationSubsystem* Navigation = nullptr;
		TArray<ABuildingNavigationActor*> Buildings;
		TArray<FVector> OutsidePoints;
		TArray<FVector> InsidePoints;
		double Deadline;
		int32 InitialRefreshCount = 0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingNavigationGeometryTest, "MovingCastle.NavigationDemo.BuildingGeometryBoundary",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBuildingNavigationGeometryTest::RunTest(const FString& Parameters)
{
	// 从现有楼梯/合并演示启动，验证真实 Recast 导出及变化结束后的刷新，不另建测试专用关卡。
	ADD_LATENT_AUTOMATION_COMMAND(BuildingNavigationGeometryTests::FGeometryBoundaryCommand(this));
	return true;
}

#endif
