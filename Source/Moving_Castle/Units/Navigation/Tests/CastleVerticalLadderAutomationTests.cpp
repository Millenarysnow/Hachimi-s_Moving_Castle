#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleNavigationSubsystem.h"
#include "CastleLadderLinkComponent.h"
#include "CastleBuildings.h"
#include "CastleMassSpawner.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "BuildingNavigationActor.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Demo/CastleNavigationDemo.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Mass/CastleMassPatrolProcessor.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "MassMovementFragments.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavLinkCustomComponent.h"
#include "Units/BuildingInteraction/CastleBuildingTriggerSubsystem.h"
#include "Units/BuildingInteraction/Mass/CastleMassBuildingTriggerProcessor.h"

namespace CastleVerticalLadderTests
{
	/** 使用真实 Game 世界、Recast 和 Mass 处理器完成双层梯子动态生命周期验收。 */
	class FVerticalLadderCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FVerticalLadderCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(TEXT("双层梯子导航自动化超过 90 秒。"));
				return true;
			}
			if (!FindDemo()) { return false; }
			if (Demo->Phase < 0) { Test->AddError(TEXT("导航演示地图初始化失败，详见 NPC 导航 Error 日志。")); return true; }
			if (Stage == 0) { return BuildFixture(); }
			if (Stage == 1) { return CheckInitialRoute(); }
			if (Stage == 2) { return CheckRemovedLink(); }
			if (Stage == 3) { return CheckRestoredLink(); }
			if (Stage == 4) { return FinishCleanup(); }
			if (Stage == 5) { return CheckUnknownLink(); }
			if (Stage == 6) { return CheckLegRebinding(); }
			return true;
		}

	private:
		/** 只在正式演示的飞入和初始 Recast 构建完成后创建夹具。 */
		bool FindDemo()
		{
			if (Demo) { return true; }
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::Game) { continue; }
				for (TActorIterator<ACastleNavigationDemo> It(Context.World()); It; ++It) { Demo = *It; break; }
			}
			if (!Test->TestNotNull(TEXT("在 L_NPCNavigationDemo 的 Game 世界中运行"), Demo)) { return false; }
			World = Demo->GetWorld();
			Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
			Triggers = World->GetSubsystem<UCastleBuildingTriggerSubsystem>();
			Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
			Manager = &World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
			return true;
		}

		/** 在演示 Recast Bounds 内生成两层功能楼板、功能根和双向 Link。 */
		bool BuildFixture()
		{
			if (Demo->Phase != 1 || !Navigation->IsNavigationReady()) { return false; }
			InitialRefreshCount = Navigation->RefreshCount;
			const FTransform DemoTransform = Demo->GetActorTransform();
			FScopedCastleNavigationChange Change(World, Demo);
			Demo->bRunPathDemonstration = false;
			Navigation->ClearSharedRoute();
			for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It) { TestBounds = *It; break; }
			if (!Test->TestNotNull(TEXT("夹具需要地图提供真实 Brush 导航体积"), TestBounds)) { return true; }
			OriginalBoundsTransform = TestBounds->GetActorTransform();
			OriginalBoundsMobility = TestBounds->GetRootComponent()->Mobility;
			TestBounds->GetRootComponent()->SetMobility(EComponentMobility::Movable);
			const FBox OriginalBounds = TestBounds->GetComponentsBoundingBox(true);
			const FVector TargetExtent(3500.0, 1200.0, 1100.0);
			TestBounds->SetActorScale3D(TestBounds->GetActorScale3D() * TargetExtent / OriginalBounds.GetExtent());
			const FVector NewCenter = TestBounds->GetComponentsBoundingBox(true).GetCenter();
			TestBounds->SetActorLocation(TestBounds->GetActorLocation() + FVector(-300.0, 0.0, 900.0) - NewCenter);
			FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)->OnNavigationBoundsUpdated(TestBounds);
			Test->TestTrue(TEXT("程序化测试导航体积高 2200 cm 且顶面达到 2000 cm"),
				TestBounds->GetComponentsBoundingBox(true).GetSize().Z >= 2000.0
				&& TestBounds->GetComponentsBoundingBox(true).Max.Z >= 1500.0);
			Building = World->SpawnActor<ABuildingNavigationActor>();
			if (!Test->TestNotNull(TEXT("创建程序化双层导航建筑夹具"), Building)) { return true; }
			Root = NewObject<USceneComponent>(Building, TEXT("LadderFixtureRoot"));
			Building->SetRootComponent(Root);
			Building->AddInstanceComponent(Root);
			Root->RegisterComponent();
			Building->SetActorTransform(FTransform(FVector(-2450.0, 0.0, 0.0)) * DemoTransform);
			GameplayRoot = NewObject<UBuildingGameplayRootComponent>(Building, TEXT("GameplayRoot"));
			GameplayRoot->SetupAttachment(Root);
			Building->AddInstanceComponent(GameplayRoot);
			GameplayRoot->RegisterComponent();
			AddFloor(TEXT("LowerFunctionalFloor"), FVector(0.0, 0.0, -25.0));
			AddFloor(TEXT("UpperFunctionalFloor"), FVector(0.0, 0.0, 535.0));
			Ladder = NewObject<UCastleLadderLinkComponent>(Building, TEXT("LadderLink"));
			Ladder->SetupAttachment(GameplayRoot);
			Building->AddInstanceComponent(Ladder);
			Ladder->RegisterComponent();
			Test->TestTrue(TEXT("功能分支下的梯子组件符合建筑导航资格"), Building->IsComponentRelevantForNavigation(Ladder));
			Test->TestTrue(TEXT("双向连接在本世界获得有效注册 ID"), Ladder->GetId().IsValid());
			UCastleLadderLinkComponent* OutsideLink = NewObject<UCastleLadderLinkComponent>(Building, TEXT("NonGameplayLadder"));
			OutsideLink->bLadderLinkEnabled = false;
			OutsideLink->SetupAttachment(Root);
			Building->AddInstanceComponent(OutsideLink);
			OutsideLink->RegisterComponent();
			Test->TestFalse(TEXT("同类型梯子挂在非功能分支时被建筑导航过滤拒绝"), Building->IsComponentRelevantForNavigation(OutsideLink));
			const FTransform FixtureTransform = Building->GetActorTransform();
			LowerPoint = FixtureTransform.TransformPosition(FVector(-250.0, 0.0, 5.0));
			UpperPoint = FixtureTransform.TransformPosition(FVector(250.0, 0.0, 565.0));
			Stage = 1;
			return false;
		}

		/** 让可行走碰撞只存在于 GameplayRoot 下，尺寸为 800×800 cm。 */
		void AddFloor(const FName Name, const FVector& RelativeLocation)
		{
			UStaticMeshComponent* Floor = NewObject<UStaticMeshComponent>(Building, Name);
			Floor->SetupAttachment(GameplayRoot);
			Floor->SetMobility(EComponentMobility::Movable);
			Floor->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Floor->SetRelativeLocation(RelativeLocation);
			Floor->SetRelativeScale3D(FVector(8.0, 8.0, 0.5));
			Floor->SetCollisionProfileName(TEXT("BlockAll"));
			Floor->SetCanEverAffectNavigation(true);
			Floor->SetHiddenInGame(true);
			Building->AddInstanceComponent(Floor);
			Floor->RegisterComponent();
		}

		/** 等待 Recast 后验证双向路径、速度倍率、建筑进入和固定的查询次数。 */
		bool CheckInitialRoute()
		{
			if (!Navigation->IsNavigationReady()) { return false; }
			if (!bRouteRequested)
			{
				Test->TestEqual(TEXT("夹具楼板与梯子合并为一轮 NavMesh 刷新"), Navigation->RefreshCount, InitialRefreshCount + 1);
				FNavLocation LowerProjection;
				FNavLocation UpperProjection;
				UNavigationSystemV1* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				Test->TestTrue(TEXT("下层楼板在导航体积及 Recast 表面上可投影"),
					System->ProjectPointToNavigation(LowerPoint, LowerProjection, FVector(80.0, 80.0, 80.0)));
				Test->TestTrue(TEXT("上层楼板在 560 cm 高处可投影"),
					System->ProjectPointToNavigation(UpperPoint, UpperProjection, FVector(80.0, 80.0, 80.0)));
				FVector LinkLowerLocal;
				FVector LinkUpperLocal;
				ENavLinkDirection::Type Direction;
				Ladder->GetLinkData(LinkLowerLocal, LinkUpperLocal, Direction);
				Test->TestEqual(TEXT("Link 导出双向通行"), Direction, ENavLinkDirection::BothWays);
				const FTransform FixtureTransform = Building->GetActorTransform();
			const FVector LinkLowerWorld = FixtureTransform.TransformPosition(LinkLowerLocal);
				const FVector LinkUpperWorld = FixtureTransform.TransformPosition(LinkUpperLocal);
				FNavLocation LowerAnchorProjection;
				FNavLocation UpperAnchorProjection;
				Test->TestTrue(TEXT("下层 Link 接驳锚点精确落在 Recast 多边形"),
					System->ProjectPointToNavigation(LinkLowerWorld, LowerAnchorProjection, FVector(10.0, 10.0, 20.0))
					&& FVector::Dist2D(LinkLowerWorld, LowerAnchorProjection.Location) < 1.0);
				Test->TestTrue(TEXT("上层 Link 接驳锚点精确落在 Recast 多边形"),
					System->ProjectPointToNavigation(LinkUpperWorld, UpperAnchorProjection, FVector(10.0, 10.0, 20.0))
					&& FVector::Dist2D(LinkUpperWorld, UpperAnchorProjection.Location) < 1.0);
				Test->TestTrue(TEXT("下层接驳点距楼板边缘大于 AgentRadius 35 cm"),
					FMath::Min(400.0 - FMath::Abs(Ladder->BottomStandOffset.X), 400.0 - FMath::Abs(Ladder->BottomStandOffset.Y)) > 35.0);
				Test->TestTrue(TEXT("上层接驳点距楼板边缘大于 AgentRadius 35 cm"),
					FMath::Min(400.0 - FMath::Abs(Ladder->TopStandOffset.X), 400.0 - FMath::Abs(Ladder->TopStandOffset.Y)) > 35.0);
				FText Failure;
				InitialQueryCount = Navigation->PathQueryCount;
				if (!Test->TestTrue(TEXT("提交一楼到二楼双点闭环"), Navigation->RequestSharedPatrolRoute({LowerPoint, UpperPoint}, Failure)))
				{
					Test->AddError(Failure.ToString());
					return true;
				}
				bRouteRequested = true;
				return false;
			}
			Path = Navigation->AcquireSharedPath();
			if (!Path || !Path->bValid) { return false; }
			Test->TestTrue(TEXT("两点闭环恰好执行两次集中寻路"), Navigation->PathQueryCount == InitialQueryCount + 2);
			Test->TestEqual(TEXT("同一把梯子上行和下行各生成一个距离区间"), Path->LadderRanges.Num(), 2);
			Test->TestTrue(TEXT("双向折线采样穿过 560 cm 上层"), Path->PatrolPoints.ContainsByPredicate([](const FVector& Point) { return Point.Z > 550.0; }));
			double MaximumGap = 0.0;
			for (int32 Index = 1; Index < Path->PatrolPoints.Num(); ++Index)
			{
				MaximumGap = FMath::Max(MaximumGap, FVector::Distance(Path->PatrolPoints[Index - 1], Path->PatrolPoints[Index]));
			}
			Test->TestTrue(TEXT("共享三维采样没有悬空大跳变"), MaximumGap < 100.0);
			if (!RegisterTriggerBuildings()) { return true; }
			if (!TestSpeedMultipliers()) { return true; }
			Test->TestEqual(TEXT("Mass 多次跨层移动不增加集中寻路次数"), Navigation->PathQueryCount, InitialQueryCount + 2);
			// 去回程共用同一轴线；重建后必须只在原下行 Leg 内重绑定，不能跳到上行路段。
			const auto Entity = Spawners[0]->SpawnedUnits[0].Entity;
			auto& Patrol = Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Entity);
			RebindDistance = (Path->LadderRanges[1].StartDist + Path->LadderRanges[1].EndDist) * 0.5;
			Patrol.Distance = RebindDistance;
			Patrol.Leg = 1;
			FVector Position, Direction;
			Path->SamplePatrol(RebindDistance, Position, Direction);
			Manager->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Position);
			Navigation->BeginGeometryChange(Demo);
			Navigation->EndGeometryChange(Demo);
			Stage = 6;
			return false;
		}

		/** 原路线仅因 NavMesh 重建重绑时，使用完整三维距离并保留去回程路段身份。 */
		bool CheckLegRebinding()
		{
			if (!Navigation->IsNavigationReady()) { return false; }
			Path = Navigation->AcquireSharedPath();
			if (!Test->TestTrue(TEXT("无关几何刷新后双层路线恢复"), Path && Path->bValid)) { return true; }
			RunProcessor<UCastleMassPatrolProcessor>(0.0f);
			const auto& Patrol = Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Spawners[0]->SpawnedUnits[0].Entity);
			Test->TestEqual(TEXT("去回程重叠时仍保持原下行 Leg"), Patrol.Leg, 1);
			Test->TestTrue(TEXT("三维重绑定没有跳到上行进度"), FMath::IsNearlyEqual(Patrol.Distance, RebindDistance, 2.0));
			const FVector ExpectedFacing = (Ladder->LadderBottomOffset - Ladder->BottomStandOffset).GetSafeNormal2D();
			const FTransform& ReboundTransform = Manager->GetFragmentDataChecked<FTransformFragment>(Spawners[0]->SpawnedUnits[0].Entity).GetTransform();
			Test->TestTrue(TEXT("纯垂直攀爬朝向梯面而不是世界默认轴"), FMath::Abs(FMath::FindDeltaAngleDegrees(ReboundTransform.Rotator().Yaw, ExpectedFacing.Rotation().Yaw)) < 0.1);
			Test->TestEqual(TEXT("192个单位只增加两次集中重算"), Navigation->PathQueryCount, InitialQueryCount + 4);
			PathQueriesAfterRoute = Navigation->PathQueryCount;
			Test->AddExpectedMessagePlain(TEXT("NPC 导航：共享路线不可达"), ELogVerbosity::Warning);
			Ladder->UnregisterComponent();
			Stage = 2;
			return false;
		}

		/** 用两个真实建筑效果组件验证三维折线穿过上下层逻辑盒。 */
		bool RegisterTriggerBuildings()
		{
			const FTransform FixtureTransform = Building->GetActorTransform();
			const FVector LowerLocation = FixtureTransform.TransformPosition(FVector(-250.0, 0.0, 0.0));
			const FVector UpperLocation = FixtureTransform.TransformPosition(FVector(250.0, 0.0, 560.0));
			LowerBuilding = World->SpawnActor<ACastleNormalRoomBuilding>(LowerLocation, FRotator::ZeroRotator);
			UpperBuilding = World->SpawnActor<ACastleNormalRoomBuilding>(UpperLocation, FRotator::ZeroRotator);
			if (!Test->TestNotNull(TEXT("创建一楼测试建筑"), LowerBuilding)
				|| !Test->TestNotNull(TEXT("创建二楼测试建筑"), UpperBuilding)) { return false; }
			if (!Test->TestNotNull(TEXT("一楼建筑具有原生效果组件"), LowerBuilding->BuildingEffect.Get())
				|| !Test->TestNotNull(TEXT("二楼建筑具有原生效果组件"), UpperBuilding->BuildingEffect.Get())) { return false; }
			FText Failure;
			TArray<FBox> LowerBoxes = {FBox(FVector(-40.0, -40.0, -20.0), FVector(40.0, 40.0, 100.0))};
			TArray<FBox> UpperBoxes = LowerBoxes;
			if (!Test->TestTrue(TEXT("登记一楼进入区域"), Triggers->RegisterBuilding(LowerBuilding, LowerBuilding->GetActorTransform(), MoveTemp(LowerBoxes), Failure)))
			{
				Test->AddError(Failure.ToString());
				return false;
			}
			if (!Test->TestTrue(TEXT("登记二楼进入区域"), Triggers->RegisterBuilding(UpperBuilding, UpperBuilding->GetActorTransform(), MoveTemp(UpperBoxes), Failure)))
			{
				Test->AddError(Failure.ToString());
				return false;
			}
			return true;
		}

		/** 三种倍率各用单独 Mass 共享配置，直接从同一攀爬区间起步测量位移。 */
		bool TestSpeedMultipliers()
		{
			const double TestDistance = Path->LadderRanges[0].StartDist + 20.0;
			const float Multipliers[] = {0.5f, 1.0f, 2.0f};
			const double ExpectedTravel[] = {5.0, 10.0, 20.0};
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Multipliers); ++Index)
			{
				UMassEntityConfigAsset* Config = NewObject<UMassEntityConfigAsset>(World);
				UCastleMassNPCTrait* Trait = NewObject<UCastleMassNPCTrait>(Config);
				Trait->Movement.Acceleration = 0.0f;
				Trait->Movement.MaxSpeed = 100.0f;
				Trait->Movement.LadderSpeedMultiplier = Multipliers[Index];
				Config->GetMutableConfig().AddTrait(*Trait);
				ACastleMassSpawner* Spawner = World->SpawnActorDeferred<ACastleMassSpawner>(ACastleMassSpawner::StaticClass(), FTransform::Identity);
				Spawner->Configuration = Config;
				Spawner->Count = 64;
				Spawner->Columns = 1;
				Spawner->bFollowSharedPatrolRoute = true;
				Spawner->FinishSpawning(FTransform::Identity);
				Spawners.Add(Spawner);
				if (!Test->TestEqual(TEXT("每个倍率配置生成64个共享巡逻单位"), Spawner->SpawnedUnits.Num(), 64)) { return false; }
				Manager->FlushCommands();
				FCastleMassPatrolFragment& Patrol = Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Spawner->SpawnedUnits[0].Entity);
				Patrol.Distance = TestDistance;
				Patrol.Speed = 0.0;
				Patrol.PathRevision = Path->PathRevision;
				Patrol.RouteRevision = Path->RouteRevision;
				Patrol.bBlocked = false;
				FVector Position;
				FVector Direction;
				Path->SamplePatrol(TestDistance, Position, Direction);
				FTransform& Transform = Manager->GetFragmentDataChecked<FTransformFragment>(Spawner->SpawnedUnits[0].Entity).GetMutableTransform();
				Transform.SetLocation(Position);
				Transform.SetRotation(FRotator(35.0, 180.0, 20.0).Quaternion());
			}
			RunProcessor<UCastleMassPatrolProcessor>(0.1f);
			for (int32 Index = 0; Index < Spawners.Num(); ++Index)
			{
				const auto Entity = Spawners[Index]->SpawnedUnits[0].Entity;
				const FCastleMassPatrolFragment& Patrol = Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Entity);
				const FTransform& Transform = Manager->GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform();
				Test->TestTrue(FString::Printf(TEXT("倍率 %.1f 的单步距离精确缩放"), Multipliers[Index]),
					FMath::IsNearlyEqual(Patrol.Distance - TestDistance, ExpectedTravel[Index], 0.01));
				Test->TestTrue(TEXT("梯子爬升期间单位保持直立"), FMath::IsNearlyZero(Transform.Rotator().Pitch, 0.01f)
					&& FMath::IsNearlyZero(Transform.Rotator().Roll, 0.01f));
			}
			UMassEntityConfigAsset* InvalidConfig = NewObject<UMassEntityConfigAsset>(World);
			UCastleMassNPCTrait* InvalidTrait = NewObject<UCastleMassNPCTrait>(InvalidConfig);
			InvalidTrait->Movement.Acceleration = 0.0f;
			InvalidTrait->Movement.MaxSpeed = 100.0f;
			InvalidTrait->Movement.LadderSpeedMultiplier = 0.0f;
			InvalidConfig->GetMutableConfig().AddTrait(*InvalidTrait);
			InvalidSpawner = World->SpawnActorDeferred<ACastleMassSpawner>(ACastleMassSpawner::StaticClass(), FTransform::Identity);
			InvalidSpawner->Configuration = InvalidConfig;
			InvalidSpawner->Count = 1;
			InvalidSpawner->Columns = 1;
			InvalidSpawner->bFollowSharedPatrolRoute = true;
			InvalidSpawner->FinishSpawning(FTransform::Identity);
			Manager->FlushCommands();
			FCastleMassPatrolFragment& InvalidPatrol = Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(InvalidSpawner->SpawnedUnits[0].Entity);
			InvalidPatrol.Distance = TestDistance;
			InvalidPatrol.PathRevision = Path->PathRevision;
			InvalidPatrol.RouteRevision = Path->RouteRevision;
			InvalidPatrol.bBlocked = false;
			Test->AddExpectedMessagePlain(TEXT("NPC 巡逻：1 个单位遇到非法梯子速度倍率"), ELogVerbosity::Error);
			RunProcessor<UCastleMassPatrolProcessor>(0.1f);
			Test->TestEqual(TEXT("非法倍率在梯子区间停止单位"), InvalidPatrol.Speed, 0.0);
			Units->StopUnits(InvalidSpawner->SpawnedUnits);
			return true;
		}

		/** Link 注销后整条闭环失效；禁止路线沿垂直端点插值穿楼板。 */
		bool CheckRemovedLink()
		{
			if (!Navigation->IsNavigationReady()) { return false; }
			const auto Current = Navigation->AcquireSharedPath();
			if (Current && Current->bValid) { return false; }
			Test->TestTrue(TEXT("移除梯子后共享闭环明确失效"), Current && !Current->bValid);
			Test->TestFalse(TEXT("失败原因指出路径不可达或 Link 无效"), Navigation->LastFailure.IsEmpty());
			Test->TestEqual(TEXT("动态移除只通过集中路径请求重算"), Navigation->PathQueryCount, PathQueriesAfterRoute + 1);
			RunProcessor<UCastleMassPatrolProcessor>(1.0f);
			const auto Entity = Spawners[0]->SpawnedUnits[0].Entity;
			Test->TestEqual(TEXT("失效路线使爬梯速度归零"), Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Entity).Speed, 0.0);
			Test->TestTrue(TEXT("失效路线使Velocity归零"), Manager->GetFragmentDataChecked<FMassVelocityFragment>(Entity).Value.IsZero());
			Ladder->RegisterComponent();
			Stage = 3;
			return false;
		}

		/** Link 重新注册后闭环恢复，再由正式 Mass 处理器触发两层建筑效果。 */
		bool CheckRestoredLink()
		{
			if (!Navigation->IsNavigationReady()) { return false; }
			Path = Navigation->AcquireSharedPath();
			if (!Path || !Path->bValid) { return false; }
			Test->TestEqual(TEXT("恢复同一 Link 后两点闭环重新发布"), Path->LadderRanges.Num(), 2);
			// 计数验收只启用一个单位，其余191个单位显式停用，避免将批量进入误当成单圈次数。
			for (ACastleMassSpawner* Spawner : Spawners) { Units->StopUnits(Spawner->SpawnedUnits); }
			Manager->FlushCommands();
			const int64 LowerBefore = LowerBuilding->BuildingEffect->TriggerCount;
			const int64 UpperBefore = UpperBuilding->BuildingEffect->TriggerCount;
			for (int32 Index = 0; Index < Spawners.Num() - 1; ++Index) { Units->StopUnits(Spawners[Index]->SpawnedUnits); }
			Manager->FlushCommands();
			const auto Entity = Spawners.Last()->SpawnedUnits[0].Entity;
			FCastleMassPatrolFragment& Patrol = Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Entity);
			Patrol.bEnabled = true;
			Patrol.Distance = 0.0;
			Patrol.Speed = 0.0;
			Patrol.CompletedLaps = 0;
			Patrol.PathRevision = Path->PathRevision;
			Patrol.RouteRevision = Path->RouteRevision;
			Patrol.bBlocked = false;
			const int32 QueriesBeforeMovement = Navigation->PathQueryCount;
			for (int32 Step = 0; Step < 120 && Patrol.CompletedLaps < 1; ++Step)
			{
				RunProcessor<UCastleMassPatrolProcessor>(0.25f);
				RunProcessor<UCastleMassBuildingTriggerProcessor>(0.0f);
			}
			Test->TestTrue(TEXT("共享 Mass 巡逻器完成一圈上下楼梯"), Patrol.CompletedLaps >= 1);
			Test->TestEqual(TEXT("一圈跨层移动触发一楼建筑一次"), LowerBuilding->BuildingEffect->TriggerCount, LowerBefore + 1);
			Test->TestEqual(TEXT("一圈跨层移动触发二楼建筑一次"), UpperBuilding->BuildingEffect->TriggerCount, UpperBefore + 1);
			Test->TestEqual(TEXT("Mass 单位跨层移动不增加集中路径查询"), Navigation->PathQueryCount, QueriesBeforeMovement);
			Test->AddInfo(FString::Printf(TEXT("跨层实测：PathQueryCount=%d，PatrolPoints=%d，TriggerCount下层=%lld、上层=%lld。"),
				QueriesBeforeMovement, Path->PatrolPoints.Num(), LowerBuilding->BuildingEffect->TriggerCount, UpperBuilding->BuildingEffect->TriggerCount));
			// 在同一两层几何上换成引擎自带的其他 CustomLink，确认没有放宽成任意 NavLink。
			FScopedCastleNavigationChange Change(World, Demo);
			Ladder->UnregisterComponent();
			UnknownLinkOwner = World->SpawnActor<AActor>();
			USceneComponent* UnknownRoot = NewObject<USceneComponent>(UnknownLinkOwner);
			UnknownLinkOwner->SetRootComponent(UnknownRoot);
			UnknownLinkOwner->AddInstanceComponent(UnknownRoot);
			UnknownRoot->RegisterComponent();
			UnknownLinkOwner->SetActorTransform(Building->GetActorTransform());
			UNavLinkCustomComponent* UnknownLink = NewObject<UNavLinkCustomComponent>(UnknownLinkOwner);
			UnknownLink->SetLinkData(Ladder->BottomStandOffset, Ladder->TopStandOffset, ENavLinkDirection::BothWays);
			UnknownLinkOwner->AddInstanceComponent(UnknownLink);
			UnknownLink->RegisterComponent();
			Test->AddExpectedMessagePlain(TEXT("NPC 导航：共享巡逻遇到未登记、已禁用或非梯子类型的 NavLink"), ELogVerbosity::Warning);
			Stage = 5;
			return false;
		}

		/** 真实 Recast 路径经过非梯子 CustomLink 后，集中求解必须废弃整条闭环。 */
		bool CheckUnknownLink()
		{
			if (!Navigation->IsNavigationReady()) { return false; }
			Test->TestFalse(TEXT("未知CustomLink不能冒充梯子通行"), Navigation->GetSharedPath().bValid);
			Test->TestTrue(TEXT("未知连接失败原因明确指明非梯子类型"), Navigation->LastFailure.ToString().Contains(TEXT("非梯子类型")));
			Navigation->ClearSharedRoute();
			for (ACastleMassSpawner* Spawner : Spawners) { Spawner->Destroy(); }
			Spawners.Reset();
			if (InvalidSpawner) { InvalidSpawner->Destroy(); InvalidSpawner = nullptr; }
			Triggers->UnregisterBuilding(LowerBuilding);
			Triggers->UnregisterBuilding(UpperBuilding);
			LowerBuilding->Destroy();
			UpperBuilding->Destroy();
			Building->Destroy();
			UnknownLinkOwner->Destroy();
			TestBounds->SetActorTransform(OriginalBoundsTransform);
			TestBounds->GetRootComponent()->SetMobility(OriginalBoundsMobility);
			FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)->OnNavigationBoundsUpdated(TestBounds);
			Stage = 4;
			return false;
		}

		/** 销毁自动化几何并等待最后一轮 Recast 完成，避免污染后续测试。 */
		bool FinishCleanup()
		{
			if (!Navigation->IsNavigationReady()) { return false; }
			return true;
		}

		template<typename TProcessor>
		void RunProcessor(const float DeltaTime)
		{
			TProcessor* Processor = NewObject<TProcessor>(World);
			Processor->CallInitialize(World, Manager->AsShared());
			UE::Mass::FProcessingContext Context(*Manager, DeltaTime);
			UE::Mass::Executor::Run(*Processor, Context);
		}

		FAutomationTestBase* Test = nullptr;
		double Deadline = FPlatformTime::Seconds() + 90.0;
		int32 Stage = 0;
		int32 InitialQueryCount = 0;
		int32 InitialRefreshCount = 0;
		int32 PathQueriesAfterRoute = 0;
		double RebindDistance = 0.0;
		bool bRouteRequested = false;
		UWorld* World = nullptr;
		ACastleNavigationDemo* Demo = nullptr;
		ABuildingNavigationActor* Building = nullptr;
		AActor* UnknownLinkOwner = nullptr;
		USceneComponent* Root = nullptr;
		UBuildingGameplayRootComponent* GameplayRoot = nullptr;
		UCastleLadderLinkComponent* Ladder = nullptr;
		ACastleNormalRoomBuilding* LowerBuilding = nullptr;
		ACastleNormalRoomBuilding* UpperBuilding = nullptr;
		ANavMeshBoundsVolume* TestBounds = nullptr;
		FTransform OriginalBoundsTransform;
		EComponentMobility::Type OriginalBoundsMobility = EComponentMobility::Static;
		TArray<ACastleMassSpawner*> Spawners;
		ACastleMassSpawner* InvalidSpawner = nullptr;
		FVector LowerPoint = FVector::ZeroVector;
		FVector UpperPoint = FVector::ZeroVector;
		UCastleNavigationSubsystem* Navigation = nullptr;
		UCastleBuildingTriggerSubsystem* Triggers = nullptr;
		UCastleMassUnitSubsystem* Units = nullptr;
		FMassEntityManager* Manager = nullptr;
		TSharedPtr<const FCastleSharedNavigationPath, ESPMode::ThreadSafe> Path;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleVerticalLadderAutomationTest, "MovingCastle.Navigation.VerticalLadder",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleVerticalLadderAutomationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleVerticalLadderTests::FVerticalLadderCommand(this));
	return true;
}

#endif
