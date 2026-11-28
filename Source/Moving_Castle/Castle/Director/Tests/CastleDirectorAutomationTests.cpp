// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "CastleBoard.h"
#include "CastlePlayerController.h"
#include "CastleMainTowerComponent.h"
#include "CastleShopComponent.h"
#include "CastleEconomyComponent.h"
#include "CastleBackpackComponent.h"
#include "CastleBuildingTestUtilities.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceNull.h"

/** 节点规则用正式纯函数证明：区间、强制精英、跨调用确定性。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorNodeRulesTest, "MovingCastle.Director.NodeRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleDirectorNodeRulesTest::RunTest(const FString& Parameters)
{
	UCastleDirectorConfigDataAsset* Config = NewObject<UCastleDirectorConfigDataAsset>();
	for (int32 Step = 1; Step <= 100; ++Step)
	{
		const int32 Count = UCastleDirectorSubsystem::GetNodeCountForStep(*Config, Step);
		TestTrue(TEXT("节点数为2或3"), Count >= 2 && Count <= 3);
		TestEqual(TEXT("同种子同关数确定性"), Count, UCastleDirectorSubsystem::GetNodeCountForStep(*Config, Step));
		TestEqual(TEXT("精英只出现在每5步"), UCastleDirectorSubsystem::GetNodeKindForStep(*Config, Step),
			Step % 5 == 0 ? ECastleDirectorNodeKind::Elite : ECastleDirectorNodeKind::Normal);
		const auto Kinds = UCastleDirectorSubsystem::GetNodeKindsForStep(*Config, Step);
		TestTrue(TEXT("节点批次完全可复现"), Kinds == UCastleDirectorSubsystem::GetNodeKindsForStep(*Config, Step));
		TestEqual(TEXT("完整类型批次满足节点数量"), Kinds.Num(), Count);
		const int32 Rest = Kinds.FilterByPredicate([](ECastleDirectorNodeKind Kind) { return Kind == ECastleDirectorNodeKind::RestIsland; }).Num();
		TestTrue(TEXT("至多一个休息节点"), Rest <= 1);
		if (Step == 1 || Step % 5 == 0)
		{
			for (const auto Kind : Kinds)
			{
				TestEqual(TEXT("首步全普通、五倍数步全精英"), Kind,
					Step == 1 ? ECastleDirectorNodeKind::Normal : ECastleDirectorNodeKind::Elite);
			}
		}
		else
		{
			TestTrue(TEXT("普通步始终有普通战斗节点"), Kinds.Contains(ECastleDirectorNodeKind::Normal));
		}
	}
	return true;
}

/** 使用真实世界、控制器和组件验证奖励状态、失败原子性、编辑锁和死亡转移。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorStateTest, "MovingCastle.Director.StateAndRewards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleDirectorStateTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("创建导演测试世界"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	CastleBuildingTests::ConfigureBoard(Board);
	ACastlePlayerController* Controller = World->SpawnActor<ACastlePlayerController>();
	AActor* TowerActor = World->SpawnActor<AActor>();
	UCastleMainTowerComponent* Tower = NewObject<UCastleMainTowerComponent>(TowerActor);
	Tower->InitializeHealth();
	TestTrue(TEXT("注册控制台验收主塔"), World->GetSubsystem<UCastleMainTowerSubsystem>()->RegisterMainTower(Tower));
	FOutputDeviceNull CommandOutput;
	AddExpectedError(TEXT("CastleDamageMainTower 失败"), EAutomationExpectedErrorFlags::Contains, 6);
	for (const TCHAR* Command : {TEXT("CastleDamageMainTower"), TEXT("CastleDamageMainTower 0"), TEXT("CastleDamageMainTower -1"),
		TEXT("CastleDamageMainTower nan"), TEXT("CastleDamageMainTower inf"), TEXT("CastleDamageMainTower 100abc")})
	{
		TestTrue(TEXT("命令由正式控制台识别"), IConsoleManager::Get().ProcessUserConsoleInput(Command, CommandOutput, World));
		TestEqual(TEXT("非法伤害不改生命"), Tower->CurrentHealth, 1000.0f);
	}
	IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleDamageMainTower 50"), CommandOutput, World);
	TestEqual(TEXT("合法伤害经过主塔接口扣血"), Tower->CurrentHealth, 950.0f);
	Tower->InitializeHealth();
	UCastleDirectorSubsystem* Director = World->GetSubsystem<UCastleDirectorSubsystem>();
	UCastleDirectorConfigDataAsset* Config = NewObject<UCastleDirectorConfigDataAsset>();
	FText Reason;
	Board->PlacementSurfaceMode = ECastlePlacementSurfaceMode::GroundBoard;
	Board->GridSize = FIntVector(3, 3, 8);
	if (!TestTrue(TEXT("导演测试显式生成旧地面建造面"), Board->GenerateGround(Reason))) { AddError(Reason.ToString()); return false; }
	TestTrue(TEXT("导演未运行时允许编辑"), Director->IsEditingAllowed(Reason));
	TestTrue(TEXT("导演未运行时允许旧商店"), Director->IsShopAccessAllowed(Reason));
	Director->ConfigureForAutomation(Config, Board, Controller, Tower);
	int32 AddedIndex = INDEX_NONE;
	// 以两张一级卡验证 HUD 独立于商店的数据来源，保持期望的总占格数。
	for (int32 Index = 0; Index < 2; ++Index)
	{
		TestTrue(TEXT("为 HUD 计数配置一张一级卡"), Board->PrototypeBackpack->TryAddBuilding(
			Board->FindBuildingDefinition(ECastleBuildingType::ArcherTower), 1, ECastleTileColor::Red, 3, AddedIndex, Reason));
	}
	TestFalse(TEXT("HUD读取不提前初始化商店"), Controller->GetShop()->IsInitialized());
	TestTrue(TEXT("商店未初始化时HUD已有正确占用格数"), Director->GetHUDSummary().Contains(TEXT("房间 2/30")));
	// 正式单局的编辑阶段从开局起允许使用普通商店，奖励是否选店只影响免费换批事务。
	TestTrue(TEXT("开局编辑可用"), Director->IsEditingAllowed(Reason));
	TestTrue(TEXT("正式单局开局即可使用普通商店"), Director->IsShopAccessAllowed(Reason));
	TestEqual(TEXT("开局房间上限30"), Controller->GetShop()->RoomLimit, 30);
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Battle);
	TestFalse(TEXT("战斗禁止编辑"), Director->IsEditingAllowed(Reason));
	TestFalse(TEXT("战斗阶段仍禁止打开普通商店"), Director->IsShopAccessAllowed(Reason));
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Reward);
	TestFalse(TEXT("奖励禁止编辑"), Director->IsEditingAllowed(Reason));
	TestFalse(TEXT("奖励选择期间仍禁止打开普通商店"), Director->IsShopAccessAllowed(Reason));
	// 独立地面关卡保留原有奖励与失败验证，正式塔身成长由 MainMap 集成测试覆盖。
	UCastleEconomyComponent* Economy = Controller->GetEconomy();
	const int32 InitialGold = Economy->GetGold();
	if (!TestTrue(TEXT("金币奖励成功"), Director->TryChooseReward(ECastleDirectorReward::Gold, Reason))) { AddError(Reason.ToString()); return false; }
	TestTrue(TEXT("未选择商店奖励也保持普通商店可用"), Director->IsShopAccessAllowed(Reason));
	TestEqual(TEXT("金币奖励不改变主塔等级"), Director->GetLevel(), 1);
	TestEqual(TEXT("净发放60金币"), Economy->GetGold() - InitialGold, 60);
	TestEqual(TEXT("房间上限仍为30"), Controller->GetShop()->RoomLimit, 30);
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Reward);
	Tower->ApplyDamage(500.0f);
	const int32 AttackCount = Tower->AttackCount;
	if (!TestTrue(TEXT("回血奖励成功"), Director->TryChooseReward(ECastleDirectorReward::Healing, Reason))) { AddError(Reason.ToString()); return false; }
	TestTrue(TEXT("后续编辑阶段仍可使用普通商店"), Director->IsShopAccessAllowed(Reason));
	TestEqual(TEXT("回复最大生命30%"), Tower->CurrentHealth, 800.0f);
	TestEqual(TEXT("回血不增加攻击次数"), Tower->AttackCount, AttackCount);
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Reward);
	if (!TestTrue(TEXT("商店奖励成功"), Director->TryChooseReward(ECastleDirectorReward::Shop, Reason))) { AddError(Reason.ToString()); return false; }
	TestTrue(TEXT("商店自动打开"), Controller->GetShop()->IsShopOpen());
	TestTrue(TEXT("本轮商店权限可用"), Director->IsShopAccessAllowed(Reason));
	// 商店奖励实际生成的每件商品都必须服从已提交的 Lv1 数字范围。
	TestEqual(TEXT("商店奖励时仍为Lv1"), Director->GetLevel(), 1);
	for (const auto& Offer : Controller->GetShop()->GetOffers())
	{
		TestTrue(TEXT("Lv1 商品数字在 1 至 3 之间"), Offer.Descriptor.Number >= 1 && Offer.Descriptor.Number <= 3);
	}
	Controller->GetShop()->CloseShop();
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Battle);
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Reward);
	const int32 BeforeLevel = Director->GetLevel();
	const int32 BeforeGold = Economy->GetGold();
	const int32 BeforeLimit = Controller->GetShop()->RoomLimit;
	const float BeforeHealth = Tower->CurrentHealth;
	const auto BeforeOffers = Controller->GetShop()->GetOffers();
	if (!TestTrue(TEXT("成功提交商店奖励后有商品可核对"), !BeforeOffers.IsEmpty())) { return false; }
	const auto Definitions = Board->BuildingDefinitions;
	// 独立测试直接切阶段，补齐生产清场前已隐藏建造面的前置条件。
	TestTrue(TEXT("模拟清场前隐藏建造面"), Board->SetPlacementSurfacePresentationHidden(true, Reason));
	Board->BuildingDefinitions.Reset();
	AddExpectedError(TEXT("奖励换批失败"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("导演奖励失败"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("商品池失效拒绝奖励"), Director->TryChooseReward(ECastleDirectorReward::Shop, Reason));
	TestEqual(TEXT("失败保留奖励阶段"), Director->GetPhase(), ECastleDirectorPhase::Reward);
	TestEqual(TEXT("失败保留等级"), Director->GetLevel(), BeforeLevel);
	TestEqual(TEXT("失败保留金币"), Economy->GetGold(), BeforeGold);
	TestEqual(TEXT("失败保留房间上限"), Controller->GetShop()->RoomLimit, BeforeLimit);
	TestEqual(TEXT("失败保留生命"), Tower->CurrentHealth, BeforeHealth);
	const auto& AfterOffers = Controller->GetShop()->GetOffers();
	TestTrue(TEXT("失败保留商品"), AfterOffers.Num() == BeforeOffers.Num() && AfterOffers[0].Descriptor.Number == BeforeOffers[0].Descriptor.Number);
	TestTrue(TEXT("失败重新隐藏地面建造面"), Board->IsPlacementSurfacePresentationHidden());
	Board->BuildingDefinitions = Definitions;
	Tower->MaxHealth = -1.0f;
	AddExpectedError(TEXT("导演奖励失败"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("无效主塔配置拒绝回血奖励"), Director->TryChooseReward(ECastleDirectorReward::Healing, Reason));
	TestEqual(TEXT("回血失败保留生命"), Tower->CurrentHealth, BeforeHealth);
	TestEqual(TEXT("回血失败保留奖励阶段"), Director->GetPhase(), ECastleDirectorPhase::Reward);
	Tower->MaxHealth = 1000.0f;
	for (const ECastleDirectorPhase Phase : {ECastleDirectorPhase::Editing, ECastleDirectorPhase::Battle, ECastleDirectorPhase::Reward})
	{
		Tower->InitializeHealth();
		Director->SetPhaseForAutomation(Phase);
		Tower->ApplyDamage(99999.0f);
		TestEqual(TEXT("任意阶段主塔死亡都结束游戏"), Director->GetPhase(), ECastleDirectorPhase::GameOver);
		TestFalse(TEXT("结束后禁止编辑"), Director->IsEditingAllowed(Reason));
	}
	return true;
}
/** 连续开启/关闭奖励和商店时，两种输入忽略计数都必须完整归零。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorModalInputTest, "MovingCastle.Director.ModalInput",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleDirectorModalInputTest::RunTest(const FString& Parameters)
{
	// 该独立测试世界不跑整张地图 BeginPlay；显式允许 Actor 动态事件，使商店正式通知链路可执行。
	TGuardValue<bool> AllowActorEvents(GAllowActorScriptExecutionInEditor, true);
 const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
  .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
 if (!TestNotNull(TEXT("创建输入测试世界"), World)) { return false; }
 ON_SCOPE_EXIT { World->DestroyWorld(false); };
 ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
 CastleBuildingTests::ConfigureBoard(Board);
 ACastlePlayerController* Controller = World->SpawnActor<ACastlePlayerController>();
 FText Reason;
 Controller->SetDirectorModalState(true, false, nullptr);
 TestTrue(TEXT("奖励打开忽略移动"), Controller->IsMoveInputIgnored());
 TestTrue(TEXT("奖励打开忽略视角"), Controller->IsLookInputIgnored());
 Controller->SetDirectorModalState(true, false, nullptr);
 TestTrue(TEXT("奖励期间商店事务打开"), Controller->GetShop()->TryOpenForReward(Controller->GetEconomy(), Board, Reason));
 Controller->SetDirectorModalState(false, false, nullptr);
 TestTrue(TEXT("奖励关闭但商店仍打开时移动仍锁定"), Controller->IsMoveInputIgnored());
 Controller->GetShop()->CloseShop();
 TestFalse(TEXT("奖励到商店再关闭后移动归零"), Controller->IsMoveInputIgnored());
 TestFalse(TEXT("奖励到商店再关闭后视角归零"), Controller->IsLookInputIgnored());
 Controller->SetDirectorModalState(true, false, nullptr);
 Controller->SetDirectorModalState(false, false, nullptr);
 TestFalse(TEXT("单独奖励关闭移动归零"), Controller->IsMoveInputIgnored());
 TestFalse(TEXT("单独奖励关闭视角归零"), Controller->IsLookInputIgnored());
 return true;
}


#endif
