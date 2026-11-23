#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleGambleSkyIsland.h"
#include "CastleGambleRouletteWidget.h"
#include "CastleGambleAcceptanceIsland.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastleBackpackComponent.h"
#include "CastleBuildingDefinition.h"
#include "CastleEconomyComponent.h"
#include "CastleMainTowerComponent.h"
#include "CastlePlayerController.h"
#include "Components/Button.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Items/CastleBadgeDataAsset.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Items/CastleItemGrantLibrary.h"
#include "Items/CastlePendingRoomReward.h"
#include "Items/CastleRoomGroupDataAsset.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "UI/Items/CastleWildcardRoomPickerWidget.h"

namespace
{
	UCastleGambleSkyIslandDataAsset* LoadGambleConfig()
	{
		return LoadObject<UCastleGambleSkyIslandDataAsset>(nullptr,
			TEXT("/Game/MovingCastle/Data/SkyIslands/DA_GambleSkyIsland.DA_GambleSkyIsland"));
	}

	bool IsSameCard(const FCastleBuildingDescriptor& A, const FCastleBuildingDescriptor& B)
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
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("隔离游戏世界"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	FText Reason;
	auto* Config = LoadGambleConfig();
	auto* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_NormalRoom.DA_NormalRoom"));
	if (!TestNotNull(TEXT("赌博岛正式 DA"), Config) || !TestNotNull(TEXT("正式普通房"), Definition)
		|| !TestTrue(TEXT("赌博岛配置合法"), Config->Validate(Reason))) { return false; }
	TestEqual(TEXT("八个房间组"), Config->RoomGroups.Num(), 8);
	TestEqual(TEXT("七个徽章"), Config->Badges.Num(), 7);

	auto* Island = World->SpawnActor<ACastleGambleSkyIsland>();
	if (!TestNotNull(TEXT("原生赌博岛"), Island)) { return false; }
	// 只为隔离机台接上真实组件，生产入口从当前游戏世界解析这几项依赖。
	Island->Config = DuplicateObject(Config, Island);
	Island->Backpack = NewObject<UCastleBackpackComponent>(Island);
	Island->Economy = NewObject<UCastleEconomyComponent>(Island);
	Island->Tower = NewObject<UCastleMainTowerComponent>(Island);
	Island->Badges = NewObject<UCastleBadgeSubsystem>(NewObject<UGameInstance>());
	Island->MiniCellSize = 40.0;
	UCastleBackpackComponent& Backpack = *Island->Backpack;
	FCastleBuildingDescriptor RedOne, RedTwo, BlueOne, GreenThree;
	if (!TestTrue(TEXT("生成正式测试房间"),
		Definition->CreateDescriptor(1, ECastleTileColor::Red, 1, RedOne, Reason)
		&& Definition->CreateDescriptor(1, ECastleTileColor::Red, 2, RedTwo, Reason)
		&& Definition->CreateDescriptor(1, ECastleTileColor::Blue, 1, BlueOne, Reason)
		&& Definition->CreateDescriptor(1, ECastleTileColor::Green, 3, GreenThree, Reason))) { return false; }
	const FCastleBuildingDescriptor Initial[] = {RedOne, GreenThree, RedTwo, BlueOne};
	if (!TestTrue(TEXT("填入测试牌"), Backpack.TryAddItemsAllowOverflow(Initial, Reason))) { return false; }
	TestFalse(TEXT("空微缩区拒绝开转"), Island->TrySpinRoulette(ECastleRouletteColor::Black, Reason));
	TestFalse(TEXT("首块不能放在非原点"), Island->TryPlaceCard(0, FIntVector(1, 0, 0), Reason));
	if (!TestTrue(TEXT("首块放在原点"), Island->TryPlaceCard(0, FIntVector::ZeroValue, Reason))) { return false; }
	TestFalse(TEXT("异色且异号被拒"), Island->TryPlaceCard(0, FIntVector(1, 0, 0), Reason));
	TestEqual(TEXT("拒绝后仍有三张"), Backpack.GetItemCount(), 3);
	TestTrue(TEXT("同色横向相邻"), Island->TryPlaceCard(1, FIntVector(1, 0, 0), Reason));
	TestTrue(TEXT("同数字垂直相邻"), Island->TryPlaceCard(1, FIntVector(0, 0, 1), Reason));
	TestTrue(TEXT("撤回最后一张"), Island->TryReturnLastCard(Reason));
	FCastleBuildingDescriptor Returned;
	TestTrue(TEXT("撤回保留完整牌面"), Backpack.TryGetItemAt(1, Returned) && IsSameCard(Returned, BlueOne));
	TestFalse(TEXT("拦截复制张数溢出"), Island->MiniBoard.CanSettle(MAX_int32, Reason));

	Island->EndPlay(EEndPlayReason::Destroyed);
	TestEqual(TEXT("EndPlay 清空未下注暂存"), Island->MiniBoard.GetPlacements().Num(), 0);
	TestEqual(TEXT("EndPlay 全量退回"), Backpack.GetItemCount(), 4);
	TestTrue(TEXT("离岛退回第一张原牌"), Backpack.TryGetItemAt(2, Returned) && IsSameCard(Returned, RedOne));
	TestTrue(TEXT("离岛退回第二张原牌"), Backpack.TryGetItemAt(3, Returned) && IsSameCard(Returned, RedTwo));

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
		for (const FCastleBuildingDescriptor& Card : Paid) { TestTrue(TEXT("复制保留完整牌面"), IsSameCard(Card, RedOne)); }
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
	Island->Config->SlotWeights = {0, 0, 0, 12, 0};
	for (const auto& Group : Config->RoomGroups)
	{
		if (Group->GroupType != ECastleRoomGroupType::ThreeNumberFourArchers) { continue; }
		Island->Config->RoomGroups = {Group};
		TestTrue(TEXT("房间组档发放正式箭塔牌"), Island->TryPlaySlot(Reason));
		TestEqual(TEXT("三张箭塔含临时栏溢出"), Island->Backpack->GetItemCount() + Island->Backpack->GetTempItemCount(), 3);
	}
	TestEqual(TEXT("房间组档花十金"), Island->Economy->GetGold(), 130);

	Island->Tower->MaxHealth = 400;
	Island->Tower->MaxBalloons = 2;
	Island->Tower->HealthPerBalloon = 200;
	TestTrue(TEXT("主塔初始化两球"), Island->Tower->InitializeHealth());
	TestTrue(TEXT("回收一球"), Island->TryRecycleBalloon(Reason));
	TestEqual(TEXT("扣一球血量"), Island->Tower->CurrentHealth, 200.0f);
	TestEqual(TEXT("发三十金"), Island->Economy->GetGold(), 160);
	AddExpectedError(TEXT("回收后需要保留至少一个存活气球"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("最后一球拒绝回收"), Island->TryRecycleBalloon(Reason));
	TestTrue(TEXT("最后一球仍存活且未当作受击"), Island->Tower->IsAlive() && Island->Tower->AttackCount == 0);
	TestEqual(TEXT("拒绝时不发币"), Island->Economy->GetGold(), 160);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleGambleLuckAutomationTest, "MovingCastle.SkyIslands.Gamble.LuckAndAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleGambleLuckAutomationTest::RunTest(const FString& Parameters)
{
	auto* Config = LoadGambleConfig();
	if (!TestNotNull(TEXT("赌博空岛 DA"), Config)) { return false; }
	auto* BadgeAsset = LoadObject<UCastleBadgeDataAsset>(nullptr,
		TEXT("/Game/MovingCastle/Data/Items/DA_Badge_GambleLuck.DA_Badge_GambleLuck"));
	if (!TestNotNull(TEXT("正式幸运徽章"), BadgeAsset)) { return false; }
	auto* BadgeSource = NewObject<UCastleBadgeSubsystem>(NewObject<UGameInstance>());
	bool bReplace = false;
	FText Reason;
	const double Baseline = BadgeSource->GetGambleLuckBonus();
	if (!TestTrue(TEXT("获得一枚正式幸运徽章"), BadgeSource->TryAcquireBadge(BadgeAsset->Badge, bReplace, Reason))) { return false; }
	const double Lucky = BadgeSource->GetGambleLuckBonus();
	TestTrue(TEXT("幸运值严格增加"), Lucky > Baseline);
	int32 RouletteHits[2] = {};
	int32 SlotHits[2] = {};
	int32 RouletteFrequency[3] = {};
	FRandomStream Random(1352227);
	constexpr int32 Samples = 100000;
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const double Sample = Random.GetUnsignedInt() / 4294967296.0;
		for (int32 HasBadge = 0; HasBadge < 2; ++HasBadge)
		{
			ECastleRouletteColor Color;
			ECastleSlotPrize Prize;
			int32 Slot = INDEX_NONE;
			const double Luck = HasBadge ? Lucky : Baseline;
			if (!Config->TryRollRoulette(ECastleRouletteColor::Black, Luck, Sample, Color, Slot)
				|| !Config->TryRollSlot(Luck, Sample, Prize)) { AddError(TEXT("有效样本落在概率区间外")); return false; }
			RouletteHits[HasBadge] += Color == ECastleRouletteColor::Black;
			SlotHits[HasBadge] += Prize != ECastleSlotPrize::None;
			if (!HasBadge) { ++RouletteFrequency[static_cast<uint8>(Color)]; }
		}
	}
	TestTrue(TEXT("无徽章黑色接近 18/37"), FMath::Abs(RouletteFrequency[0] / double(Samples) - 18.0 / 37.0) < 0.005);
	TestTrue(TEXT("无徽章红色接近 18/37"), FMath::Abs(RouletteFrequency[1] / double(Samples) - 18.0 / 37.0) < 0.005);
	TestTrue(TEXT("无徽章绿色接近 1/37"), FMath::Abs(RouletteFrequency[2] / double(Samples) - 1.0 / 37.0) < 0.005);
	TestTrue(TEXT("无徽章老虎机中奖率接近 60%"), FMath::Abs(SlotHits[0] / double(Samples) - 0.60) < 0.005);
	TestTrue(TEXT("幸运徽章使轮盘押中率严格提高"), RouletteHits[1] > RouletteHits[0]);
	TestTrue(TEXT("幸运徽章使老虎机中奖率严格提高"), SlotHits[1] > SlotHits[0]);
	// 逐项使用真实奖池，验证每个建筑定义和候选规格都能交给正式发放入口。
	int32 DirectGroups = 0;
	int32 ChoiceGroups = 0;
	for (const UCastleRoomGroupDataAsset* Group : Config->RoomGroups)
	{
		auto* Backpack = NewObject<UCastleBackpackComponent>();
		UCastlePendingRoomReward* Pending = nullptr;
		if (!TestTrue(FString::Printf(TEXT("发放房间组 %s"), *GetNameSafe(Group)),
			UCastleItemGrantLibrary::TryGrantRoomGroup(Group, Backpack, Config->RoomDefinitions, Random, Pending, Reason))) { continue; }
		if (Pending)
		{
			++ChoiceGroups;
			FCastleWildcardRoomOptions Options;
			TestTrue(TEXT("待领奖品可开启选择并提供正式候选"), Pending->TryBeginClaim(Options, Reason)
				&& !Options.Definitions.IsEmpty() && !Options.Colors.IsEmpty() && !Options.Numbers.IsEmpty());
		}
		else
		{
			++DirectGroups;
			TestTrue(TEXT("确定房间组已真实入包"), Backpack->GetItemCount() > 0);
		}
	}
	TestEqual(TEXT("房间奖池的每个条目均验证"), DirectGroups + ChoiceGroups, 8);
	TestTrue(TEXT("房间奖池同时包含确定牌和选择牌"), DirectGroups > 0 && ChoiceGroups > 0);
	BadgeSource->ClearAllBadgesForTesting();
	for (int32 Index = 0; Index < Config->Badges.Num(); ++Index)
	{
		TestTrue(TEXT("奖池徽章可沿正式发放入口处理"),
			UCastleItemGrantLibrary::TryGrantBadge(Config->Badges[Index], BadgeSource, bReplace, Reason));
		TestEqual(TEXT("前六个入槽，最后一个等待替换"), bReplace, Index == 6);
	}
	TestEqual(TEXT("徽章系统保留六个已持有槽位"), BadgeSource->GetBadges().Num(), 6);
	FCastleBadgeDefinition PendingBadge;
	TestTrue(TEXT("满槽时保留待替换徽章"), BadgeSource->TryGetPendingBadge(PendingBadge));

	UClass* Panel = LoadClass<UCastleGambleRouletteWidget>(nullptr,
		TEXT("/Game/MovingCastle/UI/SkyIslands/Gamble/WBP_GambleRoulettePanel.WBP_GambleRoulettePanel_C"));
	auto* GeneratedPanel = Cast<UWidgetBlueprintGeneratedClass>(Panel);
	if (!TestNotNull(TEXT("保存后的轮盘面板为原生基类的 Widget Blueprint"), GeneratedPanel)) { return false; }
	const UWidgetTree* Tree = GeneratedPanel->GetWidgetTreeArchetype();
	if (!TestNotNull(TEXT("占位面板含持久化控件树"), Tree)) { return false; }
	for (const FName Name : {FName(TEXT("CardOptions")), FName(TEXT("CellOptions")), FName(TEXT("SpinButton")), FName(TEXT("CloseButton"))})
	{
		TestNotNull(FString::Printf(TEXT("保存后的控件树包含 %s"), *Name.ToString()), Tree->FindWidget(Name));
	}
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
			Test.TestTrue(TEXT("离岛返回原来的飞行位置"), Controller->GetPawn()->GetActorLocation().Equals(ReturnLocation, 5.0));
			Test.TestTrue(TEXT("离岛退回 1 张未下注原牌"), Backpack.IsValid()
				&& Backpack->GetItemCount() + Backpack->GetTempItemCount() == CardsBeforeDeparture);
			Test.TestTrue(TEXT("验收过程保留本局关数"), Director->GetClearedSteps() == OriginalStep);
			return true;
		}
		ReturnLocation = Controller->GetPawn()->GetActorLocation();
		OriginalStep = Director->GetClearedSteps();
		FOutputDeviceNull Output;
		if (!Test.TestTrue(TEXT("MainMap 识别赌博岛验收命令"), IConsoleManager::Get().ProcessUserConsoleInput(
			TEXT("CastleTestGambleSkyIsland"), Output, World))) { return true; }
		TActorIterator<ACastleGambleAcceptanceIsland> IslandIt(World);
		if (!Test.TestTrue(TEXT("命令生成实际赌博空岛"), static_cast<bool>(IslandIt))) { return true; }
		auto* Island = *IslandIt;
		Test.TestTrue(TEXT("玩家传送至赌博岛到达锚点"), Controller->GetPawn()->GetActorLocation().Equals(
			Island->GetPlayerArrivalTransform().GetLocation(), 5.0));
		const FVector RouletteCenter = Island->Roulette->GetComponentLocation();
		FHitResult Hit;
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleGambleAcceptanceAutomation), false, Controller->GetPawn());
		World->LineTraceSingleByChannel(Hit, RouletteCenter - Island->GetActorForwardVector() * 300.0, RouletteCenter, ECC_Visibility, Query);
		FText Reason;
		const FCastleSkyIslandInteractContext HitContext{Island, Controller, Hit};
		if (!Test.TestTrue(TEXT("真实射线命中轮盘并开启 WBP"), Island->Roulette->ExecuteInteract(HitContext, Reason)))
		{
			Test.AddError(Reason.ToString());
			return true;
		}
		Test.TestTrue(TEXT("轮盘显示鼠标并锁定飞行视角"), Controller->bShowMouseCursor && Controller->IsLookInputIgnored());
		Test.TestFalse(TEXT("轮盘打开时其它机台受模态门控"), Island->CanUseMachine(HitContext, Reason));
		if (!Test.TestTrue(TEXT("关闭轮盘后归还飞行输入"), Island->TryCloseRoulettePanel(Reason)
			&& !Controller->bShowMouseCursor && !Controller->IsMoveInputIgnored() && !Controller->IsLookInputIgnored())) { return true; }

		// 测试资产副本只用于确定性地跑到两种弹窗档位，正式 DA 和当前关数保持原样。
		Island->Config = DuplicateObject(Island->Config.Get(), Island);
		const auto Group = Island->Config->RoomGroups.FindByPredicate([](const UCastleRoomGroupDataAsset* Entry)
		{
			return Entry->GroupType == ECastleRoomGroupType::CustomNormalRoom;
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
		CardsBeforeDeparture = Backpack->GetItemCount() + Backpack->GetTempItemCount();
		if (!Test.TestTrue(TEXT("离岛前保留一张尚未下注房间"), Island->TryPlaceCard(0, FIntVector::ZeroValue, Reason))) { return true; }
		Island->OnDepartureRequested.Broadcast();
		bDeparting = true;
		return false;
	}

private:
	FAutomationTestBase& Test;
	TWeakObjectPtr<UCastleBackpackComponent> Backpack;
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
	if (!AutomationOpenMap(TEXT("/Game/MovingCastle/Maps/MainMap"), true)) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FCastleGambleAcceptanceScenario(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FCastleStopGamblePIE(*this));
	return true;
}

#endif
