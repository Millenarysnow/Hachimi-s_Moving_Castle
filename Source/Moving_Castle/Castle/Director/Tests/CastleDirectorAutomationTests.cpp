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
#include "CastleBackpackComponent.h"
#include "CastleBuildingTestUtilities.h"
#include "Engine/World.h"
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
	TestTrue(TEXT("导演未运行时允许编辑"), Director->IsEditingAllowed(Reason));
	TestTrue(TEXT("导演未运行时允许旧商店"), Director->IsShopAccessAllowed(Reason));
	Director->ConfigureForAutomation(Config, Board, Controller, Tower);
	int32 AddedIndex = INDEX_NONE;
	TestTrue(TEXT("为HUD计数显式配置两格卡"), Board->PrototypeBackpack->TryAddBuilding(
		Board->FindBuildingDefinition(ECastleBuildingType::ArcherTower), 2, ECastleTileColor::Red, 3, AddedIndex, Reason));
	TestFalse(TEXT("HUD读取不提前初始化商店"), Controller->GetShop()->IsInitialized());
	TestTrue(TEXT("商店未初始化时HUD已有正确占用格数"), Director->GetHUDSummary().Contains(TEXT("房间 2/15")));
	TestTrue(TEXT("开局编辑可用"), Director->IsEditingAllowed(Reason));
	TestFalse(TEXT("开局没有商店权限"), Director->IsShopAccessAllowed(Reason));
	TestEqual(TEXT("开局上限15"), Controller->GetShop()->RoomLimit, 15);
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Battle);
	TestFalse(TEXT("战斗禁止编辑"), Director->IsEditingAllowed(Reason));
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Reward);
	TestFalse(TEXT("奖励禁止编辑"), Director->IsEditingAllowed(Reason));
	Config->RewardExp = 360;
	TestTrue(TEXT("经验奖励可一次连升多级"), Director->TryChooseReward(ECastleDirectorReward::Experience, Reason));
	TestEqual(TEXT("主塔4级"), Director->GetLevel(), 4);
	TestEqual(TEXT("剩余60经验"), Director->GetExperience(), 60);
	TestEqual(TEXT("房间上限30写入商店"), Controller->GetShop()->RoomLimit, 30);
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Reward);
	Tower->ApplyDamage(500.0f);
	const int32 AttackCount = Tower->AttackCount;
	TestTrue(TEXT("回血奖励成功"), Director->TryChooseReward(ECastleDirectorReward::Healing, Reason));
	TestEqual(TEXT("回复最大生命30%"), Tower->CurrentHealth, 800.0f);
	TestEqual(TEXT("回血不增加攻击次数"), Tower->AttackCount, AttackCount);
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Reward);
	TestTrue(TEXT("商店奖励成功"), Director->TryChooseReward(ECastleDirectorReward::Shop, Reason));
	TestTrue(TEXT("商店自动打开"), Controller->GetShop()->IsShopOpen());
	TestTrue(TEXT("本轮商店权限可用"), Director->IsShopAccessAllowed(Reason));
	Controller->GetShop()->CloseShop();
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Battle);
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Reward);
	const int32 BeforeLevel = Director->GetLevel();
	const int32 BeforeExp = Director->GetExperience();
	const int32 BeforeLimit = Controller->GetShop()->RoomLimit;
	const float BeforeHealth = Tower->CurrentHealth;
	const auto BeforeOffers = Controller->GetShop()->GetOffers();
	const auto Definitions = Board->BuildingDefinitions;
	Board->BuildingDefinitions.Reset();
	AddExpectedError(TEXT("奖励换批失败"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("导演奖励失败"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("商品池失效拒绝奖励"), Director->TryChooseReward(ECastleDirectorReward::Shop, Reason));
	TestEqual(TEXT("失败保留奖励阶段"), Director->GetPhase(), ECastleDirectorPhase::Reward);
	TestEqual(TEXT("失败保留等级"), Director->GetLevel(), BeforeLevel);
	TestEqual(TEXT("失败保留经验"), Director->GetExperience(), BeforeExp);
	TestEqual(TEXT("失败保留房间上限"), Controller->GetShop()->RoomLimit, BeforeLimit);
	TestEqual(TEXT("失败保留生命"), Tower->CurrentHealth, BeforeHealth);
	TestEqual(TEXT("失败保留商品"), Controller->GetShop()->GetOffers()[0].Descriptor.Number, BeforeOffers[0].Descriptor.Number);
	Board->BuildingDefinitions = Definitions;
	Config->RewardExp = MAX_int32;
	Config->RoomLimitPerLevel = MAX_int32;
	TestFalse(TEXT("升级溢出拒绝经验奖励"), Director->TryChooseReward(ECastleDirectorReward::Experience, Reason));
	TestEqual(TEXT("溢出保留经验"), Director->GetExperience(), BeforeExp);
	Config->RoomLimitPerLevel = 5;
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
#endif
