#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Items/CastleBadgeRuntimeSubsystem.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/Support/CastleSupportTowerData.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"
#include "CastleBackpackComponent.h"
#include "CastleEconomyComponent.h"
#include "CastleMainTowerComponent.h"
#include "CastleMassFragments.h"
#include "CastleMassProcessors.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleNavigationSubsystem.h"
#include "CastleShopComponent.h"
#include "CastleTowerDispatchConfigDataAsset.h"
#include "CastleTowerDispatchSubsystem.h"
#include "Components/Building/Effects/Attack/CastleTowerAttackEffectComponent.h"
#include "Components/Building/Effects/Farm/CastleFarmEffectComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Mass/CastleMassPatrolProcessor.h"
#include "MassEntityQuery.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"
#include "Subsystems/CastleShieldTowerSubsystem.h"
#include "Units/Mass/Projectiles/CastleMassHomingProjectileProcessor.h"

namespace CastleBadgesFullTests
{
	using EBadge = ECastleBadgeType;
	constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** 每个用例隔离真实 GameInstance 库存和世界子系统，只手动推进所需的生产处理器。 */
	struct FGame
	{
		FAutomationTestBase& Test;
		UGameInstance* Instance;
		UWorld* World;
		ACastleBoard* Board;
		ACastlePlayerController* Player;
		UCastleBadgeSubsystem* Badges;
		UCastleDirectorSubsystem* Director;
		UCastleBadgeRuntimeSubsystem* Runtime;
		UCastleEconomyComponent* Economy;
		TUniquePtr<CastleBuildingTests::FScopedNativeBuildingModels> Models;

		explicit FGame(FAutomationTestBase& InTest) : Test(InTest)
		{
			Instance = NewObject<UGameInstance>(GEngine);
			Instance->InitializeStandalone();
			const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
				.CreatePhysicsScene(true).CreateNavigation(true).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->SetGameInstance(Instance);
			const auto Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			check(Navigation && Navigation->GetDefaultNavDataInstance(FNavigationSystem::Create));
			Models = MakeUnique<CastleBuildingTests::FScopedNativeBuildingModels>(World);
			Board = World->SpawnActor<ACastleBoard>();
			CastleBuildingTests::ConfigureBoard(Board);
			Player = World->SpawnActor<ACastlePlayerController>();
			// 此夹具不启动整个关卡，显式完成玩家控制器登记，让玩法按正常入口找到同一账户。
			World->AddController(Player);
			Economy = Player->GetEconomy();
			Badges = UCastleBadgeSubsystem::GetForWorld(World);
			Director = World->GetSubsystem<UCastleDirectorSubsystem>();
			Runtime = World->GetSubsystem<UCastleBadgeRuntimeSubsystem>();
		}

		~FGame()
		{
			Models.Reset();
			World->BeginTearingDown();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			UWorld* InstanceWorld = Instance->GetWorld();
			Instance->Shutdown();
			GEngine->DestroyWorldContext(InstanceWorld);
			InstanceWorld->DestroyWorld(false);
		}

		bool Equip(const EBadge Type)
		{
			FCastleBadgeDefinition Definition;
			FText Reason;
			bool bReplacement = false;
			const bool bAdded = FCastleBadgeDefinition::TryMakeStandard(Type, Definition)
				&& Badges->TryAcquireBadge(Definition, bReplacement, Reason) && !bReplacement;
			return Test.TestTrue(FString::Printf(TEXT("已入槽：%s %s"),
				*Definition.GetDisplayName().ToString(), *Reason.ToString()), bAdded);
		}

		bool Ground(const int32 Width = 9, const int32 Height = 5)
		{
			Board->PlacementSurfaceMode = ECastlePlacementSurfaceMode::GroundBoard;
			Board->GridSize = FIntVector(Width, Height, 5);
			FCastleGroundGenerationResult Result;
			for (int32 X = 0; X < Width; ++X)
			{
				for (int32 Y = 0; Y < Height; ++Y)
				{
					auto& Cell = Result.Cells.AddDefaulted_GetRef();
					Cell.Coordinate = Board->GridOrigin + FIntVector(X, Y, 0);
					Cell.Color = ECastleTileColor::Red;
					Cell.Number = 4;
				}
			}
			FText Reason;
			return Test.TestTrue(TEXT("准备完整测试地面"), Board->ApplyGroundGenerationResult(Result, Reason));
		}

		ACastleBuildingBase* Place(const ECastleBuildingType Type, const FIntVector Cell, const int32 Level = 1,
			const int32 Number = 4, const ECastleTileColor Color = ECastleTileColor::Red, const bool bMerge = false)
		{
			FText Reason;
			const auto* Definition = Board->FindBuildingDefinition(Type);
			FCastleBuildingDescriptor Card;
			if (!Definition || !Definition->CreateDescriptor(Level, Color, Number, Card, Reason))
			{
				Test.AddError(FString::Printf(TEXT("测试塔定义 %s 无法创建描述：%s。"), *GetPathNameSafe(Definition), *Reason.ToString()));
				return nullptr;
			}
			auto* Result = Board->PlaceBuilding(Board->GridOrigin + Cell, Card, bMerge, Reason);
			if (!CastleBuildingTests::CompletePlacement(Result))
			{
				Test.AddError(FString::Printf(TEXT("测试放置定义 %s 失败：%s。"), *Definition->GetPathName(), *Reason.ToString()));
				return nullptr;
			}
			return Result;
		}

		UCastleMainTowerComponent* MakeTower(const int32 Balloons = 5)
		{
			AActor* Owner = World->SpawnActor<AActor>();
			auto* Tower = NewObject<UCastleMainTowerComponent>(Owner);
			Tower->MaxBalloons = Balloons;
			check(Tower->InitializeHealth());
			check(World->GetSubsystem<UCastleMainTowerSubsystem>()->RegisterMainTower(Tower));
			return Tower;
		}

		bool LoadSupportDefinition(const TCHAR* Name)
		{
			auto* Definition = LoadObject<UCastleBuildingDefinition>(nullptr, *FString::Printf(
				TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), Name, Name));
			if (!Test.TestNotNull(Name, Definition)) { return false; }
			Board->BuildingDefinitions.Add(Definition);
			return true;
		}

		FMassEntityManager& Manager() const { return World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager(); }
		UCastleMassUnitSubsystem& Units() const { return *World->GetSubsystem<UCastleMassUnitSubsystem>(); }

		/** 真实 Mass 模板提供完整 Frag，测试只显式选择生命、速度与位置输入。 */
		template<typename TTrait>
		UMassEntityConfigAsset* MakeUnitConfig(const float Health = 100.0f)
		{
			auto* Config = NewObject<UMassEntityConfigAsset>(World);
			auto* Trait = NewObject<TTrait>(Config);
			Trait->MaxHealth = Health;
			Trait->Movement.MaxSpeed = 100.0f;
			Trait->Movement.Acceleration = 0.0f;
			Config->GetMutableConfig().AddTrait(*Trait);
			return Config;
		}

		template<typename TTrait>
		bool Spawn(const TArray<FTransform>& Transforms, TArray<FCastleMassUnitHandle>& Out, const float Health = 100.0f)
		{
			FText Reason;
			const bool bSpawned = Units().SpawnUnits(MakeUnitConfig<TTrait>(Health), Transforms, Out, Reason);
			return Test.TestTrue(FString::Printf(TEXT("生成测试单位：%s"), *Reason.ToString()), bSpawned);
		}

		template<typename TProcessor>
		void Step(const float DeltaSeconds)
		{
			auto& Entities = Manager();
			Entities.FlushCommands();
			auto* Processor = NewObject<TProcessor>(World);
			Processor->CallInitialize(World, Entities.AsShared());
			UE::Mass::FProcessingContext Context(Entities, DeltaSeconds);
			UE::Mass::Executor::Run(*Processor, Context);
			Entities.FlushCommands();
		}

