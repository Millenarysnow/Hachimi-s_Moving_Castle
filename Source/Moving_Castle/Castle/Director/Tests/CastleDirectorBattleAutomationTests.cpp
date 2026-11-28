// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Audio/CastleAudioSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Castle/Automation/CastlePIEAutomation.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Combat/CastleDifficultyCurveDataAsset.h"
#include "Castle/Actors/CastleBoard.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"
#include "Castle/SkyIslands/CastleSkyIslandBase.h"
#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Components/CastleShopComponent.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Castle/Components/CastleEconomyComponent.h"
#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Castle/Automation/CastleTowerOverviewTestUtilities.h"
#include "Units/Navigation/Interaction/CastlePatrolRoute.h"
#include "Units/Navigation/Interaction/CastlePatrolWaypointMarker.h"
#include "Units/Navigation/CastleNavigationSubsystem.h"
#include "Units/Mass/CastleMassSpawner.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PawnMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Castle/UI/BuildBar/CastleBuildBarWidget.h"
#include "Castle/UI/Common/CastleHUDControls.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetSwitcher.h"
#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MassEntitySubsystem.h"
#include "CastleTowerDispatchSubsystem.h"




/** 在正式 MainMap 的 PIE 中操作生产入口；不构造棋盘、路线快照或替代战斗夹具。 */
class FDirectorBattleScenario final : public IAutomationLatentCommand
{
public:
	explicit FDirectorBattleScenario(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 100.0) { Test->AddError(TEXT("导演完整战斗场景超时。")); return true; }
		UWorld* World = nullptr;
		for (const auto& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE) { World = Context.World(); break; }
		}
		if (!World || !World->HasBegunPlay()) { return false; }
		auto* Director = World->GetSubsystem<UCastleDirectorSubsystem>();
		if (!Test->TestTrue(TEXT("正式地图导演已运行"), Director && Director->IsRunning())) { return true; }
		const auto* Audio = UCastleAudioSubsystem::GetForWorld(World);
		if (!Test->TestNotNull(TEXT("正式局音频实例"), Audio)) { return true; }
		// 战斗流程测试通过原本的节点事务驱动每一阶段，音频须在同一帧与阶段一致。
		switch (Director->GetPhase())
		{
		case ECastleDirectorPhase::Editing:
		case ECastleDirectorPhase::Reward: Test->TestEqual(TEXT("编辑及奖励阶段使用建造 BGM"), Audio->GetCurrentBGMTrack(), ECastleBGMTrack::Build); break;
		case ECastleDirectorPhase::Battle: Test->TestEqual(TEXT("正式战斗使用战斗 BGM"), Audio->GetCurrentBGMTrack(), ECastleBGMTrack::Battle); break;
		case ECastleDirectorPhase::SkyIsland: Test->TestEqual(TEXT("正式空岛使用空岛 BGM"), Audio->GetCurrentBGMTrack(), ECastleBGMTrack::SkyIsland); break;
		case ECastleDirectorPhase::GameOver: Test->TestEqual(TEXT("结束阶段停止音乐"), Audio->GetCurrentBGMTrack(), ECastleBGMTrack::None); break;
		case ECastleDirectorPhase::Inactive: break;
		}
		auto* Board = Director->Board.Get();
		auto* Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
		auto* Dispatch = World->GetSubsystem<UCastleTowerDispatchSubsystem>();
		FText Reason;
		if (Stage == 0)
		{
			// 所有故障注入只改临时副本，不改磁盘上的 DA 或编辑器原始地图。
			Director->Config = DuplicateObject<UCastleDirectorConfigDataAsset>(Director->Config, World);
			Director->Config->CombatConfig = DuplicateObject<UCastleCombatConfigDataAsset>(Director->Config->CombatConfig, Director->Config);
			auto* CombatConfig = Director->Config->CombatConfig.Get();
			auto* Curve = DuplicateObject(CombatConfig->DifficultyCurve.Get(), CombatConfig);
			CombatConfig->DifficultyCurve = Curve;
			if (!Test->TestTrue(TEXT("正式 DA 的关卡间隔和普通表可用于空岛回归"),
				Director->Config->EliteEveryNSteps == 5 && Curve->NormalBattles.Num() >= 5)) { return true; }
			// 区分普通表第 4、5 项，确保第 6 关不能沿用按实际战斗次数选出的第 4 项。
			FCastleCombatWaveDefinition RaidWave;
			RaidWave.BugCurve.bEnabled = false;
			auto& Raid = RaidWave.RaidEntries.AddDefaulted_GetRef();
			Raid.EnemyId = TEXT("MiasmaWelp");
			Raid.Count = 4;
			Raid.SpawnInterval = 0.0f;
			Curve->NormalBattles[3].Waves = {RaidWave};
			Curve->NormalBattles[4].Waves = {RaidWave, RaidWave};
			if (!Test->TestTrue(TEXT("临时战斗配置初始化"), Director->Combat->InitializeCombat(Director->Config->CombatConfig, Reason))) { return true; }
			Navigation->ClearSharedRoute();
			ACastleDirectorNodeActor* First = Director->Nodes[0];
			Director->HandleNodeTouched(First);
			Test->TestTrue(TEXT("重叠回调只记录，不立即派兵"), Director->PendingNode.Get() == First && !Dispatch->IsSessionActive());
			Director->HandleNodeTouched(Director->Nodes.Last());
			Test->TestTrue(TEXT("同帧多个节点保留首次选择"), Director->PendingNode.Get() == First);
			Director->Tick(0);
			Test->TestEqual(TEXT("无路线拒绝后仍编辑"), Director->GetPhase(), ECastleDirectorPhase::Editing);
			Test->TestTrue(TEXT("无路线提示可见且节点保留"), Director->GetStatusMessage().ToString().Contains(TEXT("无法开战")) && IsValid(First));
			// 三个独立塔面提供真实碰撞与匹配来源；根据本局格子的属性制测试牌，不改格子数据。
			const FIntVector Base = Board->GetBoardCenterCoordinate();
			const FVector Center = Board->GetGridWorldLocation(Base);
			const FIntVector Directions[] = { FIntVector(0, -1, 0), FIntVector(-1, 0, 0), FIntVector(1, 0, 0) };
			const ECastleBuildingType Types[] = { ECastleBuildingType::CannonTower, ECastleBuildingType::ArcherTower, ECastleBuildingType::Farm };
			FCastleBuildingDescriptor BridgeCard;
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Directions); ++Index)
			{
				FCastleGridCursorTarget Target;
				const FVector RayStart = Center + FVector(Directions[Index]) * Board->CellSize * 2.0;
				if (!Test->TestTrue(TEXT("解析正式主塔侧格"), Board->TryResolveGridCursorTarget(RayStart, Center, Target)
					&& Target.Surface == ECastleGridCursorSurface::MainTowerSide && Target.AdjacentCell == Base + Directions[Index])) { return true; }
				const FCastleTowerSideCell* Cell = Board->FindTowerSideCell(Target.Cell, Target.Face);
				if (!Test->TestNotNull(TEXT("查询本局塔面属性"), Cell)) { return true; }
				FCastleBuildingDescriptor Card;
				Card.Definition = Board->FindBuildingDefinition(Types[Index]);
				Card.Level = Index == 2 ? 4 : 1;
				Card.Color = Cell->Color;
				Card.Number = Cell->Number;
				ACastleBuildingBase* Placed = Board->PlaceBuildingAtCursor(Target, Card, false, Reason);
				if (!Test->TestNotNull(TEXT("在塔侧放置正式导航建筑"), Placed)) { Test->AddError(Reason.ToString()); return true; }
				if (Index == 0)
				{
					Building = Placed;
					BridgeCard = Card;
				}
				CastleBuildingTests::CompletePlacement(Placed);
			}
			// 双门在相反两侧。拐角使用炮塔的颜色并匹配门侧房间，保证两道接缝都可通行。
			for (const int32 Side : {-1, 1})
			{
				const ACastleBuildingBase* DoorRoom = Board->GetBuildingAt(Base + FIntVector(Side, 0, 0));
				if (!Test->TestNotNull(TEXT("双门外各有站立房间"), DoorRoom)) { return true; }
				// 西南角是第二个可点击的弓箭塔，与门外箭塔分格且不同卡，避免合并或与隐式门点重合。
				BridgeCard.Definition = Board->FindBuildingDefinition(Side == -1 ? ECastleBuildingType::ArcherTower : ECastleBuildingType::NormalRoom);
				BridgeCard.Number = DoorRoom->GetBuildingNumber();
				if (Side == -1 && BridgeCard.Color == DoorRoom->GetBuildingColor()) { BridgeCard.Number = BridgeCard.Number % 9 + 1; }
				FCastleGridCursorTarget Corner;
				Corner.bValid = true;
				Corner.Surface = ECastleGridCursorSurface::Building;
				Corner.Cell = Base + FIntVector(0, -1, 0);
				Corner.AdjacentCell = Corner.Cell + FIntVector(Side, 0, 0);
				Corner.Face = Side == 1 ? ECastleGridFace::PositiveX : ECastleGridFace::NegativeX;
				ACastleBuildingBase* Room = Board->PlaceBuildingAtCursor(Corner, BridgeCard, false, Reason);
				if (!Test->TestNotNull(TEXT("双门之间放置绕塔连接房"), Room)) { Test->AddError(Reason.ToString()); return true; }
				CastleBuildingTests::CompletePlacement(Room);
			}
			Stage = 1;
			return false;
		}
		if (Stage == 1)
		{
			if (!Navigation->IsNavigationReady()) { return false; }
			// 固定选择炮塔、箭塔与农田第二层的三个占格；V4 站立面高于楼层底面锚点。
			const FIntVector Base = Board->GetBoardCenterCoordinate();
			TArray<FVector> ClickedPoints;
			const FIntVector CannonCell = Base + FIntVector(0, -1, 0);
			const FIntVector ArcherCell = Base + FIntVector(-1, -1, 0);
			for (const FIntVector Cell : {CannonCell, ArcherCell, Base + FIntVector(1, 0, 1)})
			{
				const FVector GridCenter = Board->GetGridWorldLocation(Cell);
				FVector FloorPoint;
				if (!Test->TestTrue(TEXT("炮塔/其他建筑/上层楼顶能解析"), ACastlePatrolRoute::TryResolveWaypointFromRay(
					World, GridCenter + FVector(0, 0, 2000), GridCenter - FVector(0, 0, 1000), FloorPoint, Reason))) { Test->AddError(Reason.ToString()); return true; }
				const ACastleBuildingBase* FloorOwner = Board->GetBuildingAt(Cell);
				if (!Test->TestNotNull(TEXT("预期格存在实际建筑"), FloorOwner)
					|| !Test->TestNotNull(TEXT("该三类建筑必须持有正式 V4 标高"), FloorOwner->HachimisLayout.Get())) { return true; }
				// 由 fixture 指定的格与资产标高独立计算期望，不从被测射线或 Recast 投影反推。
				const FVector Expected = GridCenter + Board->GetActorTransform().TransformVector(
					FVector(0, 0, FloorOwner->HachimisLayout->StandingHeight - Board->LayerHeight * 0.5));
				Test->TestTrue(TEXT("楼顶点精确吸附到指定格的作者站立面"), FloorPoint.Equals(Expected, 0.1));
				if (Cell.Z == Base.Z)
				{
					// 两次瞄准都从未与其他格相接的立面外侧开始，直接对准同一栋楼内的楼板。
					const FIntVector Outward = Cell == CannonCell ? FIntVector(0, -1, 0) : FIntVector(-1, 0, 0);
					if (!VerifyObliqueFloor(*World, *Board, *FloorOwner, Outward, Expected, ClickedPoints.AddDefaulted_GetRef())) { return true; }
				}
				if (!Test->TestTrue(TEXT("对应楼内地板可投影为有效路线"), Navigation->RequestSharedPatrolRoute({FloorPoint}, Reason))) { Test->AddError(Reason.ToString()); return true; }
				const auto FloorPath = Navigation->AcquireSharedPath();
				if (!Test->TestTrue(TEXT("本层NavMesh投影通过"), FloorPath && FloorPath->bValid && FloorPath->bClosedLoop)) { Test->AddError(Navigation->LastFailure.ToString()); return true; }
				Test->TestTrue(TEXT("投影没有落到错误楼层"), FMath::Abs(FloorPath->Waypoints[0].Z - FloorPoint.Z) <= Navigation->ProjectionExtent.Z);
			}
			// 验证玩家直接将路径点 1、2 放在门 A 与门 B 外侧紧邻房间时，不会与门内隐式点重合，且路线绕塔外通行而不穿塔。
			TArray<FVector> DoorNeighborPoints;
			for (const FIntVector DoorCell : {Base + FIntVector(1, 0, 0), Base + FIntVector(-1, 0, 0)})
			{
				const ACastleBuildingBase* DoorOwner = Board->GetBuildingAt(DoorCell);
				if (!Test->TestNotNull(TEXT("双门外邻格存在实际建筑"), DoorOwner)
					|| !Test->TestNotNull(TEXT("双门外建筑具有 V4 站立面"), DoorOwner->HachimisLayout.Get())) { return true; }
				DoorNeighborPoints.Add(Board->GetGridWorldLocation(DoorCell) + Board->GetActorTransform().TransformVector(
					FVector(0, 0, DoorOwner->HachimisLayout->StandingHeight - Board->LayerHeight * 0.5)));
			}
			if (!Test->TestTrue(TEXT("门 A 与门 B 外邻格直接作为路径点 1、2 提交"), Director->PatrolRoute->SetWorldWaypoints(DoorNeighborPoints, Reason)))
			{ Test->AddError(Reason.ToString()); return true; }
			const auto DoorNeighborPath = Navigation->AcquireSharedPath();
			if (!Test->TestTrue(TEXT("门外邻格路径点与门内隐式点不重合且绕塔外生成完整开环"),
				DoorNeighborPath && DoorNeighborPath->bValid && DoorNeighborPath->bTowerDispatch && DoorNeighborPath->Length > Board->CellSize * 3.0))
			{ Test->AddError(Navigation->LastFailure.ToString()); return true; }
			// 用两次斜瞄得到的落点提交正式路线，第二点来自此前被墙盒遮挡的弓箭塔。
			if (!Test->TestTrue(TEXT("由玩家路径入口提交两个点击点"), Director->PatrolRoute->SetWorldWaypoints(ClickedPoints, Reason)))
			{ Test->AddError(Reason.ToString()); return true; }
			Test->TestTrue(TEXT("原样保存两次点击的位置和顺序"), Navigation->GetRequestedWaypoints() == ClickedPoints);
			const auto Path = Navigation->AcquireSharedPath();
			if (!Test->TestTrue(TEXT("生成前实际开环已就绪"), Path && Path->bValid && !Path->bClosedLoop && Path->bTowerDispatch)) { Test->AddError(Navigation->LastFailure.ToString()); return true; }
			// NPC 已生成后令战斗配置非法，证明最后一步失败仍能收回整批 NPC 并恢复塔侧格。
			const float Radius = Director->Config->CombatConfig->SpawnInnerRadius;
			Director->Config->CombatConfig->SpawnInnerRadius = -1;
			Test->AddExpectedError(TEXT("StartBattle："), EAutomationExpectedErrorFlags::Contains, 1);
			Test->AddExpectedError(TEXT("导演开战失败"), EAutomationExpectedErrorFlags::Contains, 1);
			Test->TestFalse(TEXT("战斗系统拒绝导致整次开战失败"), Director->TryStartSelectedBattle(Director->Nodes[0], Reason));
			Test->TestTrue(TEXT("失败不销毁节点"), Director->Nodes.Num() >= 2 && IsValid(Director->Nodes[0]));
			Test->TestFalse(TEXT("失败恢复塔侧格"), Board->IsPlacementSurfacePresentationHidden());
			Test->TestFalse(TEXT("失败关闭出兵会话"), Dispatch->IsSessionActive());
			Test->TestEqual(TEXT("失败仍待进入第 1 关"), Director->GetNextStep(), 1);
			Test->TestEqual(TEXT("失败保持编辑阶段"), Director->GetPhase(), ECastleDirectorPhase::Editing);
			Director->Config->CombatConfig->SpawnInnerRadius = Radius;
			Stage = 2;
			return false;
		}
		if (Stage == 2)
		{
			// 巡逻入口在正式地图生成两个编号，并把每个枢轴留在对应点击的楼板格心。
			const auto& RequestedPoints = Navigation->GetRequestedWaypoints();
			TSet<int32> Numbers;
			for (TActorIterator<ACastlePatrolWaypointMarker> Marker(World); Marker; ++Marker)
			{
				const int32 Index = Marker->GetDisplayedNumber() - 1;
				if (Test->TestTrue(TEXT("正式巡逻标记编号有效且唯一"), Marker->GetOwner() == Director->PatrolRoute.Get()
					&& RequestedPoints.IsValidIndex(Index) && !Numbers.Contains(Index)))
				{
					Numbers.Add(Index);
					Test->TestTrue(TEXT("标记枢轴位于对应点击的楼内格心"), Marker->GetActorLocation().Equals(RequestedPoints[Index]));
				}
			}
			Test->TestEqual(TEXT("MainMap 同时显示 1、2 两个编号"), Numbers.Num(), 2);
			// 使用飞行移动组件的实际 sweep 穿过第二层塔身，然后移动进入正式节点触发球。
			APawn* Pawn = Director->Controller->GetPawn();
			UPawnMovementComponent* Movement = Pawn ? Pawn->GetMovementComponent() : nullptr;
			if (!Test->TestNotNull(TEXT("玩家飞行移动组件"), Movement)) { return true; }
			const FVector Forward = Board->GetActorForwardVector();
			const FVector TowerMiddle = Board->GetGridWorldLocation(Board->GetBoardCenterCoordinate()) + FVector::UpVector * Board->LayerHeight;
			const FVector SweepStart = TowerMiddle - Forward * Board->CellSize * 2.0;
			const FVector SweepEnd = TowerMiddle + Forward * Board->CellSize * 2.0;
			Pawn->SetActorLocation(SweepStart);
			FHitResult Hit;
			Movement->SafeMoveUpdatedComponent(SweepEnd - SweepStart, Pawn->GetActorQuat(), true, Hit);
			Test->TestTrue(TEXT("飞行胶囊扫过实体塔身无阻挡"), !Hit.bBlockingHit && Pawn->GetActorLocation().Equals(SweepEnd, 1.0));

			const ACastleDirectorNodeActor* BattleNode = Director->Nodes[0];
			const FVector NodeCenter = BattleNode->GetActorLocation();
			const FVector Approach = Forward * 800.0;
			Pawn->SetActorLocation(NodeCenter - Approach);
			Movement->SafeMoveUpdatedComponent(Approach, Pawn->GetActorQuat(), true, Hit);
			if (!Test->TestTrue(TEXT("真实触发球重叠排队开战"), Director->PendingNode.Get() == BattleNode && !Hit.bBlockingHit)) { return true; }
			Movement->Velocity = Forward * 425.0;
			Director->Tick(0);
			if (!Test->TestEqual(TEXT("真实触碰后提交开战"), Director->GetPhase(), ECastleDirectorPhase::Battle)) { return true; }
			CastleTowerOverviewTests::Check(*Test, *Board, *Director->Controller);
			CastleTowerOverviewTests::Capture(TEXT("03-battle-tower-overview.png"));
			int32 Reserve = 0, Active = 0;
			Dispatch->GetDispatchCounts(Reserve, Active);
			Test->TestEqual(TEXT("开战登记 8 名待命 NPC"), Reserve, 8);
			Test->TestEqual(TEXT("手动模式开战没有自动派兵"), Active, 0);
			Test->TestTrue(TEXT("战斗隐藏塔侧格"), Board->IsPlacementSurfacePresentationHidden());
			Test->TestEqual(TEXT("成功才清除节点"), Director->Nodes.Num(), 0);
			Test->TestEqual(TEXT("开战后仍处于第 1 关"), Director->GetNextStep(), 1);
			Test->TestTrue(TEXT("HUD显示真实剩余敌人"), Director->GetHUDSummary().Contains(TEXT("剩余敌人")));
			// 验证进入战斗界面后 HUD 切换到战斗模式、显示骷髅进度条(初值 0)、隐藏建造栏，以及暂停/倍速切换与恢复。
			TArray<UUserWidget*> DirectorHUDs;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, DirectorHUDs, UCastleDirectorHUDWidget::StaticClass(), true);
			if (Test->TestEqual(TEXT("战斗时视口存在唯一主界面 HUD"), DirectorHUDs.Num(), 1))
			{
				UCastleDirectorHUDWidget* BattleHUD = Cast<UCastleDirectorHUDWidget>(DirectorHUDs[0]);
				const UCastleIconBarWidget* SkullBar = Cast<UCastleIconBarWidget>(BattleHUD->GetWidgetFromName(TEXT("BattleProgressBar")));
				const UCastleHUDIconButtonWidget* PauseBtn = Cast<UCastleHUDIconButtonWidget>(BattleHUD->GetWidgetFromName(TEXT("BattlePauseButton")));
				const UCastleHUDIconButtonWidget* SpeedBtn = Cast<UCastleHUDIconButtonWidget>(BattleHUD->GetWidgetFromName(TEXT("BattleSpeedButton")));
				Test->TestEqual(TEXT("开战后 HUD 切到战斗模式"), BattleHUD->GetCurrentScreenMode(), ECastleHUDScreenMode::Battle);
				Test->TestEqual(TEXT("开战后顶部主塔血条持续显示 20 颗心"), BattleHUD->GetFilledHeartCount(), 20);
				Test->TestEqual(TEXT("开战瞬间骷髅已击杀进度为 0"), BattleHUD->GetFilledSkullCount(), 0);
				if (Test->TestNotNull(TEXT("战斗骷髅进度条存在"), SkullBar))
				{
					Test->TestEqual(TEXT("战斗模式显示底部骷髅进度条"), SkullBar->GetVisibility(), ESlateVisibility::HitTestInvisible);
				}
				if (Test->TestNotNull(TEXT("战斗倍速按钮存在"), SpeedBtn))
				{
					Test->TestEqual(TEXT("初始为一倍速图标(0)"), SpeedBtn->GetActiveIconIndex(), 0);
					Test->TestTrue(TEXT("切换至 2x 倍速"), Director->ToggleBattleSpeed());
					Test->TestEqual(TEXT("全局时间缩放变为 2.0"), UGameplayStatics::GetGlobalTimeDilation(World), 2.0f);
					Test->TestEqual(TEXT("图标切至二倍速(2)"), SpeedBtn->GetActiveIconIndex(), 2);
				}
				if (Test->TestNotNull(TEXT("战斗暂停按钮存在"), PauseBtn))
				{
					Test->TestEqual(TEXT("初始为暂停图标(0)"), PauseBtn->GetActiveIconIndex(), 0);
					Test->TestTrue(TEXT("战斗暂停成功"), Director->ToggleBattlePause());
					Test->TestTrue(TEXT("世界已进入战斗暂停"), World->IsPaused());
					Test->TestEqual(TEXT("暂停后图标切至继续(2)"), PauseBtn->GetActiveIconIndex(), 2);
					Test->TestTrue(TEXT("战斗恢复继续成功"), Director->ToggleBattlePause());
					Test->TestFalse(TEXT("世界已恢复运行"), World->IsPaused());
					Test->TestEqual(TEXT("继续后图标切回暂停(0)"), PauseBtn->GetActiveIconIndex(), 0);
				}
			}
			TArray<UUserWidget*> BuildBars;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, BuildBars, UCastleBuildBarWidget::StaticClass(), true);
			if (BuildBars.Num() == 1)
			{
				Test->TestEqual(TEXT("战斗界面隐藏建造栏"), BuildBars[0]->GetVisibility(), ESlateVisibility::Collapsed);
			}
			Stage = 3;
			return false;
		}
		if (Stage == 3)
		{
			if (Director->GetPhase() == ECastleDirectorPhase::Battle)
			{
				// 正常伤害命令保留群虫分裂及实体回收链路，不手动广播清场。
				IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleKillAllEnemies"), *GLog, World);
				return false;
			}
			if (!Test->TestEqual(TEXT("清场自动进入奖励"), Director->GetPhase(), ECastleDirectorPhase::Reward)) { return true; }
			Test->TestEqual(TEXT("清场后自动恢复 1x 倍速"), UGameplayStatics::GetGlobalTimeDilation(World), 1.0f);
			Test->TestFalse(TEXT("清场后倍速标志归零"), Director->IsBattleDoubleSpeed());
			Test->TestEqual(TEXT("只通过一关"), Director->ClearedSteps, 1);
			Test->TestFalse(TEXT("清场后出兵会话已回收"), Dispatch->IsSessionActive());
			Test->TestTrue(TEXT("奖励期间塔侧格保持隐藏"), Board->IsPlacementSurfacePresentationHidden());
			Test->AddExpectedError(TEXT("导演拒绝非法或重复清场通知"), EAutomationExpectedErrorFlags::Contains, 1);
			Director->Combat->OnBattleCleared.Broadcast(ECastleBattleKind::Normal, 1);
			Test->TestEqual(TEXT("重复通知不重复记关"), Director->ClearedSteps, 1);
			const int32 GoldBeforeReward = Director->Controller->GetEconomy()->GetGold();
			if (!Test->TestTrue(TEXT("选择金币恢复编辑"), Director->TryChooseReward(ECastleDirectorReward::Gold, Reason))) { return true; }
			Test->TestFalse(TEXT("选择奖励后显示塔侧格"), Board->IsPlacementSurfacePresentationHidden());
			Test->TestEqual(TEXT("实际发放60金币"), Director->Controller->GetEconomy()->GetGold() - GoldBeforeReward, 60);
			Test->TestTrue(TEXT("下一关节点已生成"), Director->Nodes.Num() >= 2);
			Test->TestFalse(TEXT("奖励关闭恢复移动"), Director->Controller->IsMoveInputIgnored());
			Test->TestFalse(TEXT("奖励关闭恢复视角"), Director->Controller->IsLookInputIgnored());
			Stage = 4;
			return false;
		}
		if (Stage == 4)
		{
			// 等待正式门到门导航的当前快照就绪后选择战斗节点。
			if (!Navigation->IsNavigationReady()) { return false; }
			// 第二关在 Mass 处理作用域中记录选择；边界内禁止创建 NPC，随后由导演 Tick 消费。
			if (Director->ClearedSteps == 1)
			{
				auto& Manager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
				{
					auto Processing = Manager.NewProcessingScope();
					Test->TestTrue(TEXT("实际Mass处理边界已进入"), Manager.IsProcessing());
					Director->HandleNodeTouched(Director->Nodes[0]);
					Test->TestFalse(TEXT("Mass处理期间只记录，不派出NPC"), Dispatch->IsSessionActive());
				}
				Director->Tick(0);
			}
			else if (!Test->TestTrue(TEXT("后续关卡开战"), Director->TryStartSelectedBattle(Director->Nodes[0], Reason)))
			{
				Test->AddError(Reason.ToString());
				return true;
			}
			if (!Test->TestEqual(TEXT("进入正式战斗阶段"), Director->GetPhase(), ECastleDirectorPhase::Battle)) { return true; }
			if (Director->ClearedSteps == 4)
			{
				Test->TestTrue(TEXT("第 4 关选择空岛后开始第 5 关精英战斗"),
					Director->GetStatusMessage().ToString().Contains(TEXT("第 5 关 · 精英战斗开始")));
			}
			if (Director->ClearedSteps == 5)
			{
				// 从实际战斗快照读取波数，避免测试只检查节点文字或选表公式。
				int32 Wave = 0, TotalWaves = 0;
				Test->TestTrue(TEXT("第 6 关实际生成战斗计划"), Director->Combat->GetCurrentWaveProgress(Wave, TotalWaves));
				Test->TestEqual(TEXT("第 6 关使用普通表第 5 项的两波配置"), TotalWaves, 2);
			}
			FText GateReason;
			Test->TestFalse(TEXT("战斗编辑门控关闭"), Director->IsEditingAllowed(GateReason));
			Test->TestFalse(TEXT("战斗阶段仍限制打开普通商店"), Director->IsShopAccessAllowed(GateReason));
			Test->TestFalse(TEXT("战斗保持飞行输入"), Director->Controller->IsMoveInputIgnored());
			Stage = 5;
			return false;
		}
		if (Stage == 5)
		{
			if (Director->GetPhase() == ECastleDirectorPhase::Battle)
			{
				IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleKillAllEnemies"), *GLog, World);
				return false;
			}
			if (Director->ClearedSteps == 5)
			{
				Test->TestEqual(TEXT("精英胜利自动进入下一步编辑"), Director->GetPhase(), ECastleDirectorPhase::Editing);
				Test->TestTrue(TEXT("精英胜利自动打开免费商店"), Director->Controller->GetShop()->IsShopOpen());
				Test->TestTrue(TEXT("首次精英胜利解锁雾瘴幼龙"), Director->Combat->HasUnlockedEnemyTag(TEXT("Unlock.MiasmaWelp")));
				Director->DiscardTowerUpgradeRewards();
				Director->Controller->GetShop()->CloseShop();
				Stage = 4;
				return false;
			}
			if (!Test->TestEqual(TEXT("普通战斗清场进入奖励"), Director->GetPhase(), ECastleDirectorPhase::Reward)) { return true; }
			ECastleDirectorReward Reward = ECastleDirectorReward::Gold;
			if (Director->ClearedSteps == 3)
			{
				Director->Tower->ApplyDamage(500);
				Reward = ECastleDirectorReward::Healing;
				// 用临时配置确保第 4 关有真实的休息岛节点，仍走导演生成的节点与返塔流程。
				Director->Config->NodeCountMin = Director->Config->NodeCountMax = 2;
				Director->Config->NormalNodeWeight = Director->Config->RewardNodeWeight = 0;
				Director->Config->RestNodeWeight = 1;
			}
			if (Director->ClearedSteps == 6) { Reward = ECastleDirectorReward::Shop; }
			const int32 Gold = Director->Controller->GetEconomy()->GetGold();
			const float HealthBeforeReward = Director->Tower->CurrentHealth;
			const int32 AttacksBeforeReward = Director->Tower->AttackCount;
			if (!Test->TestTrue(TEXT("实际奖励应用"), Director->TryChooseReward(Reward, Reason))) { Test->AddError(Reason.ToString()); return true; }
			if (Director->ClearedSteps == 2)
			{
				// 主塔成长由房间占格驱动，连续领取金币不能推进等级。
				Test->TestEqual(TEXT("第二次金币奖励到账60"), Director->Controller->GetEconomy()->GetGold() - Gold, 60);
				Test->TestEqual(TEXT("第二次领奖后仍是Lv1"), Director->Level, 1);
				Test->TestTrue(TEXT("房间未达标没有升级奖励"), Director->UpgradeRewards.IsEmpty());
			}
			if (Director->ClearedSteps == 3)
			{
				Test->TestEqual(TEXT("实际回血恢复300"), Director->Tower->CurrentHealth, FMath::Min(Director->Tower->MaxHealth, HealthBeforeReward + Director->Tower->MaxHealth * 0.3f));
				Test->TestEqual(TEXT("实际回血不改攻击次数"), Director->Tower->AttackCount, AttacksBeforeReward);
				if (!Test->TestTrue(TEXT("第 4 关生成休息岛节点"), Director->Nodes.Num() == 2
					&& Director->Nodes.Last()->GetKind() == ECastleDirectorNodeKind::RestIsland)) { return true; }
				Director->HandleNodeTouched(Director->Nodes.Last());
				Director->Tick(0);
				if (!Test->TestEqual(TEXT("第 4 关进入真实休息岛"), Director->GetPhase(), ECastleDirectorPhase::SkyIsland)) { return true; }
				const auto* Portal = Director->ActiveSkyIsland->FindComponentByClass<UCastleSkyIslandDepartureComponent>();
				if (!Test->TestNotNull(TEXT("找到休息岛传送门"), Portal)) { return true; }
				const FVector PortalLocation = Portal->GetComponentLocation();
				if (!Test->TestTrue(TEXT("准星命中离岛传送门"), Director->TryInteractWithSkyIsland(
					PortalLocation + FVector::UpVector * 450.0, PortalLocation, Reason))) { return true; }
				Director->Tick(0);
				Test->TestEqual(TEXT("返塔后待进入第 5 关"), Director->GetNextStep(), 5);
				if (!Test->TestTrue(TEXT("休息岛返塔发布第 5 关节点"), Director->GetPhase() == ECastleDirectorPhase::Editing
					&& Director->Nodes.Num() == 2)) { return true; }
				for (ACastleDirectorNodeActor* Node : Director->Nodes)
				{
					Test->TestEqual(TEXT("第5关全部精英节点"), Node->GetKind(), ECastleDirectorNodeKind::Elite);
					auto* Component = Node->FindComponentByClass<UWidgetComponent>();
					auto* Label = Component ? Cast<UCastleDirectorNodeLabelWidget>(Component->GetUserWidgetObject()) : nullptr;
					FText Text; FLinearColor Color;
					if (!Test->TestTrue(TEXT("精英节点屏幕标签实例有效"), Label && Label->TryGetNodeLabel(Text, Color))) { return true; }
					Test->TestTrue(TEXT("实际精英节点标签为红色"), Color.Equals(FLinearColor::Red));
					Test->TestEqual(TEXT("精英标签关卡文本"), Text.ToString(), FString(TEXT("精英 · 第 5 关")));
				}
			}
			if (Director->ClearedSteps == 6)
			{
				Test->TestTrue(TEXT("商店奖励自动打开"), Director->Controller->GetShop()->IsShopOpen());
				Test->TestEqual(TEXT("免费换批不扣金币"), Director->Controller->GetEconomy()->GetGold(), Gold);
				Director->Controller->ToggleShop();
				Test->TestFalse(TEXT("关闭商店后移动恢复"), Director->Controller->IsMoveInputIgnored());
				Director->Controller->ToggleShop();
				Test->TestTrue(TEXT("本轮B可重新打开商店"), Director->Controller->GetShop()->IsShopOpen());
			}
			Stage = Director->ClearedSteps < 6 ? 4 : 6;
			return false;
		}
		if (Stage == 6)
		{
			IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleDamageMainTower 99999"), *GLog, World);
			Test->TestEqual(TEXT("真实死亡进入游戏结束"), Director->GetPhase(), ECastleDirectorPhase::GameOver);
			Test->TestTrue(TEXT("真实死亡暂停世界"), World->IsPaused());
			Director->RestartCurrentMap();
			Stage = 7;
			return false;
		}
		if (Stage == 7)
		{
			if (Director->ClearedSteps != 0 || Director->GetPhase() != ECastleDirectorPhase::Editing) { return false; }
			Test->TestEqual(TEXT("重新加载重置等级"), Director->Level, 1);
			Test->TestEqual(TEXT("重新加载重置房间上限"), Director->GetCurrentRoomLimit(), 30);
			Test->TestEqual(TEXT("重新加载恢复 5×5×9 棋盘容量"), Board->GridSize, FIntVector(5, 5, 9));
			Test->TestEqual(TEXT("重新加载恢复主塔开局三层"), Board->GetUnlockedLayerCount(), 3);
			Test->TestTrue(TEXT("重新加载生成第一关节点"), Director->Nodes.Num() >= 2);
			// 最后验证战斗失败事件可见，不在非法状态继续单局。
			Test->AddExpectedError(TEXT("导演收到战斗失败"), EAutomationExpectedErrorFlags::Contains, 1);
			Director->Combat->OnBattleFailed.Broadcast(ECastleBattleKind::Normal, 1, FText::FromString(TEXT("自动化注入失败")));
			Test->TestEqual(TEXT("战斗失败进入游戏结束"), Director->GetPhase(), ECastleDirectorPhase::GameOver);
			Test->TestTrue(TEXT("失败暂停世界"), World->IsPaused());
			Test->TestTrue(TEXT("结束界面包含具体原因"), Director->GetGameOverFailure().ToString().Contains(TEXT("自动化注入失败")));
			return true;
		}
		Test->AddError(TEXT("导演验收阶段无效。"));
		return true;
	}
