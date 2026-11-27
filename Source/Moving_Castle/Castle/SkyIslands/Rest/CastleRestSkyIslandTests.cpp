#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CastleRestSkyIsland.h"
#include "Castle/Automation/CastleTowerOverviewTestUtilities.h"
#include "Castle/Components/CastleEconomyComponent.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorSettings.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Items/CastleBadgeDataAsset.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/Items/CastleItemGrantLibrary.h"
#include "CastleSkyIslandAcceptance.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

/** 用正式地图已有的主塔和金币账户验证台座，不另外维护替代账户或生命状态。 */
class FCastleRestSkyIslandSupplyScenario final : public IAutomationLatentCommand
{
public:
	FCastleRestSkyIslandSupplyScenario(FAutomationTestBase& InTest, TStrongObjectPtr<UCastleDirectorConfigDataAsset>&& InConfig)
		: Test(InTest), Config(MoveTemp(InConfig)) {}

	virtual bool Update() override
	{
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE) { World = Context.World(); break; }
		}
		auto* Director = World ? World->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
		if (!Director || !Director->IsRunning())
		{
			if (FPlatformTime::Seconds() - Started < 30.0) { return false; }
			Test.AddError(TEXT("休息空岛测试需要 MainMap 的导演完成开局。"));
			return true;
		}
		if (Stage == 0)
		{
			if (!Test.TestTrue(TEXT("导演从本次测试的独立内存资产启动"), Director->GetConfig() == Config.Get()))
			{
				return true;
			}
			const TActorIterator<ACastleDirectorNodeActor> Node(World);
			const auto* Controller = World->GetFirstPlayerController();
			if (!Node || !Controller || !Controller->GetPawn())
			{
				Test.AddError(TEXT("本局需要一个实际导演节点和玩家 Pawn。"));
				return true;
			}
			BeforeIsland = Director->GetClearedSteps();
			// 第一步按正式规则全是战斗节点。本测试只指定已登记节点的类型，后续完全由真实导演处理。
			if (!Test.TestTrue(TEXT("明确指定本次测试的休息节点"),
				Node->Configure(*Config, Director->GetNextStep(), ECastleDirectorNodeKind::RestIsland)))
			{
				return true;
			}
			CastleQueueSkyIslandAcceptance(World, ECastleSkyIslandType::Rest, ACastleRestSkyIsland::StaticClass());
			WaitUntil = FPlatformTime::Seconds() + 5.0;
			++Stage;
			return false;
		}
		if (Stage == 1)
		{
			if (Director->GetPhase() == ECastleDirectorPhase::Editing && FPlatformTime::Seconds() < WaitUntil) { return false; }
			if (!Test.TestEqual(TEXT("导演传送至休息空岛"), Director->GetPhase(), ECastleDirectorPhase::SkyIsland)) { return true; }
			if (!VerifySupply(*World, *Director)) { return true; }
			WaitUntil = FPlatformTime::Seconds() + 5.0;
			++Stage;
			return false;
		}
		if (Director->GetPhase() == ECastleDirectorPhase::SkyIsland && FPlatformTime::Seconds() < WaitUntil) { return false; }
		FText Reason;
		Test.TestEqual(TEXT("离岛恢复编辑阶段"), Director->GetPhase(), ECastleDirectorPhase::Editing);
		Test.TestEqual(TEXT("离岛只推进一步"), Director->GetClearedSteps(), BeforeIsland + 1);
		// 返塔关闭本次补给商店，恢复主塔编辑阶段的常驻商店入口。
		Test.TestTrue(TEXT("返塔后主塔普通商店可用"), Director->IsShopAccessAllowed(Reason));
		Test.TestFalse(TEXT("离岛销毁休息岛"), static_cast<bool>(TActorIterator<ACastleRestSkyIsland>(World)));
		Test.TestTrue(TEXT("离岛生成下一批节点"), static_cast<bool>(TActorIterator<ACastleDirectorNodeActor>(World)));
		const auto* Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
		if (Controller && Controller->GetPawn() && Controller->GetShop())
		{
			Test.TestFalse(TEXT("离岛后房间商店已关闭"), Controller->GetShop()->IsShopOpen());
			const TActorIterator<ACastleBoard> Board(World);
			if (Test.TestTrue(TEXT("返塔世界仍有主棋盘"), static_cast<bool>(Board)))
			{
				CastleTowerOverviewTests::Check(Test, **Board, *Controller);
				CastleTowerOverviewTests::Capture(TEXT("04-rest-island-return-tower-overview.png"));
			}
		}
		return true;
	}

