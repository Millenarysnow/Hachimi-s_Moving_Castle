#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleGambleSkyIsland.h"
#include "CastleGambleRouletteWidget.h"
#include "CastleGambleAcceptanceIsland.h"

#include "Blueprint/GameViewportSubsystem.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/SkyIslands/Shop/CastleShopSkyIslandDataAsset.h"
#include "CastleBackpackComponent.h"
#include "CastleBuildingDefinition.h"
#include "CastleEconomyComponent.h"
#include "CastleGroundTileActor.h"
#include "CastleMainTowerComponent.h"
#include "CastlePlayerController.h"
#include "Components/Button.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Items/CastleBadgeDataAsset.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Items/CastleItemGrantLibrary.h"
#include "Items/CastlePendingRoomReward.h"
#include "Items/CastleRoomGroupDataAsset.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "UI/Items/CastleWildcardRoomPickerWidget.h"
#include "UI/Items/CastleBadgeBarWidget.h"
#include "UI/Items/CastleBadgeReplaceModalWidget.h"
#include "UI/Items/CastleTempBuildBarWidget.h"

namespace
{
	UCastleGambleSkyIslandDataAsset* LoadGambleConfig()
	{
		return LoadObject<UCastleGambleSkyIslandDataAsset>(nullptr,
			TEXT("/Game/MovingCastle/Data/SkyIslands/DA_GambleSkyIsland.DA_GambleSkyIsland"));
	}

	/** 本文件的整卡比较与建造栏使用不同的符号，供 Unity 编译合并时区分。 */
	bool IsSameGambleCard(const FCastleBuildingDescriptor& A, const FCastleBuildingDescriptor& B)
	{
		return A.Definition == B.Definition && A.Level == B.Level && A.Color == B.Color && A.Number == B.Number;
	}