		FCastleMassUnitFragment& Vital(const FCastleMassUnitHandle& Unit) const
		{
			return Manager().GetFragmentDataChecked<FCastleMassUnitFragment>(Unit.Entity);
		}

		void StartWave(const int32 Wave = 1) const
		{
			World->GetSubsystem<UCastleCombatSubsystem>()->OnWaveStarted.Broadcast(Wave, 2);
		}

		/** 隔离的未开局测试世界不会启动导航或 Mass 调度。先发布新的空载体，再写入单段合成快照，
		 * 由真实巡逻、出兵和金币结算入口消费；用例不复制目标路径算法，也不修改已有共享快照。 */
		void PublishStraightPath(const double Length, const bool bLadder = false)
		{
			auto* Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
			Navigation->ClearSharedRoute();
			const auto Snapshot = Navigation->AcquireSharedPath();
			// 导航用 MakeShared 创建可变底层存储；测试只在这份新快照尚无其他使用者时设置输入。
			auto& Path = const_cast<FCastleSharedNavigationPath&>(*Snapshot);
			Path.bValid = Path.bTowerDispatch = true;
			Path.Length = Length;
			Path.Waypoints = Path.PatrolPoints = {FVector::ZeroVector, FVector(Length, 0, 0)};
			Path.WaypointDistances = Path.PatrolDistances = {0.0, Length};
			if (bLadder)
			{
				Path.LadderRanges.AddDefaulted_GetRef().EndDist = Length * 0.5;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgesFullCatalogueTest, "MovingCastle.Items.Badges.Full.CatalogueAndAggregation",
	CastleBadgesFullTests::Flags)

bool FCastleBadgesFullCatalogueTest::RunTest(const FString&)
{
	using namespace CastleBadgesFullTests;
	FGame Game(*this);
	// 每个枚举必须有可验证的真实数据、可展示文案和明确的同类叠加结果。
	constexpr int32 BadgeCount = static_cast<int32>(EBadge::LastBalloonClasp);
	TestEqual(TEXT("目录共 25 种徽章"), BadgeCount, 25);
	for (int32 Index = 1; Index <= BadgeCount; ++Index)
	{
		Game.Badges->ClearAllBadgesForTesting();
		const EBadge Type = static_cast<EBadge>(Index);
		FCastleBadgeDefinition Definition;
		FText Reason;
		if (!TestTrue(TEXT("标准徽章配置通过验证"), FCastleBadgeDefinition::TryMakeStandard(Type, Definition)
			&& Definition.Validate(Reason))) { return false; }
		const FString Label = Definition.GetDisplayName().ToString();
		TestFalse(Label + TEXT("有效果摘要"), Definition.GetEffectSummary().IsEmpty());
		for (int32 Count = 1; Count <= 2; ++Count)
		{
			if (!Game.Equip(Type)) { return false; }
			const auto Equal = [&](const double Actual, const double Expected)
			{
				TestTrue(FString::Printf(TEXT("%s ×%d，实际 %g，预期 %g"), *Label, Count, Actual, Expected),
					FMath::IsNearlyEqual(Actual, Expected, 1e-5));
			};
			switch (Type)
			{
			case EBadge::FirepowerCard: Equal(Game.Badges->GetFriendlyDamageMultiplier(), 1.0 + 0.1 * Count); break;
			case EBadge::CoinPurse: Equal(Game.Badges->GetCoinPurseTenths(), Count); break;
			case EBadge::StartingFunds: Equal(Game.Badges->GetStartingFundsGold(), 100 * Count); break;
			case EBadge::DoubleDoorPass: Equal(Game.Badges->GetDoubleDoorPassGold(), 10 * Count); break;
			case EBadge::SpareBowstring: Equal(Game.Badges->GetSpareBowstringBonus(), Count); break;
			case EBadge::BalloonRack: Equal(Game.Badges->GetMainTowerHealthMultiplier(), 1.0 + 0.2 * Count); break;
			case EBadge::ShopDiscount:
				Equal(Game.Badges->GetDiscountedShopBuyPrice(400), Count == 1 ? 340 : 289);
				Equal(Game.Badges->GetDiscountedShopBuyPrice(0), 0);
				Equal(Game.Badges->GetDiscountedShopBuyPrice(1), 1);
				break;
			case EBadge::ShopRebate: Equal(Game.Badges->GetShopRebateCards(), Count); break;
			case EBadge::ShiftWhistle: Equal(Game.Badges->GetShiftWhistleSpeedMultiplier(), 1.0 + 0.2 * Count); break;
			case EBadge::StringGlove: Equal(Game.Badges->GetStringGloveExtraCells(), Count); break;
			case EBadge::QuickstringPass: Equal(Game.Badges->GetArcherFireRateMultiplier(), 1.0 + 0.2 * Count); break;
			case EBadge::FreeRefresh:
				Game.Director->OnEditingPhaseEntered.Broadcast();
				Equal(Game.Runtime->GetRemainingFreeShopRefreshes(), 3);
				break;
			case EBadge::QuickLadder: Equal(Game.Badges->GetQuickLadderSpeedMultiplier(), 1.0 + 0.5 * Count); break;
			case EBadge::LoopPurchase:
				Game.Director->OnRunStarted.Broadcast();
				for (int32 Refresh = 0; Refresh < 9; ++Refresh)
				{
					TestFalse(TEXT("未到第十次刷新不锁定数字"), Game.Runtime->IsMatchingShopRefreshDue());
					Game.Runtime->RecordShopRefresh(false);
				}
				TestTrue(TEXT("第十次刷新锁定数字"), Game.Runtime->IsMatchingShopRefreshDue());
				break;
			case EBadge::LateAdvantage:
				Equal(Game.Badges->CalculateLateAdvantageSpeedMultiplier(0), 0.5);
				Equal(Game.Badges->CalculateLateAdvantageSpeedMultiplier(1), 1.2);
				Equal(Game.Badges->CalculateLateAdvantageSpeedMultiplier(2), 1.4);
				break;
			case EBadge::SiphonArrow: Equal(Game.Badges->GetSiphonArrowFraction(), 0.1 * Count); break;
			case EBadge::BlueAbacus: Equal(Game.Badges->GetBlueRoomNumberBonus(), Count); break;
			case EBadge::WorkshopRefund: Equal(Game.Badges->GetUpgradeBonusRoomCards(), 2 * Count); break;
			case EBadge::EmergencyBallast:
				Equal(Game.Badges->GetEnemyAttackSpeedMultiplier(0.20f), 0.5);
				Equal(Game.Badges->GetEnemyAttackSpeedMultiplier(0.2001f), 1.0);
				break;
			case EBadge::RapidMarch: Equal(Game.Badges->GetRapidMarchSpeedMultiplier(5), 1.0 + Count); break;
			case EBadge::BerserkBee: Equal(Game.Badges->GetBerserkBeeGold(), 5 * Count); break;
			case EBadge::BurstAlarm:
				Equal(Game.Badges->GetGlobalTowerFireRateMultiplier(false), 1.0);
				Equal(Game.Badges->GetGlobalTowerFireRateMultiplier(true), 2.0);
				Equal(Game.Badges->GetSummonSpeedMultiplier(true), 2.0);
				break;
			case EBadge::ChainReaction:
			case EBadge::LapReassignment:
			case EBadge::LastBalloonClasp:
				TestTrue(Label + TEXT("以持有为开关，运行时验证行为"), Game.Badges->HasBadge(Type));
				break;
			default: AddError(Label + TEXT("缺少叠加断言")); return false;
			}
		}
	}
	FCastleBadgeDefinition Wrong;
	FText Reason;
	FCastleBadgeDefinition::TryMakeStandard(EBadge::ShopDiscount, Wrong);
	Wrong.Magnitude = 0.86f;
	TestFalse(TEXT("拒绝偏离精确定价公式的配置"), Wrong.Validate(Reason));
	FCastleBadgeDefinition::TryMakeStandard(EBadge::ShopRebate, Wrong);
	Wrong.Magnitude = 1.5f;
	TestFalse(TEXT("拒绝非整数返利张数"), Wrong.Validate(Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgesFullEconomyTest, "MovingCastle.Items.Badges.Full.GameplayGoldAndRun",
	CastleBadgesFullTests::Flags)

bool FCastleBadgesFullEconomyTest::RunTest(const FString&)
{
	using namespace CastleBadgesFullTests;
	FGame Game(*this);
	FText Reason;
	int32 Final = -1;
	TestTrue(TEXT("零徽章时按原玩法金额发放"), Game.Economy->TryAddGameplayGold(10, Reason, &Final));
	TestEqual(TEXT("无加成发十金"), Final, 10);
	for (int32 Count = 1; Count <= 2; ++Count)
	{
		if (!Game.Equip(EBadge::CoinPurse) || !Game.Equip(EBadge::StartingFunds)) { return false; }
		const int32 BeforeAdd = Game.Economy->GetGold();
		TestTrue(TEXT("玩法收入一金仍至少加一"), Game.Economy->TryAddGameplayGold(1, Reason, &Final));
		TestEqual(TEXT("最小玩法增益"), Final, 2);
		TestTrue(TEXT("正好整除不多向上取整"), Game.Economy->TryAddGameplayGold(10, Reason, &Final));
		TestEqual(TEXT("精确的整数边界"), Final, 10 + Count);
		TestTrue(TEXT("零奖金额合法"), Game.Economy->TryAddGameplayGold(0, Reason, &Final));
		TestEqual(TEXT("零金币不凭空加一"), Final, 0);
		TestEqual(TEXT("获取启动资金徽章当轮不会补发"), Game.Economy->GetGold(), BeforeAdd + 12 + Count);
		const int32 BeforeInitial = Game.Economy->GetGold();
		TestTrue(TEXT("初始、退款和调试共用的普通金币入口不加成"), Game.Economy->TryAddGold(17, Reason));
		TestEqual(TEXT("普通收入保持原金额"), Game.Economy->GetGold(), BeforeInitial + 17);
		Game.Director->OnRunStarted.Broadcast();
		TestEqual(TEXT("新开局发放线性启动资金，零钱袋不参与"), Game.Economy->GetGold(), BeforeInitial + 17 + 100 * Count);
		Game.Director->OnEditingPhaseEntered.Broadcast();
		TestEqual(TEXT("再次进入编辑不重发初始奖励"), Game.Economy->GetGold(), BeforeInitial + 17 + 100 * Count);
	}
	const int32 BeforeOverflow = Game.Economy->GetGold();
	AddExpectedError(TEXT("玩法奖励超过金币整数上限"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("加成后溢出拒绝入账"), Game.Economy->TryAddGameplayGold(MAX_int32, Reason, &Final));
	TestEqual(TEXT("拒绝保留账户"), Game.Economy->GetGold(), BeforeOverflow);
	TestEqual(TEXT("拒绝不报告发放金额"), Final, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgesFullShopTest, "MovingCastle.Items.Badges.Full.OrdinaryShop",
	CastleBadgesFullTests::Flags)

bool FCastleBadgesFullShopTest::RunTest(const FString&)
{
	using namespace CastleBadgesFullTests;
	FGame Game(*this);
	FText Reason;
	auto* Shop = Game.Player->GetShop();
	auto* Backpack = Game.Board->PrototypeBackpack.Get();
	if (!TestTrue(TEXT("开启普通商店"), Game.Economy->TryAddGold(10000, Reason)
		&& Shop->InitializeShop(Game.Economy, Game.Board, Reason) && Shop->TrySetOfferNumberRange(1, 3, Reason)
		&& Shop->TryOpenShop(Reason))) { return false; }
	const int32 Original = Shop->GetOffers()[0].Price;
	if (!Game.Equip(EBadge::ShopDiscount) || !Game.Equip(EBadge::ShopDiscount)
		|| !Game.Equip(EBadge::CoinPurse)) { return false; }
	const int32 Discounted = FMath::Max(1, Original * 289 / 400);
	TestEqual(TEXT("已有货架的显示价立即按两枚折扣计算"), Shop->GetOffers()[0].Price, Discounted);
	int32 Before = Game.Economy->GetGold();
	TestTrue(TEXT("按显示价购买"), Shop->TryBuyOffer(0, Reason));
	TestEqual(TEXT("实际扣费与显示相同"), Game.Economy->GetGold(), Before - Discounted);
	int32 SellPrice = 0;
	TestTrue(TEXT("卖牌走原定价策略"), Shop->TryGetSellPrice(0, SellPrice, Reason));
	TestEqual(TEXT("折扣不改变卖价"), SellPrice, Original / 2);
	Before = Game.Economy->GetGold();
	TestTrue(TEXT("卖出卡牌"), Shop->TrySellBackpackItem(0, Reason));
	TestEqual(TEXT("卖牌收益不应用零钱袋"), Game.Economy->GetGold(), Before + SellPrice);
	Before = Game.Economy->GetGold();
	TestTrue(TEXT("普通付费刷新"), Shop->TryRefreshOffers(Reason));
	TestEqual(TEXT("折扣不作用于刷新费"), Game.Economy->GetGold(), Before - Shop->RefreshCost);

	Game.Badges->ClearAllBadgesForTesting();
	for (int32 Count = 1; Count <= 2; ++Count)
	{
		if (!Game.Equip(EBadge::FreeRefresh) || !Game.Equip(EBadge::LoopPurchase) || !Game.Equip(EBadge::ShopRebate)) { return false; }
	}
	Game.Director->OnRunStarted.Broadcast();
	Game.Director->OnEditingPhaseEntered.Broadcast();
	int32 Slot = INDEX_NONE;
	const auto* Definition = Game.Board->BuildingDefinitions[0].Get();
	FCastleBuildingDescriptor Card;
	if (!TestTrue(TEXT("第一张卡用超出商店等级范围的数字 9"), Definition->CreateDescriptor(1, ECastleTileColor::Red, 9, Card, Reason)
		&& Backpack->TryAddItem(Card, Slot, Reason))) { return false; }
	const auto Definitions = Game.Board->BuildingDefinitions;
	Game.Board->BuildingDefinitions.Reset();
	Before = Game.Economy->GetGold();
	AddExpectedError(TEXT("换批失败"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("坏配置取消刷新"), Shop->TryRefreshOffers(Reason));
	TestEqual(TEXT("坏配置不消费免费配额"), Game.Runtime->GetRemainingFreeShopRefreshes(), 3);
	TestEqual(TEXT("坏配置不扣钱"), Game.Economy->GetGold(), Before);
	Game.Board->BuildingDefinitions = Definitions;
	for (int32 Refresh = 1; Refresh <= 9; ++Refresh)
	{
		Before = Game.Economy->GetGold();
		if (!TestTrue(TEXT("成功的手动刷新"), Shop->TryRefreshOffers(Reason))) { return false; }
		TestEqual(TEXT("前三次免费，之后原价"), Game.Economy->GetGold(), Before - (Refresh <= 3 ? 0 : Shop->RefreshCost));
	}
	Shop->CloseShop();
	TestTrue(TEXT("奖励自动开店仍成功"), Shop->TryOpenForReward(Game.Economy, Game.Board, Reason));
	TestEqual(TEXT("重复开店和奖励不恢复免费配额"), Game.Runtime->GetRemainingFreeShopRefreshes(), 0);
	TestTrue(TEXT("奖励自动开店不推进采购签次数"), Game.Runtime->IsMatchingShopRefreshDue());
	TestTrue(TEXT("第十次手动刷新"), Shop->TryRefreshOffers(Reason));
	TestEqual(TEXT("仅首格锁定数字 9"), Shop->GetOffers()[0].Descriptor.Number, 9);
	for (int32 Index = 1; Index < Shop->GetOffers().Num(); ++Index)
	{
		TestTrue(TEXT("其余货位依旧按普通等级范围"), Shop->GetOffers()[Index].Descriptor.Number <= 3);
	}
	TestTrue(TEXT("清空建筑栏"), Backpack->TryRemoveItemAt(0, Card, Reason));
	Game.Director->OnEditingPhaseEntered.Broadcast();
	TestEqual(TEXT("重新进入编辑阶段补满三次"), Game.Runtime->GetRemainingFreeShopRefreshes(), 3);
	for (int32 Refresh = 1; Refresh <= 10; ++Refresh)
	{
		if (Refresh == 10) { AddExpectedMessagePlain(TEXT("主建筑栏为空，本次首格"), ELogVerbosity::Warning); }
		TestTrue(TEXT("空栏也可以正常刷新"), Shop->TryRefreshOffers(Reason));
	}
	TestTrue(TEXT("空栏首格仍在正常范围"), Shop->GetOffers()[0].Descriptor.Number <= 3);

	TestTrue(TEXT("缩小主栏到一张以观察返利溢出"), Backpack->TrySetCapacity(1, Reason));
	for (int32 Purchase = 1; Purchase <= 9; ++Purchase)
	{
		if (!TestTrue(TEXT("完成本周期普通购买"), Shop->TryBuyOffer(0, Reason))) { return false; }
		TestEqual(TEXT("前九次不发额外卡"), Backpack->GetTempItemCount(), 0);
		TestFalse(TEXT("尝试重复购买同格不改变计数"), Shop->TryBuyOffer(0, Reason));
		if (!Backpack->TryRemoveItemAt(0, Card, Reason) || !Shop->TryRefreshOffers(Reason)) { return false; }
	}
	Game.Board->BuildingDefinitions.Reset();
	Before = Game.Economy->GetGold();
	AddExpectedError(TEXT("无法准备返利"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("第十次返利配置坏时撤销整笔交易"), Shop->TryBuyOffer(0, Reason));
	TestEqual(TEXT("失败不改变金币"), Game.Economy->GetGold(), Before);
	TestEqual(TEXT("失败不偷偷加入商品"), Backpack->GetItemCount(), 0);
	TestFalse(TEXT("失败货位保持可购买"), Shop->GetOffers()[0].bSold);
	Game.Board->BuildingDefinitions = Definitions;
	TestTrue(TEXT("修复同一笔可重试"), Shop->TryBuyOffer(0, Reason));
	TestEqual(TEXT("购买卡进入主栏"), Backpack->GetItemCount(), 1);
	TestEqual(TEXT("两枚返利共发两张临时栏卡"), Backpack->GetTempItemCount(), 2);
	for (const auto& Reward : Backpack->GetTempItems())
	{
		TestTrue(TEXT("返利是独立的标准四色 1–6 一级卡"), Reward.Level == 1 && Reward.Number >= 1
			&& Reward.Number <= 6 && CastleTileColors::IsPlayable(Reward.Color) && Definitions.Contains(Reward.Definition));
	}

	// 纯数据商店可以脱离游戏世界做配置校验和交易，不能误用上述玩家徽章或取消已有购买能力。
	auto* OfflineBoard = NewObject<ACastleBoard>();
	OfflineBoard->BuildingDefinitions = Definitions;
	auto* OfflineWallet = NewObject<UCastleEconomyComponent>();
	auto* OfflineShop = NewObject<UCastleShopComponent>();
	if (!TestTrue(TEXT("独立商店可按原价初始化、打开并刷新"), OfflineWallet->TryAddGold(100, Reason)
		&& OfflineShop->InitializeShop(OfflineWallet, OfflineBoard, Reason) && OfflineShop->TryOpenShop(Reason)
		&& OfflineShop->TryRefreshOffers(Reason))) { return false; }
	const int32 OfflinePrice = OfflineShop->GetOffers()[0].Price;
	TestTrue(TEXT("独立商店仍可正常购买"), OfflineShop->TryBuyOffer(0, Reason));
	TestEqual(TEXT("独立商店的实际刷新和购买均正常扣费"), OfflineWallet->GetGold(), 100 - OfflineShop->RefreshCost - OfflinePrice);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgesFullBuildingEntryTest, "MovingCastle.Items.Badges.Full.BuildingEntryAndChain",
	CastleBadgesFullTests::Flags)

bool FCastleBadgesFullBuildingEntryTest::RunTest(const FString&)
{
	using namespace CastleBadgesFullTests;
	FGame Game(*this);
	if (!Game.Ground()) { return false; }
	for (int32 Count = 0; Count < 2; ++Count)
	{
		if (!Game.Equip(EBadge::SpareBowstring) || !Game.Equip(EBadge::ShiftWhistle) || !Game.Equip(EBadge::ChainReaction)) { return false; }
	}
	Game.Director->OnRunStarted.Broadcast();
	auto* Source = Game.Place(ECastleBuildingType::Farm, {0, 0, 0});
	auto* Peer = Game.Place(ECastleBuildingType::Farm, {2, 0, 0}, 2);
	auto* Room = Game.Place(ECastleBuildingType::NormalRoom, {4, 0, 0});
	auto* OtherNumber = Game.Place(ECastleBuildingType::Farm, {5, 0, 0}, 1, 5);
	auto* OtherColor = Game.Place(ECastleBuildingType::Farm, {6, 0, 0}, 1, 4, ECastleTileColor::Blue);
	auto* OtherType = Game.Place(ECastleBuildingType::ArcherTower, {7, 0, 0});
	if (!Source || !Peer || !Room || !OtherNumber || !OtherColor || !OtherType) { return false; }
	TArray<FCastleMassUnitHandle> NPCs;
	TArray<FTransform> Starts;
	Starts.Init(FTransform::Identity, 4);
	if (!Game.Spawn<UCastleMassNPCTrait>(Starts, NPCs)) { return false; }
	Game.StartWave();
	FCastleBuildingTriggerContext Entry;
	Entry.Source = ECastleBuildingTriggerSource::NPCEntry;
	Entry.NPC = NPCs[0];
	TestTrue(TEXT("普通房间实际经过，但不会消耗功能塔首进"), Room->TriggerBuildingEffect(Entry));
	const int32 InitialGold = Game.Economy->GetGold();
	if (!TestTrue(TEXT("正式建筑入口接受首名 NPC"), Source->TriggerBuildingEffect(Entry))) { return false; }
	TestEqual(TEXT("本次本塔加二，连锁塔以正常数字结算"), Game.Economy->GetGold() - InitialGold, 10);
	TestEqual(TEXT("首进数字只放在当次上下文"), Source->BuildingEffect->LastTriggerContext.TriggerNumberBonus, int64{2});
	TestEqual(TEXT("卡面原样保留"), Source->GetBuildingNumber(), 4);
	TestEqual(TEXT("来源只触发一次"), Source->BuildingEffect->TriggerCount, int64{1});
	TestEqual(TEXT("不同等级同组塔只触发一次"), Peer->BuildingEffect->TriggerCount, int64{1});
	TestTrue(TEXT("同组塔确为子触发"), Peer->BuildingEffect->LastTriggerContext.bChainReactionChild);
	TestEqual(TEXT("异数字不连锁"), OtherNumber->BuildingEffect->TriggerCount, int64{0});
	TestEqual(TEXT("异色不连锁"), OtherColor->BuildingEffect->TriggerCount, int64{0});
	TestEqual(TEXT("异类型不连锁"), OtherType->BuildingEffect->TriggerCount, int64{0});
	if (!TestTrue(TEXT("同士兵可再次触发塔本身"), Source->TriggerBuildingEffect(Entry))) { return false; }
	TestEqual(TEXT("同波第二次数字恢复"), Source->BuildingEffect->LastTriggerContext.TriggerNumberBonus, int64{0});
	for (int32 Index = 1; Index < NPCs.Num(); ++Index)
	{
		Entry.NPC = NPCs[Index];
		if (!TestTrue(TEXT("本波独立人员进塔"), Source->TriggerBuildingEffect(Entry))) { return false; }
	}
	const double Now = Game.World->GetTimeSeconds();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const auto* Movement = Game.Runtime->FindNPCMovement(NPCs[Index].Entity);
		if (!TestNotNull(TEXT("前三个不同的 NPC 获得汽笛"), Movement)) { return false; }
		TestTrue(TEXT("两枚汽笛加 40%"), FMath::IsNearlyEqual(Movement->At(Now + 3.99), 1.4f));
		TestEqual(TEXT("汽笛恰好四秒到期"), Movement->At(Now + 4.0), 1.0f);
	}
	TestNull(TEXT("第四人不获得固定三人名额"), Game.Runtime->FindNPCMovement(NPCs[3].Entity));
	Game.StartWave(2);
	TestTrue(TEXT("下一波第四人可以成为首进"), OtherNumber->TriggerBuildingEffect(Entry));
	TestEqual(TEXT("第二波重置全局首进数字"), OtherNumber->BuildingEffect->LastTriggerContext.TriggerNumberBonus, int64{2});
	TestNotNull(TEXT("下一波重置汽笛人数"), Game.Runtime->FindNPCMovement(NPCs[3].Entity));

	// 在不同等级上实际触发，验证较小倍率只刷新较大的已生效行军，两个移速来源各自过期。
	Game.Badges->ClearAllBadgesForTesting();
	if (!Game.Equip(EBadge::RapidMarch) || !Game.Equip(EBadge::RapidMarch)
		|| !Game.Equip(EBadge::ShiftWhistle)) { return false; }
	Game.StartWave();
	TestTrue(TEXT("先进入二级塔"), Peer->TriggerBuildingEffect(Entry));
	TestTrue(TEXT("再进入一级塔"), Source->TriggerBuildingEffect(Entry));
	const auto* Combined = Game.Runtime->FindNPCMovement(NPCs[3].Entity);
	if (!TestNotNull(TEXT("持有两组独立限时效果"), Combined)) { return false; }
	TestTrue(TEXT("行军按两枚二级塔的 1.8 计，并与汽笛 1.2 相乘"), FMath::IsNearlyEqual(Combined->At(Now + 2.99), 2.16f));
	TestTrue(TEXT("三秒后只保留汽笛"), FMath::IsNearlyEqual(Combined->At(Now + 3.0), 1.2f));
	Game.Director->OnDirectorPhaseChanged.Broadcast(ECastleDirectorPhase::Reward);
	TestNull(TEXT("离开战斗不携带上一场限时移速"), Game.Runtime->FindNPCMovement(NPCs[3].Entity));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgesFullTowerEffectsTest, "MovingCastle.Items.Badges.Full.TowerHealthRateAndUpgrade",
	CastleBadgesFullTests::Flags)

bool FCastleBadgesFullTowerEffectsTest::RunTest(const FString&)
{
	using namespace CastleBadgesFullTests;
	FGame Game(*this);
	if (!Game.Ground()) { return false; }
	UCastleMainTowerComponent* Tower = Game.MakeTower();
	Game.Director->ConfigureForAutomation(NewObject<UCastleDirectorConfigDataAsset>(Game.World), Game.Board, Game.Player, Tower);
	Game.Director->OnRunStarted.Broadcast();
	FText Reason;
	for (int32 Count = 1; Count <= 2; ++Count)
	{
		if (!Game.Equip(EBadge::BalloonRack)) { return false; }
		TestTrue(TEXT("由组件应用绝对生命倍率"), Tower->SetMaxHealthMultiplier(Game.Badges->GetMainTowerHealthMultiplier()));
		TestEqual(TEXT("备用气球挂架按基础值增加生命上限"), Tower->MaxHealth, static_cast<float>(1000 + Count * 200));
		TestEqual(TEXT("满血更新剩余生命"), Tower->CurrentHealth, Tower->MaxHealth);
	}
	for (int32 Count = 0; Count < 2; ++Count)
	{
		if (!Game.Equip(EBadge::BlueAbacus) || !Game.Equip(EBadge::WorkshopRefund)) { return false; }
	}
	auto* Blue = Game.Place(ECastleBuildingType::ArcherTower, {0, 0, 0}, 1, 4, ECastleTileColor::Blue);
	auto* Red = Game.Place(ECastleBuildingType::CannonTower, {2, 0, 0});
	if (!Blue || !Red) { return false; }
	TestEqual(TEXT("蓝房有效数字 +2"), Blue->GetEffectiveBuildingNumber(), int64{6});
	TestEqual(TEXT("非蓝色使用原数字"), Red->GetEffectiveBuildingNumber(), int64{4});
	auto* Backpack = Game.Board->PrototypeBackpack.Get();
	TestTrue(TEXT("把主栏余量压到一格"), Backpack->TrySetCapacity(1, Reason));
	if (!Game.Place(ECastleBuildingType::Farm, {5, 0, 0}, 1, 4, ECastleTileColor::Red, true)
		|| !Game.Place(ECastleBuildingType::Farm, {6, 0, 0}, 1, 4, ECastleTileColor::Red, true)) { return false; }
	TestNotNull(TEXT("成功形成二级塔"), CastleBuildingTests::FindPlacedLevel(Game.Board, 2));
	TestEqual(TEXT("工坊返料的第一张进主栏"), Backpack->GetItemCount(), 1);
	TestEqual(TEXT("工坊返料其余三张进入临时栏"), Backpack->GetTempItemCount(), 3);

	Game.Badges->ClearAllBadgesForTesting();
	for (int32 Count = 0; Count < 2; ++Count)
	{
		if (!Game.Equip(EBadge::QuickstringPass) || !Game.Equip(EBadge::BurstAlarm) || !Game.Equip(EBadge::EmergencyBallast)) { return false; }
	}
	auto* Support = Game.World->GetSubsystem<UCastleSupportTowerSubsystem>();
	if (!TestTrue(TEXT("切入战斗阶段"), Game.Director->SetPhaseForAutomation(ECastleDirectorPhase::Battle))) { return false; }
	TestTrue(TEXT("零伤攻击可结算"), Tower->ApplyDamage(0.0f));
	TestTrue(TEXT("未掉血时两枚工牌只提高箭塔 40%"), FMath::IsNearlyEqual(Support->GetTowerFireRateMultiplier(*Blue), 1.4f));
	TestEqual(TEXT("掉血前炮塔保持 1 倍"), Support->GetTowerFireRateMultiplier(*Red), 1.0f);
	TestTrue(TEXT("主塔真正掉血"), Tower->ApplyDamage(Tower->CurrentHealth - Tower->MaxHealth * 0.2f));
	TestTrue(TEXT("工牌与一次警钟相乘为 2.8 倍"), FMath::IsNearlyEqual(Support->GetTowerFireRateMultiplier(*Blue), 2.8f));
	TestEqual(TEXT("两枚警钟仍只翻倍一次"), Support->GetTowerFireRateMultiplier(*Red), 2.0f);
	TestEqual(TEXT("压舱扣在正好 20% 时敌方攻速减半"), Game.Units().GetGlobalEnemyAttackSpeedMultiplier(), 0.5f);
	TestTrue(TEXT("回血越过阈值"), Tower->ApplyHealing(1.0f));
	TestEqual(TEXT("超过 20% 恢复敌方攻速"), Game.Units().GetGlobalEnemyAttackSpeedMultiplier(), 1.0f);
	TestEqual(TEXT("回血不结束警钟"), Support->GetTowerFireRateMultiplier(*Red), 2.0f);
	TestTrue(TEXT("回到编辑阶段"), Game.Director->SetPhaseForAutomation(ECastleDirectorPhase::Editing));
	TestEqual(TEXT("战斗结束时警钟停止"), Support->GetTowerFireRateMultiplier(*Red), 1.0f);
	TestTrue(TEXT("再次开战"), Game.Director->SetPhaseForAutomation(ECastleDirectorPhase::Battle));
	TestEqual(TEXT("下一场仍等待真实掉血"), Support->GetTowerFireRateMultiplier(*Red), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgesFullShieldTest, "MovingCastle.Items.Badges.Full.LastBalloonAndShield",
	CastleBadgesFullTests::Flags)

bool FCastleBadgesFullShieldTest::RunTest(const FString&)
{
	using namespace CastleBadgesFullTests;
	FGame Game(*this);
	if (!Game.Equip(EBadge::LastBalloonClasp) || !Game.Equip(EBadge::LastBalloonClasp) || !Game.Equip(EBadge::FirepowerCard)) { return false; }
	Game.Director->OnRunStarted.Broadcast();
	auto* Tower = Game.MakeTower(3);
	auto* Shields = Game.World->GetSubsystem<UCastleShieldTowerSubsystem>();
	TestTrue(TEXT("无展开盾时敌伤仍能致死"), Tower->ApplyEnemyDamage(1000.0f));
	TestFalse(TEXT("无盾没有保命"), Tower->IsAlive());
	TestTrue(TEXT("恢复测试用主塔"), Tower->InitializeHealth());
	AActor* Weak = Game.World->SpawnActor<AActor>();
	AActor* Strong = Game.World->SpawnActor<AActor>();
	AActor* Tie = Game.World->SpawnActor<AActor>();
	const uint64 WeakId = Shields->UpsertActiveShield(Weak, FVector::ZeroVector, 100, 4, 10);
	Shields->UpsertActiveShield(Strong, FVector::ZeroVector, 100, 9, 0);
	Shields->UpsertActiveShield(Tie, FVector::ZeroVector, 100, 9, 0);
	int64 Blocks = 0;
	TArray<FCastleMassUnitHandle> Enemies;
	if (!Game.Spawn<UCastleMassEnemyTrait>({FTransform::Identity}, Enemies)) { return false; }
	TestTrue(TEXT("普通挡击反伤受火力牌加成"), Shields->ApplyAttackToShield(WeakId, Enemies[0], Blocks));
	TestEqual(TEXT("10 点盾反变 11 点"), Game.Vital(Enemies[0]).Health, 89.0f);
	TestTrue(TEXT("非敌伤的致命扣血不触发"), Tower->ApplyDamage(1000.0f));
	TestTrue(TEXT("最强盾保持激活"), Shields->GetShieldRemainingBlocks(Strong, Blocks) && Blocks == 9);
	TestTrue(TEXT("恢复满血验证敌伤"), Tower->InitializeHealth());
	TArray<UObject*> Broken;
	const auto Listener = Shields->OnShieldStateChanged.AddLambda([&Broken](UObject* Owner, int64 Remaining)
	{
		if (Remaining == 0) { Broken.Add(Owner); }
	});
	TestTrue(TEXT("敌伤恰好致命时启动气球扣"), Tower->ApplyEnemyDamage(1000.0f));
	TestTrue(TEXT("只耗尽较早登记的并列最强盾，广播零次"), Broken.Num() == 1 && Broken[0] == Strong);
	TestTrue(TEXT("较弱护盾保留"), Shields->GetShieldRemainingBlocks(Weak, Blocks) && Blocks == 3);
	TestTrue(TEXT("并列未选护盾保留"), Shields->GetShieldRemainingBlocks(Tie, Blocks) && Blocks == 9);
	int32 Live = 0, Capacity = 0;
	Tower->GetBalloonCounts(Live, Capacity);
	TestEqual(TEXT("1000/3 边界只保留一颗气球"), Live, 1);
	TestTrue(TEXT("最大可保留量为一颗份额"), FMath::IsNearlyEqual(Tower->CurrentHealth, 1000.0f / 3.0f, 0.001f));
	TestTrue(TEXT("同局第二次致命攻击结算"), Tower->ApplyEnemyDamage(1000.0f));
	TestFalse(TEXT("两枚气球扣同局依旧只允许一次"), Tower->IsAlive());
	TestEqual(TEXT("第二次没有破盾"), Broken.Num(), 1);
	Game.Director->OnRunStarted.Broadcast();
	TestTrue(TEXT("新局恢复初始生命"), Tower->InitializeHealth());
	TestTrue(TEXT("非致命敌伤不消耗气球扣"), Tower->ApplyEnemyDamage(950.0f));
	TestTrue(TEXT("新局再次允许一次"), Tower->ApplyEnemyDamage(500.0f));
	TestEqual(TEXT("低于一颗阈值时保留受击前生命，不凭空回血"), Tower->CurrentHealth, 50.0f);
	TestEqual(TEXT("新局耗尽另一面最强盾"), Broken.Num(), 2);
	Shields->OnShieldStateChanged.Remove(Listener);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgesFullProjectilesTest, "MovingCastle.Items.Badges.Full.ProvenancedArrowHits",
	CastleBadgesFullTests::Flags)

bool FCastleBadgesFullProjectilesTest::RunTest(const FString&)
{
	using namespace CastleBadgesFullTests;
	FGame Game(*this);
	if (!Game.Ground()) { return false; }
	for (int32 Count = 0; Count < 2; ++Count)
	{
		if (!Game.Equip(EBadge::FirepowerCard) || !Game.Equip(EBadge::SiphonArrow)) { return false; }
	}
	auto* Tower = Game.MakeTower();
	TestTrue(TEXT("先扣除主塔生命以观察吸血"), Tower->ApplyDamage(900.0f));
	auto* Archer = Game.Place(ECastleBuildingType::ArcherTower, {0, 0, 0}, 1, 1);
	auto* Cannon = Game.Place(ECastleBuildingType::CannonTower, {1, 0, 0}, 1, 1);
	if (!Archer || !Cannon) { return false; }
	const UCastleTowerAttackDataAsset* Data = Archer->GetBuildingDefinition()->AttackData;
	const FVector Origin = Archer->GetActorTransform().TransformPosition(FVector(0, 0, Data->LaunchHeightOffset));
	TArray<FCastleMassUnitHandle> Enemies;
	if (!Game.Spawn<UCastleMassEnemyTrait>({FTransform(Origin + FVector(30, 0, 0))}, Enemies)) { return false; }
	auto* ArcherEffect = CastChecked<UCastleTowerAttackEffectComponent>(Archer->BuildingEffect);
	auto* CannonEffect = CastChecked<UCastleTowerAttackEffectComponent>(Cannon->BuildingEffect);
	const auto ShootAndStep = [&](ACastleBuildingBase& Building, UCastleTowerAttackEffectComponent& Effect)
	{
		Effect.ClearPendingShots();
		if (!TestTrue(TEXT("触发原生攻击塔"), Building.TriggerBuildingEffect({}))) { return; }
		Effect.TickComponent(0.0f, LEVELTICK_All, nullptr);
		Game.Step<UCastleMassHomingProjectileProcessor>(10.0f);
	};
	ShootAndStep(*Archer, *ArcherEffect);
	TestTrue(TEXT("两枚火力牌把箭伤从 10 提高至 12"), FMath::IsNearlyEqual(Game.Vital(Enemies[0]).Health, 88.0f));
	TestTrue(TEXT("两枚虹吸用有效的 12 伤害回复 2.4"), FMath::IsNearlyEqual(Tower->CurrentHealth, 102.4f));
	ShootAndStep(*Cannon, *CannonEffect);
	TestTrue(TEXT("同样的火力加成也作用于炮伤"), FMath::IsNearlyEqual(Game.Vital(Enemies[0]).Health, 76.0f));
	TestTrue(TEXT("炮塔追踪炮弹没有虹吸"), FMath::IsNearlyEqual(Tower->CurrentHealth, 102.4f));

	// Lv3 空投联动从通用生成入口发射，独立于箭塔组件的箭矢来源标志。
	FText Reason;
	FCastleMassHomingProjectileSpawn Indirect;
	Indirect.Origin = Origin;
	Indirect.Target = Enemies[0];
	Indirect.Damage = 7.0f;
	TArray<FCastleMassUnitHandle> Shots;
	TestTrue(TEXT("生成无原生箭塔身份的联动弹"), Game.Units().SpawnHomingProjectiles(Data->ProjectileConfiguration, {Indirect}, Shots, Reason));
	Game.Step<UCastleMassHomingProjectileProcessor>(1.0f);
	TestTrue(TEXT("联动弹可命中但不会吸血"), FMath::IsNearlyEqual(Tower->CurrentHealth, 102.4f));

	TestTrue(TEXT("准备接近满血"), Tower->ApplyHealing(10000.0f) && Tower->ApplyDamage(0.5f));
	Game.Vital(Enemies[0]).Health = 1.0f;
	ShootAndStep(*Archer, *ArcherEffect);
	TestEqual(TEXT("过量伤害仍是有效命中，回血封顶"), Tower->CurrentHealth, Tower->MaxHealth);
	TestTrue(TEXT("击杀目标确实归零"), Game.Vital(Enemies[0]).Health == 0.0f);
	TestTrue(TEXT("主塔可进入死亡状态"), Tower->ApplyDamage(Tower->CurrentHealth));
	if (!Game.Spawn<UCastleMassEnemyTrait>({FTransform(Origin + FVector(60, 0, 0))}, Enemies)) { return false; }
	ShootAndStep(*Archer, *ArcherEffect);
	TestFalse(TEXT("虹吸不复活主塔"), Tower->IsAlive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgesFullGloveTest, "MovingCastle.Items.Badges.Full.ManualBalloonImpact",
	CastleBadgesFullTests::Flags)

bool FCastleBadgesFullGloveTest::RunTest(const FString&)
{
	using namespace CastleBadgesFullTests;
	FGame Game(*this);
	if (!Game.Ground() || !Game.LoadSupportDefinition(TEXT("BalloonTower")) || !Game.Equip(EBadge::FirepowerCard)) { return false; }
	auto* BalloonTower = Game.Place(ECastleBuildingType::BalloonTower, {0, 0, 0});
	if (!BalloonTower) { return false; }
	auto* Support = Game.World->GetSubsystem<UCastleSupportTowerSubsystem>();
	const auto* SupportData = BalloonTower->GetBuildingDefinition()->SupportData.Get();
	const FVector Site = Game.Board->GetGridWorldLocation(Game.Board->GridOrigin);
	const double Radius = SupportData->LandingRadius;
	const double Cell = Game.Board->CellSize;
	TArray<FCastleMassUnitHandle> Enemies;
	if (!Game.Spawn<UCastleMassEnemyTrait>({FTransform(Site + FVector(0, 0, 150)),
		FTransform(Site + FVector(Radius + Cell * 0.5, 0, 150)), FTransform(Site + FVector(Radius + Cell * 1.5, 0, 150))}, Enemies))
	{ return false; }
	const auto Pop = [&](const bool bManual)
	{
		if (!BalloonTower->TriggerBuildingEffect({})) { return false; }
		Support->Tick(0.01f);
		uint64 Id = 0;
		FVector Position;
		if (!Support->FindBalloonTarget(BalloonTower->GetActorLocation() + FVector(0, 0, 800), Id, Position)) { return false; }
		const bool bPopped = bManual ? Support->PopBalloon(Id) : Support->DamageBalloon(Id, SupportData->BalloonHealth);
		if (!bPopped) { return false; }
		Support->Tick(3.0f);
		return true;
	};
	if (!Game.Equip(EBadge::StringGlove) || !TestTrue(TEXT("第一枚手套手动点破"), Pop(true))) { return false; }
	TestTrue(TEXT("自身落地伤害也受火力牌影响"), FMath::IsNearlyEqual(Game.Vital(Enemies[0]).Health, 95.6f));
	TestTrue(TEXT("一格内额外敌人同时受到伤害与减速"), FMath::IsNearlyEqual(Game.Vital(Enemies[1]).Health, 95.6f)
		&& FMath::IsNearlyEqual(Game.Vital(Enemies[1]).SupportMovementMultiplier, 0.96f));
	TestEqual(TEXT("一枚手套不会越过一格"), Game.Vital(Enemies[2]).Health, 100.0f);
	if (!Game.Equip(EBadge::StringGlove) || !TestTrue(TEXT("第二枚手套再次手动点破"), Pop(true))) { return false; }
	TestTrue(TEXT("两枚手套扩大两格"), FMath::IsNearlyEqual(Game.Vital(Enemies[2]).Health, 95.6f)
		&& FMath::IsNearlyEqual(Game.Vital(Enemies[2]).SupportMovementMultiplier, 0.96f));
	const float OutsideHealth = Game.Vital(Enemies[1]).Health;
	if (!TestTrue(TEXT("敌人打破下一枚气球"), Pop(false))) { return false; }
	TestEqual(TEXT("敌方击破不继承手套额外半径"), Game.Vital(Enemies[1]).Health, OutsideHealth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgesFullBeeTest, "MovingCastle.Items.Badges.Full.BeeKillIncome",
	CastleBadgesFullTests::Flags)

bool FCastleBadgesFullBeeTest::RunTest(const FString&)
{
	using namespace CastleBadgesFullTests;
	FGame Game(*this);
	if (!Game.Ground() || !Game.LoadSupportDefinition(TEXT("BeeHive"))
		|| !Game.Equip(EBadge::BerserkBee) || !Game.Equip(EBadge::BerserkBee)
		|| !Game.Equip(EBadge::CoinPurse) || !Game.Equip(EBadge::FirepowerCard)) { return false; }
	auto* Hive = Game.Place(ECastleBuildingType::BeeHive, {0, 0, 0}, 1, 2);
	if (!Hive) { return false; }
	const FVector Nest = Hive->GetActorLocation() + Game.Board->GetActorUpVector() * (Game.Board->LayerHeight + 80.0);
	TArray<FCastleMassUnitHandle> Enemies;
	if (!Game.Spawn<UCastleMassEnemyTrait>({FTransform(Nest)}, Enemies)) { return false; }
	auto* Support = Game.World->GetSubsystem<UCastleSupportTowerSubsystem>();
	if (!TestTrue(TEXT("触发真实一级蜂巢"), Hive->TriggerBuildingEffect({}))) { return false; }
	Support->Tick(0.5f);
	const float Damage = static_cast<float>(2.0 * Hive->GetBuildingDefinition()->SupportData->BeeDamage * 1.1);
	TestTrue(TEXT("非致死一击按蜂数、基础伤害与火力倍率结算"), FMath::IsNearlyEqual(Game.Vital(Enemies[0]).Health, 100.0f - Damage, 0.001f));
	TestEqual(TEXT("非致死攻击不发金币"), Game.Economy->GetGold(), 0);
	Game.Vital(Enemies[0]).Health = Damage / 2.0f;
	if (!TestTrue(TEXT("另派一批蜂群同帧争抢同一敌人"), Hive->TriggerBuildingEffect({}))) { return false; }
	Support->Tick(0.5f);
	TestEqual(TEXT("实际击杀敌人"), Game.Vital(Enemies[0]).Health, 0.0f);
	TestEqual(TEXT("两枚每杀 10 金，加零钱袋只发 11 金且不重复"), Game.Economy->GetGold(), 11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgesFullPatrolTest, "MovingCastle.Items.Badges.Full.SharedPatrolAndReassignment",
	CastleBadgesFullTests::Flags)

bool FCastleBadgesFullPatrolTest::RunTest(const FString&)
{
	using namespace CastleBadgesFullTests;
	FGame Game(*this);
	Game.Director->OnRunStarted.Broadcast();
	if (!Game.Equip(EBadge::DoubleDoorPass) || !Game.Equip(EBadge::DoubleDoorPass) || !Game.Equip(EBadge::CoinPurse)) { return false; }
	const double Threshold = Game.Board->CellSize * 10.0;
	Game.PublishStraightPath(Threshold);
	Game.Director->OnBattleClearedForItems.Broadcast(ECastleBattleKind::Normal, 1);
	TestEqual(TEXT("双门路线正好十格不发金币"), Game.Economy->GetGold(), 0);
	Game.PublishStraightPath(Threshold + 0.1);
	Game.Director->OnBattleClearedForItems.Broadcast(ECastleBattleKind::Normal, 2);
	TestEqual(TEXT("略超十格按两枚打卡牌与零钱袋给 22 金"), Game.Economy->GetGold(), 22);

	Game.Badges->ClearAllBadgesForTesting();
	if (!Game.Equip(EBadge::QuickLadder) || !Game.Equip(EBadge::LateAdvantage)) { return false; }
	Game.MakeTower();
	Game.PublishStraightPath(200.0, true);
	auto* NPCConfig = Game.MakeUnitConfig<UCastleMassNPCTrait>();
	auto* DispatchConfig = NewObject<UCastleTowerDispatchConfigDataAsset>(Game.World);
	DispatchConfig->NPCConfiguration = NPCConfig;
	auto* Dispatch = Game.World->GetSubsystem<UCastleTowerDispatchSubsystem>();
	FText Reason;
	if (!TestTrue(TEXT("开始真实正式出兵会话"), Dispatch->Configure(DispatchConfig, Reason)
		&& Dispatch->StartBattleDispatchSession(DispatchConfig->NPCConfiguration, 1, Reason)
		&& Dispatch->SetAutoDispatchEnabled(true))) { return false; }
	FCastleMassUnitHandle Formal;
	const auto Listener = Dispatch->OnNPCDispatched.AddLambda([&Formal](const FCastleMassUnitHandle& Unit) { Formal = Unit; });
	if (!TestTrue(TEXT("从门 A 派一名正式兵"), Dispatch->TryDispatchSingleNPC(Reason))) { return false; }
	Dispatch->OnNPCDispatched.Remove(Listener);
	auto& Patrol = Game.Manager().GetFragmentDataChecked<FCastleMassPatrolFragment>(Formal.Entity);
	Game.StartWave();
	Game.Step<UCastleMassPatrolProcessor>(0.5f);
	TestTrue(TEXT("首圈 0.5 倍，梯速 1.5 倍，共移动 37.5 cm"), FMath::IsNearlyEqual(Patrol.Distance, 37.5, 0.001));
	if (!Game.Equip(EBadge::QuickLadder) || !Game.Equip(EBadge::LateAdvantage)) { return false; }
	Game.Step<UCastleMassPatrolProcessor>(0.5f);
	TestTrue(TEXT("第二枚后手优势不累乘，第二枚快速梯子叠至 2 倍"), FMath::IsNearlyEqual(Patrol.Distance, 87.5, 0.001));
	Game.Step<UCastleMassPatrolProcessor>(2.375f);
	TestEqual(TEXT("跨圈后真正完成一圈"), Patrol.CompletedLaps, int64{1});
	TestTrue(TEXT("同一帧跨门后，剩余 0.25 秒按第二圈 1.2 倍与 2 倍梯速走 60 cm"),
		FMath::IsNearlyEqual(Patrol.Distance, 60.0, 0.001));

	// 重新开一场计数，保留同一正式兵夹具；以无梯速和圈速加成的两秒一圈验证精确门槛。
	Game.Badges->ClearAllBadgesForTesting();
	if (!Game.Equip(EBadge::LapReassignment) || !Game.Equip(EBadge::LapReassignment)) { return false; }
	Game.StartWave();
	Patrol.Distance = 0.0;
	Patrol.CompletedLaps = 0;
	const auto CountTemporary = [&](const bool bValidateAtDoor = false)
	{
		auto& Manager = Game.Manager();
		FMassEntityQuery Query(Manager.AsShared());
		Query.AddRequirement<FCastleMassPatrolFragment>(EMassFragmentAccess::ReadOnly);
		FMassExecutionContext Context(Manager, 0.0f, false);
		int32 Count = 0;
		Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk)
		{
			for (const auto& Candidate : Chunk.GetFragmentView<FCastleMassPatrolFragment>())
			{
				if (!Candidate.bTemporaryOneLap) { continue; }
				++Count;
				if (bValidateAtDoor)
				{
					TestTrue(TEXT("自动派兵仍使临时兵到门 B 停止"), Candidate.bReachedDispatchEntrance);
					TestEqual(TEXT("临时兵只走完整一圈"), Candidate.CompletedLaps, int64{1});
				}
			}
		});
		return Count;
	};
	Game.Step<UCastleMassPatrolProcessor>(18.0f);
	Dispatch->Tick(0.0f);
	TestEqual(TEXT("九圈不派临时兵"), CountTemporary(), 0);
	Game.StartWave(2);
	Game.Step<UCastleMassPatrolProcessor>(2.0f);
	Dispatch->Tick(0.0f);
	TestEqual(TEXT("跨波累计第十圈派一人，两枚仍不叠加"), CountTemporary(), 1);
	int32 Reserve = -1, Active = -1;
	Dispatch->GetDispatchCounts(Reserve, Active);
	TestTrue(TEXT("临时兵不占正式池，也不计入正式在场人数"), Reserve == 0 && Active == 1);
	Game.Step<UCastleMassPatrolProcessor>(2.0f);
	TestEqual(TEXT("临时实体在正式回收前仍能观察"), CountTemporary(true), 1);
	Dispatch->Tick(0.0f);
	Game.Step<UCastleMassLifetimeProcessor>(0.0f);
	Dispatch->Tick(0.0f);
	TestEqual(TEXT("临时单位按正式 Mass 生命周期销毁"), CountTemporary(), 0);
	Game.Step<UCastleMassPatrolProcessor>(16.0f);
	Dispatch->Tick(0.0f);
	TestEqual(TEXT("十九个正式圈加临时兵一圈仍不足二十"), CountTemporary(), 0);
	Game.Step<UCastleMassPatrolProcessor>(2.0f);
	Dispatch->Tick(0.0f);
	TestEqual(TEXT("第二十个正式圈发下一名临时兵"), CountTemporary(), 1);
	TestTrue(TEXT("战斗结束回收仍在场的正式兵和临时兵"), Dispatch->EndBattleDispatchSession(Reason));
	Game.Step<UCastleMassLifetimeProcessor>(0.0f);
	Dispatch->Tick(0.0f);
	TestFalse(TEXT("全部实体退出后关闭派兵会话"), Dispatch->IsSessionActive());
	TestEqual(TEXT("收尾无遗留临时实体"), CountTemporary(), 0);
	return true;
}

#endif