private:
	bool VerifySupply(UWorld& World, UCastleDirectorSubsystem& Director)
	{
		auto* Controller = Cast<ACastlePlayerController>(World.GetFirstPlayerController());
		auto* Towers = World.GetSubsystem<UCastleMainTowerSubsystem>();
		auto* Tower = Towers ? Towers->GetRegisteredMainTower() : nullptr;
		const TActorIterator<ACastleRestSkyIsland> FoundIsland(&World);
		auto* Island = FoundIsland ? *FoundIsland : nullptr;
		if (!Test.TestNotNull(TEXT("本局控制器"), Controller)
			|| !Test.TestNotNull(TEXT("本局主塔"), Tower)
			|| !Test.TestNotNull(TEXT("休息空岛"), Island))
		{
			return false;
		}
		FText Reason;
		TInlineComponentArray<UCastleRestSkyIslandStation*> Stations(Island);
		Test.TestEqual(TEXT("三个独立实体台座"), Stations.Num(), 3);
		auto* Heal = FindObjectFast<UCastleRestSkyIslandStation>(Island, TEXT("HealStation"));
		auto* Gold = FindObjectFast<UCastleRestSkyIslandStation>(Island, TEXT("GoldStation"));
		auto* ShopStation = FindObjectFast<UCastleRestSkyIslandStation>(Island, TEXT("ShopStation"));
		auto* Departure = FindObjectFast<UCastleSkyIslandDepartureComponent>(Island, TEXT("DeparturePortal"));
		auto* Economy = Controller->GetEconomy();
		auto* Shop = Controller->GetShop();
		if (Test.TestNotNull(TEXT("回血台"), Heal) && Test.TestNotNull(TEXT("金币台"), Gold)
			&& Test.TestNotNull(TEXT("商店台"), ShopStation) && Test.TestNotNull(TEXT("离开门"), Departure)
			&& Test.TestNotNull(TEXT("本局金币账户"), Economy) && Test.TestNotNull(TEXT("本局房间商店"), Shop))
		{
			// 通过与 E 键相同的可见性射线进入组件。期望血量广播同时同步气球。
			const auto Interact = [&Director, &Reason](const UPrimitiveComponent& Target)
			{
				const FVector Center = Target.GetComponentLocation();
				return Director.TryInteractWithSkyIsland(Center - FVector(140, 0, 0), Center, Reason);
			};
			Test.TestTrue(TEXT("主塔受伤"), Tower->ApplyDamage(Tower->MaxHealth * 0.5f));
			Test.TestTrue(TEXT("首次领取回血台"), Interact(*Heal));
			Test.TestEqual(TEXT("恢复最大生命"), Tower->CurrentHealth, Tower->MaxHealth);
			int32 Balloons = 0;
			int32 Capacity = 0;
			Tower->GetBalloonCounts(Balloons, Capacity);
			Test.TestEqual(TEXT("气球恢复至当前容量"), Balloons, Capacity);
			Test.TestFalse(TEXT("回血台拒绝重复领取"), Interact(*Heal));
			Test.TestTrue(TEXT("回血台提示已领取"), Reason.ToString().Contains(TEXT("已领取")));

			// 从正式奖励入口装备零钱袋，射线领取仍使用 MainMap 上唯一玩家的账户。
			const auto* CoinPurse = LoadObject<UCastleBadgeDataAsset>(nullptr,
				TEXT("/Game/MovingCastle/Data/Items/Badges/DA_Badge_02_CoinPurse.DA_Badge_02_CoinPurse"));
			bool bNeedsReplacement = false;
			if (!Test.TestTrue(TEXT("发放带真实图标的管家零钱袋"), UCastleItemGrantLibrary::TryGrantBadge(
				CoinPurse, UCastleBadgeSubsystem::GetForWorld(&World), bNeedsReplacement, Reason) && !bNeedsReplacement)) { return false; }
			const int32 OldGold = Economy->GetGold();
			Test.TestTrue(TEXT("首次领取金币台"), Interact(*Gold));
			Test.TestFalse(TEXT("金币台拒绝重复领取"), Interact(*Gold));
			Test.TestEqual(TEXT("50 金币奖励只领取一次，零钱袋加成后到账 55"), Economy->GetGold(), OldGold + 55);
			Test.TestTrue(TEXT("首次交互打开免费房间商店"), Interact(*ShopStation));
			Test.TestTrue(TEXT("房间商店已打开"), Shop->IsShopOpen());
			Test.TestEqual(TEXT("换批免费"), Economy->GetGold(), OldGold + 55);
			Shop->CloseShop();
			Test.TestFalse(TEXT("岛上不允许通过 B 重新打开商店"), Director.IsShopAccessAllowed(Reason));
			Test.TestFalse(TEXT("商店台拒绝重复换批"), Interact(*ShopStation));
			Test.TestFalse(TEXT("重复交互不打开房间商店"), Shop->IsShopOpen());
			return Test.TestTrue(TEXT("离开门登记启程"), Interact(*Departure));
		}
		return false;
	}

	FAutomationTestBase& Test;
	TStrongObjectPtr<UCastleDirectorConfigDataAsset> Config;
	const double Started = FPlatformTime::Seconds();
	int32 BeforeIsland = 0;
	int32 Stage = 0;
	double WaitUntil = 0.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleRestSkyIslandSupplyTest, "MovingCastle.SkyIslands.Rest.Supplies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleRestSkyIslandSupplyTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<UCastleDirectorSettings>();
	const auto Previous = Settings->DirectorConfig;
	auto* SourceConfig = Settings->DirectorConfig.LoadSynchronous();
	if (!TestNotNull(TEXT("MainMap 正式导演配置"), SourceConfig)) { return false; }
	TStrongObjectPtr<UCastleDirectorConfigDataAsset> Config(DuplicateObject(SourceConfig, GetTransientPackage()));
	Config->SkyIslandClasses.Add(ECastleSkyIslandType::Rest, ACastleRestSkyIsland::StaticClass());
	Settings->DirectorConfig = Config.Get();
	// 全量队列可能保留其他测试的游戏世界。由本用例等待明确的 PIE 导演就绪。
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/MovingCastle/Maps/MainMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCastleRestSkyIslandSupplyScenario(*this, MoveTemp(Config)));
	// 后续地图用例需要等待本次 PIE 世界销毁，避免误读上一局导演和背包。
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Settings, Previous, bRequested = false]() mutable
	{
		if (!GEditor->PlayWorld)
		{
			Settings->DirectorConfig = Previous;
			return true;
		}
		if (!bRequested)
		{
			GEditor->RequestEndPlayMap();
			bRequested = true;
		}
		return false;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleRestSkyIslandConfigTest, "MovingCastle.SkyIslands.Rest.ConfigValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleRestSkyIslandConfigTest::RunTest(const FString& Parameters)
{
	// 非法配置只在临时对象上构造，项目中的二进制配置保持原样。
	auto* Asset = NewObject<UCastleRestSkyIslandDataAsset>();
	FText Reason;
	TestTrue(TEXT("合法初值"), Asset->Validate(Reason));
	AddExpectedError(TEXT("回血比例必须在"), EAutomationExpectedErrorFlags::Contains, 3);
	Asset->HealFraction = 0.0f;
	TestFalse(TEXT("拒绝零回血比例"), Asset->Validate(Reason));
	Asset->HealFraction = 1.01f;
	TestFalse(TEXT("拒绝超上限比例"), Asset->Validate(Reason));
	Asset->HealFraction = 1.0f;
	Asset->GoldReward = -1;
	TestFalse(TEXT("拒绝负金币奖励"), Asset->Validate(Reason));
	return true;
}

#endif