	/** 用公开权重入口查找能抽出指定颜色的种子，实际发奖仍必须由机台执行。 */
	int32 FindRouletteSeed(const UCastleGambleSkyIslandDataAsset& Config, ECastleRouletteColor Bet, ECastleRouletteColor Target)
	{
		for (int32 Seed = 0; Seed < 4096; ++Seed)
		{
			FRandomStream Random(Seed);
			ECastleRouletteColor Result;
			int32 Slot = INDEX_NONE;
			if (Config.TryRollRoulette(Bet, 0.0, Random.GetUnsignedInt() / 4294967296.0, Result, Slot) && Result == Target)
			{
				return Seed;
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleGambleIslandAutomationTest, "MovingCastle.SkyIslands.Gamble.Transactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleGambleIslandAutomationTest::RunTest(const FString& Parameters)
{
	auto* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	if (!TestNotNull(TEXT("隔离游戏世界"), World)) { return false; }
	// 账户与机台从同一个 GameInstance 读徽章，覆盖零钱袋真正的查找链路。
	ON_SCOPE_EXIT
	{
		Instance->Shutdown();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	FText Reason;
	auto* Config = LoadGambleConfig();
	auto* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_NormalRoom.DA_NormalRoom"));
	if (!TestNotNull(TEXT("赌博岛正式 DA"), Config) || !TestNotNull(TEXT("正式普通房"), Definition)) { return false; }

	auto* Island = World->SpawnActor<ACastleGambleSkyIsland>();
	if (!TestNotNull(TEXT("原生赌博岛"), Island)) { return false; }
	// 只为隔离机台接上真实组件，生产入口从当前游戏世界解析这几项依赖。
	Island->Config = DuplicateObject(Config, Island);
	Island->Backpack = NewObject<UCastleBackpackComponent>(Island);
	Island->Economy = NewObject<UCastleEconomyComponent>(Island);
	Island->Tower = NewObject<UCastleMainTowerComponent>(Island);
	Island->Badges = Instance->GetSubsystem<UCastleBadgeSubsystem>();
	Island->MiniCellSize = 40.0;
	Island->MiniTileSize = 38.0;
	Island->MiniLayerHeight = 43.0;
	UCastleBackpackComponent& Backpack = *Island->Backpack;
	FCastleBuildingDescriptor RedOne, RedTwo, BlueOne, GreenThree;
	if (!TestTrue(TEXT("生成正式测试房间"),
		Definition->CreateDescriptor(1, ECastleTileColor::Red, 1, RedOne, Reason)
		&& Definition->CreateDescriptor(1, ECastleTileColor::Red, 2, RedTwo, Reason)
		&& Definition->CreateDescriptor(1, ECastleTileColor::Blue, 1, BlueOne, Reason)
		&& Definition->CreateDescriptor(1, ECastleTileColor::Green, 3, GreenThree, Reason))) { return false; }
	const FCastleBuildingDescriptor Initial[] = {RedOne, GreenThree, RedTwo, BlueOne};
	// 第二格与原点不同色号，用它区分地面匹配和已放房间的相邻匹配。
	const FCastleGroundCell Ground[] = {
		{FIntVector::ZeroValue, ECastleTileColor::Red, 9}, {FIntVector(1, 0, 0), ECastleTileColor::Yellow, 8}
	};
	if (!TestTrue(TEXT("初始化带色号的测试地面"), Island->MiniBoard.InitializeGround(Ground, FIntPoint(2, 1), Reason))) { return false; }
	if (!TestTrue(TEXT("填入测试牌"), Backpack.TryAddItemsAllowOverflow(Initial, Reason))) { return false; }
	TestFalse(TEXT("空微缩区拒绝开转"), Island->TrySpinRoulette(ECastleRouletteColor::Black, Reason));
	TestFalse(TEXT("首块不能放在不同色且不同号的地面"), Island->TryPlaceCard(0, FIntVector(1, 0, 0), Reason));
	if (!TestTrue(TEXT("首块可与地面同色放置"), Island->TryPlaceCard(0, FIntVector::ZeroValue, Reason))) { return false; }
	TestFalse(TEXT("异色且异号被拒"), Island->TryPlaceCard(0, FIntVector(1, 0, 0), Reason));
	TestEqual(TEXT("拒绝后仍有三张"), Backpack.GetItemCount(), 3);
	TestTrue(TEXT("同色横向相邻"), Island->TryPlaceCard(1, FIntVector(1, 0, 0), Reason));
	TestTrue(TEXT("同数字垂直相邻"), Island->TryPlaceCard(1, FIntVector(0, 0, 1), Reason));
	TestTrue(TEXT("撤回最后一张"), Island->TryReturnLastCard(Reason));
	FCastleBuildingDescriptor Returned;
	TestTrue(TEXT("撤回保留完整牌面"), Backpack.TryGetItemAt(1, Returned) && IsSameGambleCard(Returned, BlueOne));
	TestFalse(TEXT("拦截复制张数溢出"), Island->MiniBoard.CanSettle(MAX_int32, Reason));

	Island->EndPlay(EEndPlayReason::Destroyed);
	TestEqual(TEXT("EndPlay 清空未下注暂存"), Island->MiniBoard.GetPlacements().Num(), 0);
	TestEqual(TEXT("EndPlay 全量退回"), Backpack.GetItemCount(), 4);
	TestTrue(TEXT("离岛退回第一张原牌"), Backpack.TryGetItemAt(2, Returned) && IsSameGambleCard(Returned, RedOne));
	TestTrue(TEXT("离岛退回第二张原牌"), Backpack.TryGetItemAt(3, Returned) && IsSameGambleCard(Returned, RedTwo));

	const auto VerifyRoulette = [&](ECastleRouletteColor Bet, ECastleRouletteColor Result, int32 Expected)
	{
		// 每色用独立背包，以一个主栏槽明确验证所有溢出与结算后的牌面。
		Island->Backpack = NewObject<UCastleBackpackComponent>(Island);
		auto& PayoutBackpack = *Island->Backpack;
		int32 Index = INDEX_NONE;
		if (!TestTrue(TEXT("准备轮盘测试牌"), PayoutBackpack.TrySetCapacity(1, Reason)
			&& PayoutBackpack.TryAddItem(RedOne, Index, Reason) && Island->TryPlaceCard(Index, FIntVector::ZeroValue, Reason))) { return; }
		const int32 Seed = FindRouletteSeed(*Config, Bet, Result);
		if (!TestTrue(TEXT("找到目标落点的种子"), Seed != INDEX_NONE)) { return; }
		Island->RouletteRandom.Initialize(Seed);
		if (!TestTrue(TEXT("原生机台开转"), Island->TrySpinRoulette(Bet, Reason))) { return; }
		TestEqual(TEXT("开奖耗尽微缩区"), Island->MiniBoard.GetPlacements().Num(), 0);
		TArray<FCastleBuildingDescriptor> Paid = PayoutBackpack.GetItems();
		Paid.Append(PayoutBackpack.GetTempItems());
		TestEqual(TEXT("总返还张数"), Paid.Num(), Expected);
		TestEqual(TEXT("临时栏溢出张数"), PayoutBackpack.GetTempItemCount(), FMath::Max(0, Expected - 1));
		for (const FCastleBuildingDescriptor& Card : Paid) { TestTrue(TEXT("复制保留完整牌面"), IsSameGambleCard(Card, RedOne)); }
		Island->Tick(2.0f);
	};
	VerifyRoulette(ECastleRouletteColor::Black, ECastleRouletteColor::Black, 2);
	VerifyRoulette(ECastleRouletteColor::Red, ECastleRouletteColor::Red, 2);
	VerifyRoulette(ECastleRouletteColor::Green, ECastleRouletteColor::Green, 30);
	VerifyRoulette(ECastleRouletteColor::Black, ECastleRouletteColor::Red, 0);

	TestFalse(TEXT("余额不足时老虎机拒绝"), Island->TryPlaySlot(Reason));
	TestEqual(TEXT("拒绝不改变余额"), Island->Economy->GetGold(), 0);
	TestTrue(TEXT("准备老虎机金币"), Island->Economy->TryAddGold(100, Reason));
	Island->Config->SlotWeights = {40, 0, 0, 0, 0};
	TestTrue(TEXT("未中档结算"), Island->TryPlaySlot(Reason));
	TestEqual(TEXT("未中只花十金"), Island->Economy->GetGold(), 90);
	Island->Config->SlotWeights = {0, 30, 0, 0, 0};
	TestTrue(TEXT("两倍金币档结算"), Island->TryPlaySlot(Reason));
	TestEqual(TEXT("两倍档净赚十金"), Island->Economy->GetGold(), 100);
	Island->Config->SlotWeights = {0, 0, 12, 0, 0};
	TestTrue(TEXT("金币大奖档结算"), Island->TryPlaySlot(Reason));
	TestEqual(TEXT("五倍档净赚四十金"), Island->Economy->GetGold(), 140);
	const auto* CoinPurse = LoadObject<UCastleBadgeDataAsset>(nullptr,
		TEXT("/Game/MovingCastle/Data/Items/Badges/DA_Badge_02_CoinPurse.DA_Badge_02_CoinPurse"));
	bool bNeedsReplacement = false;
	if (!TestTrue(TEXT("装备正式零钱袋"), UCastleItemGrantLibrary::TryGrantBadge(CoinPurse, Island->Badges, bNeedsReplacement, Reason))) { return false; }
	Island->Config->SlotWeights = {0, 30, 0, 0, 0};
	TestTrue(TEXT("零钱袋生效时两倍档结算"), Island->TryPlaySlot(Reason));
	TestEqual(TEXT("投 10 金，实收 22 金"), Island->Economy->GetGold(), 152);
	Island->Config->SlotWeights = {0, 0, 12, 0, 0};
	TestTrue(TEXT("零钱袋生效时五倍档结算"), Island->TryPlaySlot(Reason));
	TestEqual(TEXT("投 10 金，实收 55 金"), Island->Economy->GetGold(), 197);

	// 这笔两倍奖的基础净额恰好可入账，应用零钱袋才会超出上限。
	const int32 NearLimit = MAX_int32 - Island->Config->SlotMachineCost;
	const int32 FillGold = NearLimit - Island->Economy->GetGold();
	TestTrue(TEXT("准备临界余额"), Island->Economy->TryAddGold(FillGold, Reason));
	Island->Config->SlotWeights = {0, 30, 0, 0, 0};
	const int32 LimitSeed = Island->SlotRandom.GetCurrentSeed();
	AddExpectedError(TEXT("拒绝玩法收入 20"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("老虎机金币奖励未结算"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("加成后溢出拒绝结算"), Island->TryPlaySlot(Reason));
	TestEqual(TEXT("溢出时余额未变化"), Island->Economy->GetGold(), NearLimit);
	TestEqual(TEXT("溢出不消耗随机流"), Island->SlotRandom.GetCurrentSeed(), LimitSeed);
	TestTrue(TEXT("恢复临界测试前余额"), Island->Economy->TrySpendGold(FillGold, Reason));

	// 真实归一化奖品消费首牌，再将六张蓝 1 放入主栏及临时栏。筹码和库存由机台一次提交。
	auto* Normalize = LoadObject<UCastleRoomGroupDataAsset>(nullptr,
		TEXT("/Game/MovingCastle/Data/Items/RoomGroups/DA_RoomGroup_20_Normalize.DA_RoomGroup_20_Normalize"));
	if (!TestNotNull(TEXT("正式归一化奖品"), Normalize)) { return false; }
	int32 SourceSlot = INDEX_NONE;
	if (!TestTrue(TEXT("准备归一化首牌"), Island->Backpack->TryAddBuilding(Definition, 1, ECastleTileColor::Blue, 6, SourceSlot, Reason))) { return false; }
	Island->Config->RoomGroups = {Normalize};
	Island->Config->SlotWeights = {0, 0, 0, 1, 0};
	if (!TestTrue(TEXT("正式老虎机房间组触发背包变换"), Island->TryPlaySlot(Reason))) { return false; }
	TestEqual(TEXT("归一化仅扣一次投币成本"), Island->Economy->GetGold(), 187);
	TArray<FCastleBuildingDescriptor> Normalized = Island->Backpack->GetItems();
	Normalized.Append(Island->Backpack->GetTempItems());
	TestEqual(TEXT("蓝 6 拆为六张房间卡"), Normalized.Num(), 6);
	for (const auto& Card : Normalized)
	{
		TestTrue(TEXT("归一化保留定义和蓝色，数字变为 1"),
			Card.Definition == Definition && Card.Color == ECastleTileColor::Blue && Card.Number == 1);
	}
	// 模拟已初始化的机台奖品失效，验证拒绝时不会提交投币与随机流。
	Island->Config->RoomGroups = {nullptr};
	Island->Config->RoomDefinitions = {Definition};
	Island->Config->SlotWeights = {0, 0, 0, 12, 0};
	const int32 SeedBefore = Island->SlotRandom.GetCurrentSeed();
	const int32 CardsBefore = Island->Backpack->GetItemCount() + Island->Backpack->GetTempItemCount();
	AddExpectedError(TEXT("老虎机抽中的房间组资产失效"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("房间组奖项失效时拒绝投币"), Island->TryPlaySlot(Reason));
	TestEqual(TEXT("房间组档失败不扣金币"), Island->Economy->GetGold(), 187);
	TestEqual(TEXT("房间组档失败不消耗随机流"), Island->SlotRandom.GetCurrentSeed(), SeedBefore);
	TestEqual(TEXT("房间组档失败不发卡"), Island->Backpack->GetItemCount() + Island->Backpack->GetTempItemCount(), CardsBefore);

	Island->Tower->MaxHealth = 400;
	Island->Tower->MaxBalloons = 2;
	Island->Tower->HealthPerBalloon = 200;
	TestTrue(TEXT("主塔初始化两球"), Island->Tower->InitializeHealth());
	TestTrue(TEXT("回收一球"), Island->TryRecycleBalloon(Reason));
	TestEqual(TEXT("扣一球血量"), Island->Tower->CurrentHealth, 200.0f);
	TestEqual(TEXT("回收发三十金，零钱袋不加成"), Island->Economy->GetGold(), 217);
	AddExpectedError(TEXT("回收后需要保留至少一个存活气球"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("最后一球拒绝回收"), Island->TryRecycleBalloon(Reason));
	TestTrue(TEXT("最后一球仍存活且未当作受击"), Island->Tower->IsAlive() && Island->Tower->AttackCount == 0);
	TestEqual(TEXT("拒绝时不发币"), Island->Economy->GetGold(), 217);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleGambleCatalogTest, "MovingCastle.SkyIslands.Gamble.ConfigValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleGambleCatalogTest::RunTest(const FString& Parameters)
{
	const auto* Gamble = LoadGambleConfig();
	const auto* Shop = LoadObject<UCastleShopSkyIslandDataAsset>(nullptr,
		TEXT("/Game/MovingCastle/Data/SkyIslands/DA_ShopSkyIsland.DA_ShopSkyIsland"));
	if (!TestNotNull(TEXT("正式赌博配置"), Gamble) || !TestNotNull(TEXT("正式商店配置"), Shop)) { return false; }
	FText Reason;
	TestTrue(TEXT("赌博配置通过原生校验"), Gamble->Validate(Reason));
	TestTrue(TEXT("老虎机使用商店校验过的正式房间组目录"), Shop->Validate(Reason));
	TestEqual(TEXT("房间组 26 个"), Gamble->RoomGroups.Num(), 26);
	TestEqual(TEXT("徽章 25 枚"), Gamble->Badges.Num(), 25);
	for (const auto& Group : Shop->RoomGroupPool)
	{
		TestTrue(GetPathNameSafe(Group) + TEXT(" 同时是赌博奖品"), Gamble->RoomGroups.Contains(Group));
	}
	for (const auto& Badge : Shop->BadgePool)
	{
		TestTrue(GetPathNameSafe(Badge) + TEXT(" 同时是赌博奖品"), Gamble->Badges.Contains(Badge));
	}

	// 七个正式建筑定义就是随机房间池，也用于校验自定义选项里的硬引用。
	const TCHAR* DefinitionNames[] = {
		TEXT("DA_NormalRoom"), TEXT("DA_ArcherTower"), TEXT("DA_CannonTower"), TEXT("DA_Farm"),
		TEXT("DA_ShieldTower"), TEXT("DA_BeeHive"), TEXT("DA_BalloonTower")
	};
	TestEqual(TEXT("随机房间仅包含普通房和六类功能塔"), Gamble->RoomDefinitions.Num(), static_cast<int32>(UE_ARRAY_COUNT(DefinitionNames)));
	for (const TCHAR* Name : DefinitionNames)
	{
		const auto* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
			*FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/%s.%s"), Name, Name));
		if (!TestNotNull(Name, Definition)) { return false; }
		TestTrue(FString(Name) + TEXT(" 属于赌博房间定义池"), Gamble->RoomDefinitions.Contains(Definition));
	}
	for (const UCastleRoomGroupDataAsset* Group : Gamble->RoomGroups)
	{
		if (!TestNotNull(TEXT("房间组资产"), Group)) { return false; }
		if (Group->bChooseInPicker)
		{
			FCastleWildcardRoomOptions Options;
			TestTrue(Group->GetName() + TEXT(" 的 RoomSpec 可从正式池解析"),
				Group->RoomSpec.TryResolveOptions(Gamble->RoomDefinitions, Options, Reason));
			TestEqual(Group->GetName() + TEXT(" 有四种玩法色"), Options.Colors.Num(), 4);
			TestTrue(Group->GetName() + TEXT(" 数字候选是 1 至 6"), Options.Numbers == TArray<int32>{1, 2, 3, 4, 5, 6});
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleGambleOddsAutomationTest, "MovingCastle.SkyIslands.Gamble.OddsAndPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleGambleOddsAutomationTest::RunTest(const FString& Parameters)
{
	auto* Config = LoadGambleConfig();
	if (!TestNotNull(TEXT("赌博空岛 DA"), Config)) { return false; }
	int32 SlotHits = 0;
	int32 RouletteFrequency[3] = {};
	FRandomStream Random(1352227);
	constexpr int32 Samples = 100000;
	// 和实际机台一样按基础权重抽样，保留轮盘分布与老虎机中奖率回归。
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const double Sample = Random.GetUnsignedInt() / 4294967296.0;
		ECastleRouletteColor Color;
		ECastleSlotPrize Prize;
		int32 Slot = INDEX_NONE;
		if (!Config->TryRollRoulette(ECastleRouletteColor::Black, 0.0, Sample, Color, Slot)
			|| !Config->TryRollSlot(0.0, Sample, Prize)) { AddError(TEXT("有效样本落在概率区间外")); return false; }
		++RouletteFrequency[static_cast<uint8>(Color)];
		SlotHits += Prize != ECastleSlotPrize::None;
	}
	TestTrue(TEXT("黑色接近 18/37"), FMath::Abs(RouletteFrequency[0] / double(Samples) - 18.0 / 37.0) < 0.005);
	TestTrue(TEXT("红色接近 18/37"), FMath::Abs(RouletteFrequency[1] / double(Samples) - 18.0 / 37.0) < 0.005);
	TestTrue(TEXT("绿色接近 1/37"), FMath::Abs(RouletteFrequency[2] / double(Samples) - 1.0 / 37.0) < 0.005);
	TestTrue(TEXT("老虎机中奖率接近 60%"), FMath::Abs(SlotHits / double(Samples) - 0.60) < 0.005);

	UClass* Panel = LoadClass<UCastleGambleRouletteWidget>(nullptr,
		TEXT("/Game/MovingCastle/UI/SkyIslands/Gamble/WBP_GambleRoulettePanel.WBP_GambleRoulettePanel_C"));
	auto* GeneratedPanel = Cast<UWidgetBlueprintGeneratedClass>(Panel);
	if (!TestNotNull(TEXT("保存后的轮盘面板为原生基类的 Widget Blueprint"), GeneratedPanel)) { return false; }
	const UWidgetTree* Tree = GeneratedPanel->GetWidgetTreeArchetype();
	if (!TestNotNull(TEXT("占位面板含持久化控件树"), Tree)) { return false; }
	for (const FName Name : {FName(TEXT("ReturnAllButton")), FName(TEXT("ReturnButton")), FName(TEXT("SpinButton")), FName(TEXT("CloseButton"))})
	{
		TestNotNull(FString::Printf(TEXT("保存后的控件树包含 %s"), *Name.ToString()), Tree->FindWidget(Name));
	}
	TestNull(TEXT("轮盘面板不再包含选牌和坐标下拉框"), Tree->FindWidget(TEXT("CardOptions")));
	TestNull(TEXT("轮盘面板不再包含坐标下拉框"), Tree->FindWidget(TEXT("CellOptions")));
	Tree->ForEachWidget([this](UWidget* Widget)
	{
		if (const auto* Text = Cast<UTextBlock>(Widget))
		{
			const FSlateFontInfo& Font = Text->GetFont();
			const UFont* Asset = Cast<UFont>(Font.FontObject);
			TestTrue(FString::Printf(TEXT("%s 使用有效的微软雅黑字重"), *Text->GetName()),
				Asset && Asset->GetFName() == TEXT("MicrosoftYaHei_Font")
				&& Asset->GetInternalCompositeFont().DefaultTypeface.Fonts.ContainsByPredicate(
					[&Font](const FTypefaceEntry& Entry) { return Entry.Name == Font.TypefaceFontName; }));
		}
	});
	return true;
}

namespace
{
	UWorld* FindGamblePIE()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World()) { return Context.World(); }
		}
		return nullptr;
	}
}

/** 通过实际命令创建验收岛，走真实机台、控件绑定、奖励入口和延后离岛销毁。 */
class FCastleGambleAcceptanceScenario : public IAutomationLatentCommand
{
public:
	explicit FCastleGambleAcceptanceScenario(FAutomationTestBase& InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() > Deadline)
		{
			Test.AddError(TEXT("赌博空岛 MainMap PIE 启动或离岛超时。"));
			return true;
		}
		UWorld* World = FindGamblePIE();
		if (!World || !World->HasBegunPlay()) { return false; }
		auto* Director = World->GetSubsystem<UCastleDirectorSubsystem>();
		auto* Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
		if (!Director || !Director->IsRunning() || !IsValid(Controller) || !IsValid(Controller->GetPawn())) { return false; }
		if (bDeparting)
		{
			if (TActorIterator<ACastleGambleAcceptanceIsland>(World)) { return false; }
			Test.TestNull(TEXT("离岛后控制器清除交互空岛"), Controller->GetInteractiveSkyIsland());
			for (const auto& Tile : Tiles) { Test.TestFalse(TEXT("离岛销毁微缩地块"), Tile.IsValid()); }
			Test.TestTrue(TEXT("离岛返回原来的飞行位置"), Controller->GetPawn()->GetActorLocation().Equals(ReturnLocation, 5.0));
			Test.TestTrue(TEXT("离岛退回 1 张未下注原牌"), Backpack.IsValid()
				&& Backpack->GetItemCount() + Backpack->GetTempItemCount() == CardsBeforeDeparture);
			Test.TestTrue(TEXT("验收过程保留本局关数"), Director->GetClearedSteps() == OriginalStep);
			return true;
		}
		// 本用例验证离屏视口的中心准星，显式选择经典模式且不保存偏好。
		Controller->SetControlMode(ECastleControlMode::Classic, false);
		ReturnLocation = Controller->GetPawn()->GetActorLocation();
		OriginalStep = Director->GetClearedSteps();
		FOutputDeviceNull Output;
		if (!Test.TestTrue(TEXT("MainMap 识别赌博岛验收命令"), IConsoleManager::Get().ProcessUserConsoleInput(
			TEXT("CastleTestGambleSkyIsland"), Output, World))) { return true; }
		TActorIterator<ACastleGambleAcceptanceIsland> IslandIt(World);
		if (!Test.TestTrue(TEXT("命令生成实际赌博空岛"), static_cast<bool>(IslandIt))) { return true; }
		auto* Island = *IslandIt;
		if (!Test.TestTrue(TEXT("验收岛注册为控制器的交互空岛"), Controller->GetInteractiveSkyIsland() == Island)) { return true; }
		Test.TestTrue(TEXT("玩家传送至赌博岛到达锚点"), Controller->GetPawn()->GetActorLocation().Equals(
			Island->GetPlayerArrivalTransform().GetLocation(), 5.0));
		Test.TestEqual(TEXT("生成 5×5 个真实地块"), Island->MiniTiles.Num(), 25);
		Backpack = Island->GetBackpack();
		FCastleBuildingDescriptor Card;
		if (!Test.TestTrue(TEXT("从实际手牌读取首个槽位"), Backpack->TryGetItemAt(0, Card))) { return true; }
		const int32 InitialCards = Backpack->GetItemCount() + Backpack->GetTempItemCount();
		const ACastleGroundTileActor* Match = nullptr;
		const ACastleGroundTileActor* Mismatch = nullptr;
		for (const auto& Entry : Island->MiniTiles)
		{
			Tiles.Add(Entry.Value.Get());
			const FCastleGroundCell Cell = Entry.Value->GetGroundCell();
			if (Cell.Color == Card.Color || Cell.Number == Card.Number) { Match = Entry.Value.Get(); }
			else { Mismatch = Entry.Value.Get(); }
		}
		FText Reason;
		if (!Test.TestNotNull(TEXT("生成与首张手牌同色或同号的地面"), Match)
			|| !Test.TestNotNull(TEXT("生成与首张手牌异色异号的地面"), Mismatch)) { return true; }

		// 仅移动测试相机，保留玩家落点和返回位置，让控制器用真实 PIE 视口中心反投影。
		AActor* OriginalViewTarget = Controller->GetViewTarget();
		auto* AimCamera = World->SpawnActor<ACameraActor>();
		if (!Test.TestNotNull(TEXT("创建用于准星瞄准的 PIE 相机"), AimCamera)) { return true; }
		ON_SCOPE_EXIT
		{
			Controller->SetViewTarget(OriginalViewTarget);
			AimCamera->Destroy();
		};
		Controller->SetViewTarget(AimCamera);
		const auto AimDown = [Controller, AimCamera](const FVector& Surface, double Distance)
		{
			AimCamera->SetActorLocationAndRotation(Surface + FVector::UpVector * Distance, FRotator(-90.0, 0.0, 0.0));
			Controller->PlayerCameraManager->UpdateCamera(0.0f);
			static_cast<AActor*>(Controller)->Tick(0.0f);
			return Controller->GetCurrentGridCursorTarget();
		};
		// 直接派发已登记的按下绑定，避免无头 PIE 的窗口焦点影响真实控制器处理入口。
		if (!Test.TestNotNull(TEXT("控制器已创建输入组件"), Controller->InputComponent.Get())) { return true; }
		const auto PressKey = [this, Controller](const FKey& Key)
		{
			const FInputKeyBinding* Binding = Controller->InputComponent->KeyBindings.FindByPredicate(
				[&Key, Controller](const FInputKeyBinding& Entry)
				{
					return Entry.Chord == FInputChord(Key) && Entry.KeyEvent == IE_Pressed
						&& Entry.KeyDelegate.IsBoundToObject(Controller);
				});
			if (!Test.TestNotNull(FString::Printf(TEXT("控制器已绑定按键 %s"), *Key.ToString()), Binding)) { return false; }
			Binding->KeyDelegate.Execute(Key);
			return true;
		};
		if (!PressKey(EKeys::Two) || !PressKey(EKeys::MouseScrollUp)) { return true; }
		Test.TestEqual(TEXT("数字键后向上滚轮回到首张"), Controller->GetSelectedBackpackIndex(), 0);
		if (!PressKey(EKeys::MouseScrollDown)) { return true; }
		Test.TestEqual(TEXT("向下滚轮选择第二张"), Controller->GetSelectedBackpackIndex(), 1);
		if (!PressKey(EKeys::One)) { return true; }
		const int32 SelectedSlot = Controller->GetSelectedBackpackIndex();
		Test.TestEqual(TEXT("数字键 1 选中第一张手牌"), SelectedSlot, 0);
		FCastleGridCursorTarget Target = AimDown(Mismatch->GetActorLocation(), 250.0);
		Test.TestTrue(TEXT("真实射线命中不合法地块且高亮校验拒绝"),
			Target.bValid && Target.AdjacentCell == Mismatch->GetGroundCell().Coordinate
			&& !Island->CanPlaceCardAtIslandCursor(Target, SelectedSlot));
		Target = AimDown(Match->GetActorLocation(), 250.0);
		if (!Test.TestTrue(TEXT("控制器准星命中合法微缩地块"),
			Target.bValid && Target.AdjacentCell == Match->GetGroundCell().Coordinate
			&& Island->CanPlaceCardAtIslandCursor(Target, SelectedSlot))) { return true; }
		// 与世界主交互一致，经典模式使用右键，设计模式使用左键。
		if (!PressKey(Controller->GetPrimaryPlaceInteractKey())) { return true; }
		Test.TestEqual(TEXT("左键建造消耗一张原卡"), Backpack->GetItemCount() + Backpack->GetTempItemCount(), InitialCards - 1);
		const auto MatchingCell = Target.AdjacentCell;
		const auto* Room = Island->MiniRooms.FindRef(MatchingCell).Get();
		if (!Test.TestNotNull(TEXT("已放房间生成独立六面查询盒"), Room)) { return true; }
		Target = AimDown(Room->GetComponentLocation(), Island->MiniLayerHeight);
		if (!Test.TestTrue(TEXT("控制器准星命中已放房间顶面并定位上一层"),
			Target.bValid && Target.AdjacentCell == MatchingCell + FIntVector(0, 0, 1))) { return true; }
		const int32 OriginalCount = Backpack->GetItemCount();
		int32 SlotIndex = INDEX_NONE;
		if (!Test.TestTrue(TEXT("添加同牌验证垂直叠放"), Backpack->TryAddItem(Card, SlotIndex, Reason)
			&& Island->TryPlaceBackpackAtIslandCursor(Target, SlotIndex, Reason))) { return true; }
		if (!PressKey(Controller->GetDemolishKey())) { return true; }
		Test.TestFalse(TEXT("右键撤回从微缩棋盘移除叠放房间"), Island->MiniRooms.Contains(Target.AdjacentCell));
		FCastleBuildingDescriptor Returned;
		Test.TestTrue(TEXT("撤回叠放房间后原卡还回手牌"),
			Backpack->TryGetItemAt(OriginalCount, Returned) && IsSameGambleCard(Returned, Card));
		const FVector RouletteCenter = Island->Roulette->GetComponentLocation();
		FHitResult Hit;
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleGambleAcceptanceAutomation), false, Controller->GetPawn());
		World->LineTraceSingleByChannel(Hit, RouletteCenter - Island->GetActorForwardVector() * 300.0, RouletteCenter, ECC_Visibility, Query);
		const FCastleSkyIslandInteractContext HitContext{Island, Controller, Hit};
		if (!Test.TestTrue(TEXT("真实射线命中轮盘并开启 WBP"), Island->Roulette->ExecuteInteract(HitContext, Reason)))
		{
			Test.AddError(Reason.ToString());
			return true;
		}
		Test.TestTrue(TEXT("轮盘显示鼠标并锁定飞行视角"), Controller->bShowMouseCursor && Controller->IsLookInputIgnored());
		Test.TestNull(TEXT("轮盘模态期间控制器暂停空岛建造输入"), Controller->GetInteractiveSkyIsland());
		if (auto* Viewport = UGameViewportSubsystem::Get())
		{
			const FGameViewportWidgetSlot Slot = Viewport->GetWidgetSlot(Island->RoulettePanel);
			Test.TestTrue(TEXT("轮盘槽位锚点和对齐保持在右侧居中"),
				Slot.Anchors.Minimum.Equals(FVector2D(1.0, 0.5)) && Slot.Anchors.Maximum.Equals(FVector2D(1.0, 0.5))
				&& Slot.Alignment.Equals(FVector2D(1.0, 0.5)));
			const auto VerifyHUD = [this, World, Viewport](TSubclassOf<UUserWidget> Class, FVector2D Anchor)
			{
				TArray<UUserWidget*> Widgets;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, Class, true);
				if (!Test.TestEqual(TEXT("每种道具 HUD 只挂载一次"), Widgets.Num(), 1)) { return; }
				const FAnchors Anchors = Viewport->GetWidgetSlot(Widgets[0]).Anchors;
				Test.TestTrue(Class->GetName() + TEXT(" 锚点"), Anchors.Minimum.Equals(Anchor) && Anchors.Maximum.Equals(Anchor));
			};
			VerifyHUD(UCastleTempBuildBarWidget::StaticClass(), FVector2D(0.0, 0.58));
			VerifyHUD(UCastleBadgeBarWidget::StaticClass(), FVector2D(0.0, 0.12));
			VerifyHUD(UCastleBadgeReplaceModalWidget::StaticClass(), FVector2D(0.5, 0.5));
		}
		else { Test.AddError(TEXT("轮盘槽位验证缺少视口子系统。")); }
		Test.TestFalse(TEXT("轮盘打开时其它机台受模态门控"), Island->CanUseMachine(HitContext, Reason));
		if (!Test.TestTrue(TEXT("关闭轮盘后归还飞行输入"), Island->TryCloseRoulettePanel(Reason)
			&& !Controller->bShowMouseCursor && !Controller->IsMoveInputIgnored() && !Controller->IsLookInputIgnored())) { return true; }
		Test.TestEqual(TEXT("关闭后保留一张微缩赌注"), Island->GetMiniBoard().GetPlacements().Num(), 1);

		// 测试资产副本只用于确定性地跑到两种弹窗档位，正式 DA 和当前关数保持原样。
		Island->Config = DuplicateObject(Island->Config.Get(), Island);
		const auto Group = Island->Config->RoomGroups.FindByPredicate([](const UCastleRoomGroupDataAsset* Entry)
		{
			return Entry && Entry->GetFName() == TEXT("DA_RoomGroup_04_CustomNormalRoom");
		});
		if (!Test.TestNotNull(TEXT("获取可打开万能卡的正式房间组"), Group)) { return true; }
		const TObjectPtr<UCastleRoomGroupDataAsset> WildcardGroup = *Group;
		Island->Config->RoomGroups = {WildcardGroup};
		Island->Config->SlotWeights = {0, 0, 0, 1, 0};
		if (!Test.TestTrue(TEXT("老虎机房间组档打开真实万能卡 WBP"), Island->TryPlaySlot(Reason)
			&& Island->Picker && Controller->bShowMouseCursor && Controller->IsLookInputIgnored())) { Test.AddError(Reason.ToString()); return true; }
		Test.TestFalse(TEXT("万能卡模态阻止下一次互动"), Island->CanUseMachine(HitContext, Reason));
		auto* Cancel = Cast<UButton>(Island->Picker->GetWidgetFromName(TEXT("CancelButton")));
		if (!Test.TestNotNull(TEXT("万能卡取消按钮"), Cancel)) { return true; }
		Cancel->OnClicked.Broadcast();
		Test.TestTrue(TEXT("万能卡结束后释放鼠标和飞行输入"), !Island->Picker && !Controller->bShowMouseCursor && !Controller->IsMoveInputIgnored());
		// 再次投币确认同类正式卡包，设定牌面通过真实选牌按钮进入背包。
		const int32 BeforePick = Backpack->GetItemCount() + Backpack->GetTempItemCount();
		const int32 BeforePickGold = Island->Economy->GetGold();
		if (!Test.TestTrue(TEXT("再次投币打开选牌"), Island->TryPlaySlot(Reason) && Island->Picker)) { return true; }
		for (const FName Panel : {FName(TEXT("DefinitionChoices")), FName(TEXT("ColorChoices")), FName(TEXT("NumberChoices"))})
		{
			const auto* Choices = Cast<UPanelWidget>(Island->Picker->GetWidgetFromName(Panel));
			auto* Choice = Choices ? Cast<UButton>(Choices->GetChildAt(0)) : nullptr;
			if (!Test.TestNotNull(Panel.ToString(), Choice)) { return true; }
			Choice->OnClicked.Broadcast();
		}
		auto* Confirm = Cast<UButton>(Island->Picker->GetWidgetFromName(TEXT("ConfirmButton")));
		if (!Test.TestNotNull(TEXT("万能卡确认按钮"), Confirm)) { return true; }
		Confirm->OnClicked.Broadcast();
		Test.TestNull(TEXT("确认正式卡后关闭选牌窗"), Island->Picker.Get());
		Test.TestEqual(TEXT("选牌确认只提交一次投币费"), Island->Economy->GetGold(), BeforePickGold - Island->Config->SlotMachineCost);
		Test.TestEqual(TEXT("正式普通房卡到账"), Backpack->GetItemCount() + Backpack->GetTempItemCount(), BeforePick + 1);

		Island->Config->SlotWeights = {0, 0, 0, 0, 1};
		while (Island->Badges->GetBadges().Num() < UCastleBadgeSubsystem::MaxBadges)
		{
			if (!Test.TestTrue(TEXT("老虎机徽章档写入正式徽章槽"), Island->TryPlaySlot(Reason))) { Test.AddError(Reason.ToString()); return true; }
		}
		FCastleBadgeDefinition Pending;
		Test.TestTrue(TEXT("老虎机满槽后弹替换窗"), Island->TryPlaySlot(Reason)
			&& Island->Badges->TryGetPendingBadge(Pending) && Controller->bShowMouseCursor && Controller->IsLookInputIgnored());
		Test.TestFalse(TEXT("徽章等待替换期间拒绝再次投币"), Island->TryPlaySlot(Reason));
		Island->Badges->DiscardPendingBadge();
		Test.TestTrue(TEXT("徽章弹窗结束后恢复飞行"), !Controller->bShowMouseCursor && !Controller->IsMoveInputIgnored());

		Backpack = Island->GetBackpack();
		CardsBeforeDeparture = Backpack->GetItemCount() + Backpack->GetTempItemCount() + 1;
		Island->OnDepartureRequested.Broadcast();
		bDeparting = true;
		return false;
	}

private:
	FAutomationTestBase& Test;
	TWeakObjectPtr<UCastleBackpackComponent> Backpack;
	TArray<TWeakObjectPtr<const ACastleGroundTileActor>> Tiles;
	FVector ReturnLocation;
	int32 OriginalStep = 0;
	int32 CardsBeforeDeparture = 0;
	bool bDeparting = false;
	double Deadline = FPlatformTime::Seconds() + 50.0;
};

class FCastleStopGamblePIE : public IAutomationLatentCommand
{
public:
	explicit FCastleStopGamblePIE(FAutomationTestBase& InTest) : Test(InTest) {}
	virtual bool Update() override
	{
		if (!FindGamblePIE()) { return true; }
		if (!bRequested) { GEditor->RequestEndPlayMap(); bRequested = true; }
		if (FPlatformTime::Seconds() < Deadline) { return false; }
		Test.AddError(TEXT("停止赌博空岛 MainMap PIE 超时。"));
		return true;
	}
private:
	FAutomationTestBase& Test;
	bool bRequested = false;
	double Deadline = FPlatformTime::Seconds() + 65.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleGambleAcceptanceTest, "MovingCastle.SkyIslands.Gamble.MainMapAcceptance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleGambleAcceptanceTest::RunTest(const FString& Parameters)
{
	// 离屏编辑器不依赖通用地图等待器的窗口就绪状态，由当前 PIE 世界的导演判定就绪。
	const FString Map = FPackageName::LongPackageNameToFilename(TEXT("/Game/MovingCastle/Maps/MainMap"),
		FPackageName::GetMapPackageExtension());
	if (!TestTrue(TEXT("加载赌博验收用的 MainMap"), FEditorFileUtils::LoadMap(Map, false, false))) { return false; }
	auto* Settings = DuplicateObject(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
	Settings->SetPlayNetMode(PIE_Standalone);
	Settings->SetRunUnderOneProcess(true);
	Settings->SetPlayNumberOfClients(1);
	FRequestPlaySessionParams Request;
	Request.EditorPlaySettings = Settings;
	Request.bAllowOnlineSubsystem = false;
	GEditor->RequestPlaySession(Request);
	ADD_LATENT_AUTOMATION_COMMAND(FCastleGambleAcceptanceScenario(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FCastleStopGamblePIE(*this));
	return true;
}

#endif
