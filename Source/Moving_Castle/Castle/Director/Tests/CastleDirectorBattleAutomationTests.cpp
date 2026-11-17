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
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Components/CastleShopComponent.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Castle/Components/CastleEconomyComponent.h"
#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Units/Navigation/Interaction/CastlePatrolRoute.h"
#include "Units/Navigation/CastleNavigationSubsystem.h"
#include "Units/Mass/CastleMassSpawner.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Components/WidgetComponent.h"
#include "MassEntitySubsystem.h"




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
			Test->TestTrue(TEXT("重叠回调只记录，不立即派兵"), Director->PendingNode.Get() == First && !Director->BattleNPCSpawner);
			Director->HandleNodeTouched(Director->Nodes.Last());
			Test->TestTrue(TEXT("同帧多个节点保留首次选择"), Director->PendingNode.Get() == First);
			Director->Tick(0);
			Test->TestEqual(TEXT("无路线拒绝后仍编辑"), Director->GetPhase(), ECastleDirectorPhase::Editing);
			Test->TestTrue(TEXT("无路线提示可见且节点保留"), Director->GetStatusMessage().ToString().Contains(TEXT("无法开战")) && IsValid(First));
			// 玩家实际可放的一级炮塔作为导航输入；通过真实放置和完整飞入使导航接收变化。
			FCastleGroundCell Ground;
			if (!Test->TestTrue(TEXT("查询测试落点地面"), Board->TryGetGroundCell(FIntVector(0, 0, 0), Ground))) { return true; }
			FCastleBuildingDescriptor Card;
			Card.Definition = Board->FindBuildingDefinition(ECastleBuildingType::CannonTower);
			Card.Level = 1; Card.Color = Ground.Color; Card.Number = Ground.Number;
			Building = Board->PlaceBuilding(Ground.Coordinate, Card, false, Reason);
			if (!Test->TestNotNull(TEXT("放置正式一级炮塔"), Building.Get())) { Test->AddError(Reason.ToString()); return true; }
			CastleBuildingTests::CompletePlacement(Building.Get());
			// 另外放置一级箭塔和两层四级农田，分别验证其他楼顶及上层楼板。
			for (const FIntVector Cell : {FIntVector(1, 0, 0), FIntVector(3, 3, 0)})
			{
				if (!Test->TestTrue(TEXT("取得额外测试地面"), Board->TryGetGroundCell(Cell, Ground))) { return true; }
				Card.Definition = Board->FindBuildingDefinition(Cell.X == 1 ? ECastleBuildingType::ArcherTower : ECastleBuildingType::Farm);
				Card.Level = Cell.X == 1 ? 1 : 4; Card.Color = Ground.Color; Card.Number = Ground.Number;
				auto* Placed = Board->PlaceBuilding(Cell, Card, false, Reason);
				if (!Test->TestNotNull(TEXT("放置其他建筑和多层建筑"), Placed)) { Test->AddError(Reason.ToString()); return true; }
				CastleBuildingTests::CompletePlacement(Placed);
			}
			Stage = 1;
			return false;
		}
		if (Stage == 1)
		{
			if (!Navigation->IsNavigationReady()) { return false; }
			for (const FIntVector Cell : {FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(3, 3, 1)})
			{
				const FVector GridCenter = Board->GetGridWorldLocation(Cell);
				FVector FloorPoint;
				if (!Test->TestTrue(TEXT("炮塔/其他建筑/上层楼顶能解析"), ACastlePatrolRoute::TryResolveWaypointFromRay(
					World, GridCenter + FVector(0, 0, 2000), GridCenter - FVector(0, 0, 1000), FloorPoint, Reason))) { Test->AddError(Reason.ToString()); return true; }
				const FVector Expected = GridCenter - Board->GetActorTransform().TransformVector(FVector(0, 0, Board->LayerHeight * 0.5));
				Test->TestTrue(TEXT("楼顶点严格取本层底面锚点"), FloorPoint.Equals(Expected, 0.1));
				if (!Test->TestTrue(TEXT("对应楼内地板可投影为有效路线"), Navigation->RequestSharedPatrolRoute({FloorPoint}, Reason))) { Test->AddError(Reason.ToString()); return true; }
				const auto FloorPath = Navigation->AcquireSharedPath();
				if (!Test->TestTrue(TEXT("本层NavMesh投影通过"), FloorPath && FloorPath->bValid && FloorPath->bClosedLoop)) { Test->AddError(Navigation->LastFailure.ToString()); return true; }
				Test->TestTrue(TEXT("投影没有落到错误楼层"), FMath::Abs(FloorPath->Waypoints[0].Z - FloorPoint.Z) <= Navigation->ProjectionExtent.Z);
			}
			FVector Point;
			const FVector Center = Board->GetGridWorldLocation(FIntVector(0, 0, 0));
			if (!Test->TestTrue(TEXT("楼顶射线解析到楼内地板"), ACastlePatrolRoute::TryResolveWaypointFromRay(
				World, Center + FVector(0, 0, 2000), Center - FVector(0, 0, 1000), Point, Reason))) { Test->AddError(Reason.ToString()); return true; }
			if (!Test->TestTrue(TEXT("提交正式单点闭环待命路线"), Navigation->RequestSharedPatrolRoute({Point}, Reason))) { Test->AddError(Reason.ToString()); return true; }
			const auto Path = Navigation->AcquireSharedPath();
			if (!Test->TestTrue(TEXT("生成前实际闭环已就绪"), Path && Path->bValid && Path->bClosedLoop)) { Test->AddError(Navigation->LastFailure.ToString()); return true; }
			// NPC 已生成后令战斗配置非法，证明最后一步失败仍能收回整批 NPC 并恢复地块。
			const float Radius = Director->Config->CombatConfig->SpawnInnerRadius;
			Director->Config->CombatConfig->SpawnInnerRadius = -1;
			Test->AddExpectedError(TEXT("StartBattle："), EAutomationExpectedErrorFlags::Contains, 1);
			Test->AddExpectedError(TEXT("导演开战失败"), EAutomationExpectedErrorFlags::Contains, 1);
			Test->TestFalse(TEXT("战斗系统拒绝导致整次开战失败"), Director->TryStartSelectedBattle(Director->Nodes[0], Reason));
			Test->TestTrue(TEXT("失败不销毁节点"), Director->Nodes.Num() >= 2 && IsValid(Director->Nodes[0]));
			Test->TestFalse(TEXT("失败恢复地块"), Board->IsGroundPresentationHidden());
			Test->TestFalse(TEXT("失败销毁NPC生成器"), IsValid(Director->BattleNPCSpawner));
			Test->TestEqual(TEXT("失败不推进普通场次"), Director->NormalBattles, 0);
			Test->TestEqual(TEXT("失败保持编辑阶段"), Director->GetPhase(), ECastleDirectorPhase::Editing);
			Director->Config->CombatConfig->SpawnInnerRadius = Radius;
			Stage = 2;
			return false;
		}
		if (Stage == 2)
		{
			if (!Test->TestTrue(TEXT("真实开战成功"), Director->TryStartSelectedBattle(Director->Nodes[0], Reason))) { Test->AddError(Reason.ToString()); return true; }
			Test->TestEqual(TEXT("同步派出恰好8个NPC"), Director->BattleNPCSpawner->SpawnedUnits.Num(), 8);
			Test->TestTrue(TEXT("战斗隐藏地块"), Board->IsGroundPresentationHidden());
			Test->TestEqual(TEXT("成功才清除节点"), Director->Nodes.Num(), 0);
			Test->TestEqual(TEXT("普通计数提交为1"), Director->NormalBattles, 1);
			Test->TestTrue(TEXT("HUD显示真实剩余敌人"), Director->GetHUDSummary().Contains(TEXT("剩余敌人")));
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
			Test->TestEqual(TEXT("只通过一关"), Director->ClearedSteps, 1);
			Test->TestFalse(TEXT("清场后NPC生成器已回收"), IsValid(Director->BattleNPCSpawner));
			Test->TestTrue(TEXT("奖励期间地块保持隐藏"), Board->IsGroundPresentationHidden());
			Test->AddExpectedError(TEXT("导演拒绝非法或重复清场通知"), EAutomationExpectedErrorFlags::Contains, 1);
			Director->Combat->OnBattleCleared.Broadcast(ECastleBattleKind::Normal, 1);
			Test->TestEqual(TEXT("重复通知不重复记关"), Director->ClearedSteps, 1);
			if (!Test->TestTrue(TEXT("选择经验恢复编辑"), Director->TryChooseReward(ECastleDirectorReward::Experience, Reason))) { return true; }
			Test->TestFalse(TEXT("选择奖励后显示地块"), Board->IsGroundPresentationHidden());
			Test->TestEqual(TEXT("实际经验60"), Director->Experience, 60);
			Test->TestTrue(TEXT("下一关节点已生成"), Director->Nodes.Num() >= 2);
			Test->TestFalse(TEXT("奖励关闭恢复移动"), Director->Controller->IsMoveInputIgnored());
			Test->TestFalse(TEXT("奖励关闭恢复视角"), Director->Controller->IsLookInputIgnored());
			Stage = 4;
			return false;
		}
		if (Stage == 4)
		{
			// 第二关在 Mass 处理作用域中记录选择；边界内禁止创建 NPC，随后由导演 Tick 消费。
			if (Director->ClearedSteps == 1)
			{
				auto& Manager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
				{
					auto Processing = Manager.NewProcessingScope();
					Test->TestTrue(TEXT("实际Mass处理边界已进入"), Manager.IsProcessing());
					Director->HandleNodeTouched(Director->Nodes[0]);
					Test->TestFalse(TEXT("Mass处理期间只记录，不派出NPC"), IsValid(Director->BattleNPCSpawner));
				}
				Director->Tick(0);
			}
			else if (!Test->TestTrue(TEXT("后续关卡开战"), Director->TryStartSelectedBattle(Director->Nodes[0], Reason))) { return true; }
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
			if (!Test->TestEqual(TEXT("后续战斗清场进入奖励"), Director->GetPhase(), ECastleDirectorPhase::Reward)) { return true; }
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
				Test->TestEqual(TEXT("2级房间上限20"), Director->Controller->GetShop()->RoomLimit, 20);
			}
			if (Director->ClearedSteps == 3)
			{
				Test->TestEqual(TEXT("实际回血恢复300"), Director->Tower->CurrentHealth, FMath::Min(Director->Tower->MaxHealth, HealthBeforeReward + 300.0f));
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
					Test->TestEqual(TEXT("精英标签关卡文本"), Text.ToString(), FString(TEXT("精英战斗 · 第 5 关")));
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
			Test->TestEqual(TEXT("重新加载重置棋盘5x5"), Board->GridSize, FIntVector(5, 5, 8));
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
