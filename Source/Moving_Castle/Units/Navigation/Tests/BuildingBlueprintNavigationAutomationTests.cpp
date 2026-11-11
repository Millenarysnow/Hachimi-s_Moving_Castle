#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleGroundTileActor.h"
#include "CastleNavigationSubsystem.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"

namespace BuildingBlueprintNavigationTests
{
	/**
	 * 通过正式 Data Asset、棋盘放置和原有飞入流程验证真实蓝图楼板。
	 *
	 * 该测试不手工创建替代组件，也不直接修改 NavMesh 参数，确保覆盖作者实际
	 * 调整后的 BP_ArcherTower_lv1 以及地面覆盖策略的运行时顺序。
	 */
	class FPlacementCommand : public IAutomationLatentCommand
	{
	public:
		explicit FPlacementCommand(FAutomationTestBase* InTest)
			: Test(InTest), Deadline(FPlatformTime::Seconds() + 60.0) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(TEXT("真实蓝图楼板导航回归测试超时。"));
				return true;
			}

			if (Board == nullptr)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType != EWorldType::Game) { continue; }
					for (TActorIterator<ACastleBoard> It(Context.World()); It; ++It)
					{
						Board = *It;
						break;
					}
				}
				if (!Test->TestNotNull(TEXT("已加载 TestMap1 正式棋盘"), Board)) { return true; }
				Navigation = Board->GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
				if (!Test->TestNotNull(TEXT("棋盘拥有导航子系统"), Navigation)) { return true; }
			}

			if (!Navigation->IsNavigationReady())
			{
				return false;
			}

			UNavigationSystemV1* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Board->GetWorld());
			if (!Test->TestNotNull(TEXT("正式棋盘拥有导航系统"), System)) { return true; }

			if (Stage == 0)
			{
				UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
					TEXT("/Game/MovingCastle/Data/Buildings/DA_ArcherTower.DA_ArcherTower"));
				if (!Test->TestNotNull(TEXT("加载一级箭塔定义"), Definition)) { return true; }

				for (int32 X = 0; X < Board->GridSize.X; ++X)
				{
					FCastleGroundCell Cell;
					if (!Board->TryGetGroundCell(FIntVector(X, 0, 0) + Board->GridOrigin, Cell)
						|| Cell.Color == ECastleTileColor::Gray)
					{
						continue;
					}

					FCastleBuildingDescriptor Card;
					FText Failure;
					if (!Definition->CreateDescriptor(1, Cell.Color, Cell.Number, Card, Failure))
					{
						Test->AddError(Failure.ToString());
						return true;
					}

					InitialRefreshCount = Navigation->RefreshCount;
					Building = Board->PlaceBuilding(Cell.Coordinate, Card, false, Failure);
					if (!Test->TestNotNull(TEXT("正式放置调整后的一级箭塔"), Building)) { return true; }
					for (TActorIterator<ACastleGroundTileActor> It(Board->GetWorld()); It; ++It)
					{
						if (It->GetGroundCell().Coordinate == Cell.Coordinate)
						{
							GroundMesh = It->GetTileMeshComponent();
							break;
						}
					}
					if (!Test->TestNotNull(TEXT("正式放置地块拥有地面网格"), GroundMesh)) { return true; }

					TArray<UStaticMeshComponent*> Components;
					Building->GetComponents(Components);
					for (UStaticMeshComponent* Component : Components)
					{
						if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Component) && Component->GetStaticMesh())
						{
							Cube = Component;
						}
						else if (!UBuildingGameplayRootComponent::IsInGameplayHierarchy(Component)
							&& Component->GetStaticMesh() && VisualComponent == nullptr)
						{
							VisualComponent = Component;
						}
					}
					if (!Test->TestNotNull(TEXT("蓝图含 GameplayRoot 功能楼板"), Cube)) { return true; }
					if (!Test->TestNotNull(TEXT("蓝图含分支外美术组件"), VisualComponent)) { return true; }
					CubeTop = Cube->Bounds.Origin + FVector(0.0, 0.0, Cube->Bounds.BoxExtent.Z);
					Stage = 1;
					return false;
				}

				Test->AddError(TEXT("棋盘没有可用的非灰色测试地块。"));
				return true;
			}

			if (Stage == 1)
			{
				FNavLocation Projected;
				const bool bProjected = System->ProjectPointToNavigation(CubeTop, Projected, FVector(15.0, 15.0, 20.0));
				Test->TestTrue(TEXT("放置动画完成后 GameplayRoot 楼板顶面生成导航"), bProjected);
				Test->TestTrue(TEXT("GameplayRoot 楼板保持导航资格"), Building->IsComponentRelevantForNavigation(Cube));
				Test->TestTrue(TEXT("GameplayRoot 楼板允许影响导航"), Cube->CanEverAffectNavigation());
				Test->TestFalse(TEXT("分支外美术组件不参与导航"), VisualComponent->CanEverAffectNavigation());
				Test->TestFalse(TEXT("楼板覆盖期间停用数厘米下方的重复地面导航"), GroundMesh->CanEverAffectNavigation());
				Test->TestEqual(TEXT("放置动画完成只重建一次"), Navigation->RefreshCount, InitialRefreshCount + 1);
				Test->AddInfo(FString::Printf(TEXT("楼板顶面 %s 投影到 %s；导航刷新=%d。"), *CubeTop.ToString(), *Projected.Location.ToString(), Navigation->RefreshCount));

				FText Failure;
				Test->TestTrue(TEXT("正式棋盘移除建筑"), Board->RemoveBuilding(Building, Failure));
				if (!Failure.IsEmpty()) { Test->AddError(Failure.ToString()); }
				Stage = 2;
				return false;
			}

			if (Stage == 2)
			{
				FNavLocation GroundProjection;
				const bool bGroundProjected = System->ProjectPointToNavigation(
					FVector(CubeTop.X, CubeTop.Y, 20.0), GroundProjection, FVector(10.0, 10.0, 5.0));
				Test->TestTrue(TEXT("移除建筑后原地面导航恢复"), bGroundProjected);
				Test->TestTrue(TEXT("移除建筑后地面组件恢复导航资格"), GroundMesh->CanEverAffectNavigation());
				Test->TestEqual(TEXT("移除建筑只再重建一次"), Navigation->RefreshCount, InitialRefreshCount + 2);
				Test->AddInfo(FString::Printf(TEXT("建筑移除后地面投影=%s，导航刷新=%d。"), *GroundProjection.Location.ToString(), Navigation->RefreshCount));
				return true;
			}

			return true;
		}

	private:
		FAutomationTestBase* Test;
		ACastleBoard* Board = nullptr;
		ACastleBuildingBase* Building = nullptr;
		UStaticMeshComponent* Cube = nullptr;
		UStaticMeshComponent* VisualComponent = nullptr;
		UStaticMeshComponent* GroundMesh = nullptr;
		UCastleNavigationSubsystem* Navigation = nullptr;
		FVector CubeTop = FVector::ZeroVector;
		double Deadline;
		int32 InitialRefreshCount = 0;
		int32 Stage = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingBlueprintPlacementNavigationTest, "MovingCastle.NavigationGameplay.BlueprintPlacement",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBuildingBlueprintPlacementNavigationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(BuildingBlueprintNavigationTests::FPlacementCommand(this));
	return true;
}

#endif
