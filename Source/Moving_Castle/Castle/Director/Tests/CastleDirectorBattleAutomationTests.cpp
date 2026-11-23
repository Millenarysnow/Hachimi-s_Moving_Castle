// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Actors/CastleBoard.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"
#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Components/CastleShopComponent.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Castle/Components/CastleEconomyComponent.h"
#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Units/Navigation/Interaction/CastlePatrolRoute.h"
#include "Units/Navigation/Interaction/CastlePatrolWaypointMarker.h"
#include "Units/Navigation/CastleNavigationSubsystem.h"
#include "Units/Mass/CastleMassSpawner.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Castle/UI/BuildBar/CastleBuildBarWidget.h"
#include "Castle/UI/Common/CastleHUDControls.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetSwitcher.h"
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
		auto* Board = Director->Board.Get();
		auto* Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
		auto* Dispatch = World->GetSubsystem<UCastleTowerDispatchSubsystem>();
		FText Reason;
		if (Stage == 0)
		{
			// 所有故障注入只改临时副本，不改磁盘上的 DA 或编辑器原始地图。
			Director->Config = DuplicateObject<UCastleDirectorConfigDataAsset>(Director->Config, World);
			Director->Config->CombatConfig = DuplicateObject<UCastleCombatConfigDataAsset>(Director->Config->CombatConfig, Director->Config);
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
			// 双门在相反两侧，补齐绕塔的两个直角连接格，确保开环不会穿过塔身。
			BridgeCard.Definition = Board->FindBuildingDefinition(ECastleBuildingType::NormalRoom);
			for (const int32 Side : {1, -1})
			{
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
			for (const FIntVector Cell : {Base + FIntVector(0, -1, 0), Base + FIntVector(-1, 0, 0), Base + FIntVector(1, 0, 1)})
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
				if (!Test->TestTrue(TEXT("对应楼内地板可投影为有效路线"), Navigation->RequestSharedPatrolRoute({FloorPoint}, Reason))) { Test->AddError(Reason.ToString()); return true; }
				const auto FloorPath = Navigation->AcquireSharedPath();
				if (!Test->TestTrue(TEXT("本层NavMesh投影通过"), FloorPath && FloorPath->bValid && FloorPath->bClosedLoop)) { Test->AddError(Navigation->LastFailure.ToString()); return true; }
				Test->TestTrue(TEXT("投影没有落到错误楼层"), FMath::Abs(FloorPath->Waypoints[0].Z - FloorPoint.Z) <= Navigation->ProjectionExtent.Z);
			}
			FVector Point;
			const FVector Center = Board->GetGridWorldLocation(Base + FIntVector(0, -1, 0));
			if (!Test->TestTrue(TEXT("楼顶射线解析到楼内地板"), ACastlePatrolRoute::TryResolveWaypointFromRay(
				World, Center + FVector(0, 0, 2000), Center - FVector(0, 0, 1000), Point, Reason))) { Test->AddError(Reason.ToString()); return true; }
			if (!Test->TestTrue(TEXT("提交正式门到门路线"), Navigation->RequestTowerDispatchRoute({Point}, Reason))) { Test->AddError(Reason.ToString()); return true; }
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
			Test->TestEqual(TEXT("失败不推进普通场次"), Director->NormalBattles, 0);
			Test->TestEqual(TEXT("失败保持编辑阶段"), Director->GetPhase(), ECastleDirectorPhase::Editing);
			Director->Config->CombatConfig->SpawnInnerRadius = Radius;
			Stage = 2;
			return false;
		}
		if (Stage == 2)
		{
			// 真正 MainMap 的巡逻入口应自动为已发布落点生成模型，而非依赖手工新增关卡 Actor。
			TActorIterator<ACastlePatrolWaypointMarker> Markers(World);
			if (Test->TestTrue(TEXT("正式 MainMap 已生成巡逻标记"), static_cast<bool>(Markers)))
			{
				Test->TestTrue(TEXT("标记属于正式巡逻入口，单点显示 1"),
					Cast<ACastlePatrolRoute>(Markers->GetOwner()) && Markers->GetDisplayedNumber() == 1);
				Test->TestTrue(TEXT("MainMap 的标记枢轴仍在 NPC 请求的楼内格心"),
					Markers->GetActorLocation().Equals(Navigation->GetRequestedWaypoints()[0]));
				++Markers;
				Test->TestFalse(TEXT("MainMap 不叠放多套标记"), static_cast<bool>(Markers));
			}
			if (!Test->TestTrue(TEXT("真实开战成功"), Director->TryStartSelectedBattle(Director->Nodes[0], Reason))) { Test->AddError(Reason.ToString()); return true; }
			int32 Reserve = 0, Active = 0;
			Dispatch->GetDispatchCounts(Reserve, Active);
			Test->TestEqual(TEXT("开战登记 8 名待命 NPC"), Reserve, 8);
			Test->TestEqual(TEXT("手动模式开战没有自动派兵"), Active, 0);
			Test->TestTrue(TEXT("战斗隐藏塔侧格"), Board->IsPlacementSurfacePresentationHidden());
			Test->TestEqual(TEXT("成功才清除节点"), Director->Nodes.Num(), 0);
			Test->TestEqual(TEXT("普通计数提交为1"), Director->NormalBattles, 1);
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
			if (!Test->TestTrue(TEXT("选择经验恢复编辑"), Director->TryChooseReward(ECastleDirectorReward::Experience, Reason))) { return true; }
			Test->TestFalse(TEXT("选择奖励后显示塔侧格"), Board->IsPlacementSurfacePresentationHidden());
			Test->TestEqual(TEXT("实际经验60"), Director->Experience, 60);
			Test->TestTrue(TEXT("下一关节点已生成"), Director->Nodes.Num() >= 2);
			Test->TestFalse(TEXT("奖励关闭恢复移动"), Director->Controller->IsMoveInputIgnored());
			Test->TestFalse(TEXT("奖励关闭恢复视角"), Director->Controller->IsLookInputIgnored());
			Stage = 4;
			return false;
		}
		if (Stage == 4)
		{
			// 升级新增塔身碰撞会重建正式导航，玩家需等新快照就绪才可选择战斗。
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
				Test->TestEqual(TEXT("第5关使用精英战斗"), Director->ActiveBattleKind, ECastleBattleKind::Elite);
				Test->TestEqual(TEXT("精英独立从第1场开始"), Director->EliteBattles, 1);
				Test->TestEqual(TEXT("普通场次不因精英增加"), Director->NormalBattles, 4);
			}
			FText GateReason;
			Test->TestFalse(TEXT("战斗编辑门控关闭"), Director->IsEditingAllowed(GateReason));
			Test->TestFalse(TEXT("战斗商店权限收回"), Director->IsShopAccessAllowed(GateReason));
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
				Stage = 6;
				return false;
			}
			if (!Test->TestEqual(TEXT("普通战斗清场进入奖励"), Director->GetPhase(), ECastleDirectorPhase::Reward)) { return true; }
			ECastleDirectorReward Reward = ECastleDirectorReward::Experience;
			if (Director->ClearedSteps == 3)
			{
				Director->Tower->ApplyDamage(500);
				Reward = ECastleDirectorReward::Healing;
			}
			if (Director->ClearedSteps == 4) { Reward = ECastleDirectorReward::Shop; }
			const int32 Gold = Director->Controller->GetEconomy()->GetGold();
			const float HealthBeforeReward = Director->Tower->CurrentHealth;
			const int32 AttacksBeforeReward = Director->Tower->AttackCount;
			if (!Test->TestTrue(TEXT("实际奖励应用"), Director->TryChooseReward(Reward, Reason))) { Test->AddError(Reason.ToString()); return true; }
			if (Director->ClearedSteps == 2)
			{
				Test->TestEqual(TEXT("第二次经验升到2级"), Director->Level, 2);
				Test->TestEqual(TEXT("2级房间上限250"), Director->Controller->GetShop()->RoomLimit, 250);
				Test->TestEqual(TEXT("Lv2 有三项待领取"), Director->UpgradeRewards.Num(), 3);
				Director->ClaimTowerUpgradeReward(0);
				Test->TestEqual(TEXT("点击只消耗一项"), Director->UpgradeRewards.Num(), 2);
				Director->DiscardTowerUpgradeRewards();
				Test->TestTrue(TEXT("关闭清空未领取项"), Director->UpgradeRewards.IsEmpty());
			}
			if (Director->ClearedSteps == 3)
			{
				Test->TestEqual(TEXT("实际回血恢复300"), Director->Tower->CurrentHealth, FMath::Min(Director->Tower->MaxHealth, HealthBeforeReward + Director->Tower->MaxHealth * 0.3f));
				Test->TestEqual(TEXT("实际回血不改攻击次数"), Director->Tower->AttackCount, AttacksBeforeReward);
			}
			if (Director->ClearedSteps == 4)
			{
				Test->TestTrue(TEXT("商店奖励自动打开"), Director->Controller->GetShop()->IsShopOpen());
				Test->TestEqual(TEXT("免费换批不扣金币"), Director->Controller->GetEconomy()->GetGold(), Gold);
				Director->Controller->ToggleShop();
				Test->TestFalse(TEXT("关闭商店后移动恢复"), Director->Controller->IsMoveInputIgnored());
				Director->Controller->ToggleShop();
				Test->TestTrue(TEXT("本轮B可重新打开商店"), Director->Controller->GetShop()->IsShopOpen());
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
			Stage = Director->ClearedSteps < 5 ? 4 : 6;
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
			Test->TestEqual(TEXT("重新加载重置经验"), Director->Experience, 0);
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
	FAutomationTestBase* Test;
	int32 Stage = 0;
	double Started = FPlatformTime::Seconds();
	TWeakObjectPtr<ACastleBuildingBase> Building;
};

/** 始终退出本测试启动的 PIE；正式地图内容不保存。 */
class FDirectorBattleStopPIE final : public IAutomationLatentCommand
{
public:
	bool Update() override
	{
		for (const auto& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType != EWorldType::PIE) { continue; }
			if (!bRequested)
			{
				UClass* Tool = FindObject<UClass>(nullptr, TEXT("/Script/EditorToolset.EditorAppToolset"));
				UFunction* Stop = Tool ? Tool->FindFunctionByName(TEXT("StopPIE")) : nullptr;
				check(Stop);
				struct FParams { UObject* ReturnValue = nullptr; } Params;
				Tool->GetDefaultObject()->ProcessEvent(Stop, &Params);
				bRequested = true;
			}
			return false;
		}
		return true;
	}
private:
	bool bRequested = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorBattleIntegrationTest, "MovingCastle.Director.BattleIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleDirectorBattleIntegrationTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TEXT("/Game/MovingCastle/Maps/MainMap"), true)) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FDirectorBattleScenario(this));
	ADD_LATENT_AUTOMATION_COMMAND(FDirectorBattleStopPIE());
	return true;
}
#endif