private:
	bool VerifyObliqueFloor(UWorld& World, const ACastleBoard& Board, const ACastleBuildingBase& FloorOwner,
		const FIntVector& Outward, const FVector& Expected, FVector& OutPoint)
	{
		// 同一立面用两个俯角复现玩家从塔外看入：较陡时看见室内地板，较浅时看见前侧护栏顶面。
		const FTransform& BoardTransform = Board.GetActorTransform();
		const FVector Side(Outward);
		const FVector Tangent(-Side.Y, Side.X, 0);
		const FVector Aim = Expected - BoardTransform.TransformVector(Tangent * 60.0);
		const FVector Start = Aim + BoardTransform.TransformVector(Side * 600.0 + FVector(0, 0, 360));
		const FVector End = Start + (Aim - Start).GetSafeNormal() * 100000.0;
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(PatrolObliqueFloor), true);
		const FString Label = FloorOwner.GetClass()->GetName();
		FHitResult VisibleHit, PawnHit;
		if (!Test->TestTrue(Label + TEXT(" 斜射线实际看见同一座塔的作者楼板"),
			World.LineTraceSingleByChannel(VisibleHit, Start, End, ECC_Visibility, Query)
			&& VisibleHit.GetActor() == &FloorOwner && VisibleHit.ImpactPoint.Equals(Aim, 0.1))
			|| !Test->TestTrue(Label + TEXT(" 隐藏墙盒仍阻挡 Pawn，且位置在可见楼板之前"),
				World.LineTraceSingleByChannel(PawnHit, Start, End, ECC_Pawn, Query) && PawnHit.GetActor() == &FloorOwner
				&& Cast<UBoxComponent>(PawnHit.GetComponent()) && PawnHit.Distance < VisibleHit.Distance)) { return false; }

		FText Reason;
		if (!Test->TestTrue(Label + TEXT(" 斜瞄可见楼板可放置路径点"),
			ACastlePatrolRoute::TryResolveWaypointFromRay(&World, Start, End, OutPoint, Reason)))
		{
			Test->AddError(Reason.ToString());
			return false;
		}
		Test->TestTrue(Label + TEXT(" 路点对齐所见楼板的本层格心及作者标高"), OutPoint.Equals(Expected, 0.1));

		const FVector ShallowStart = Aim + BoardTransform.TransformVector(Side * 600.0 + FVector(0, 0, 300));
		const FVector ShallowEnd = ShallowStart + (Aim - ShallowStart).GetSafeNormal() * 100000.0;
		if (!Test->TestTrue(Label + TEXT(" 较浅的射线命中的是护栏顶面"),
			World.LineTraceSingleByChannel(VisibleHit, ShallowStart, ShallowEnd, ECC_Visibility, Query)
			&& VisibleHit.GetActor() == &FloorOwner && VisibleHit.ImpactPoint.Z > Expected.Z + 100.0
			&& FVector::DotProduct(VisibleHit.ImpactNormal, Board.GetActorUpVector()) >= 0.99)) { return false; }
		// 视觉射线虽先遇到护栏顶面，纯楼板解析仍应沿原射线找到室内功能楼板。
		FVector ShallowPoint;
		if (!Test->TestTrue(Label + TEXT(" 射线越过护栏顶面并解析室内楼板"),
			ACastlePatrolRoute::TryResolveWaypointFromRay(&World, ShallowStart, ShallowEnd, ShallowPoint, Reason)))
		{
			Test->AddError(Reason.ToString());
			return false;
		}
		Test->TestTrue(Label + TEXT(" 护栏前景下的路径点仍吸附到楼板格心及作者标高"),
			ShallowPoint.Equals(Expected, 0.1));
		// 从塔外立面上方继续朝天空发射，射线不经过任何支撑面，拒绝结果必须可见。
		const FVector SkyStart = ShallowStart + FVector(0, 0, 100000.0);
		FVector RejectedPoint;
		return Test->TestFalse(Label + TEXT(" 无楼板或地面命中的射线拒绝放点"),
			ACastlePatrolRoute::TryResolveWaypointFromRay(&World, SkyStart, SkyStart + FVector(0, 0, 10000.0), RejectedPoint, Reason))
			&& Test->TestTrue(Label + TEXT(" 无命中时给出中文提示"), Reason.ToString().Contains(TEXT("未命中")));
	}

	FAutomationTestBase* Test;
	int32 Stage = 0;
	double Started = FPlatformTime::Seconds();
	TWeakObjectPtr<ACastleBuildingBase> Building;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorBattleIntegrationTest, "MovingCastle.Director.BattleIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleDirectorBattleIntegrationTest::RunTest(const FString& Parameters)
{
	// 完整战斗场景独占一次 MainMap PIE，结束后交回编辑器视口。
	if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FDirectorBattleScenario(this));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
	return true;
}
#endif
