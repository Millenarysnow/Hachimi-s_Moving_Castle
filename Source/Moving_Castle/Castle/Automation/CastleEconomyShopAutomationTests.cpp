// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CastleTowerDispatchConfigDataAsset.h"
#include "Castle/UI/Director/CastleTowerUpgradeRewardWidget.h"
#include "Castle/UI/Items/CastleWildcardRoomPickerWidget.h"

#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildings.h"
#include "CastleBuildingTestUtilities.h"
#include "CastleCardPricing.h"
#include "CastleEconomyComponent.h"
#include "CastleShopComponent.h"
#include "CastleMainTowerComponent.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "MassEntityConfigAsset.h"
#include "Engine/StaticMesh.h"
#include "Blueprint/UserWidget.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include <limits>

/** 验证收入、支出、免费操作、非法负数和溢出都具有明确且无副作用的结果。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleEconomyTest, "MovingCastle.Logic.Economy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleEconomyTest::RunTest(const FString& Parameters)
{
	UCastleEconomyComponent* Economy = NewObject<UCastleEconomyComponent>();
	FText Reason;
	TestTrue(TEXT("初始账户可以收到金币"), Economy->TryAddGold(100, Reason));
	TestTrue(TEXT("正常消费成功"), Economy->TrySpendGold(30, Reason));
	TestEqual(TEXT("余额准确"), Economy->GetGold(), 70);
	TestFalse(TEXT("不足余额不能扣款"), Economy->TrySpendGold(71, Reason));
	TestFalse(TEXT("负支出不能变成收入"), Economy->TrySpendGold(-1, Reason));
	TestFalse(TEXT("负收入不能变成支出"), Economy->TryAddGold(-1, Reason));
	TestFalse(TEXT("整数溢出必须拒绝"), Economy->TryAddGold(MAX_int32, Reason));
	TestEqual(TEXT("失败路径均保留原余额"), Economy->GetGold(), 70);
	TestTrue(TEXT("免费操作允许零余额扣款"), Economy->TrySpendGold(70, Reason));
	TestTrue(TEXT("零金额支出成功"), Economy->TrySpendGold(0, Reason));
	TestEqual(TEXT("账户不能透支"), Economy->GetGold(), 0);
	return true;
}

/** 验证接口报价及商品、余额、背包之间的一致性，包括满包、售罄和总上限分支。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShopTest, "MovingCastle.Logic.ShopTransactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShopTest::RunTest(const FString& Parameters)
{
	ACastleBoard* Board = NewObject<ACastleBoard>();
	CastleBuildingTests::ConfigureBoard(Board);
	UCastleEconomyComponent* Economy = NewObject<UCastleEconomyComponent>();
	UCastleShopComponent* Shop = NewObject<UCastleShopComponent>();
	FText Reason;
	TestTrue(TEXT("商店显式绑定依赖"), Shop->InitializeShop(Economy, Board, Reason));
	TestTrue(TEXT("首次打开自动出六张牌"), Shop->TryOpenShop(Reason));
	const TArray<FCastleShopOffer> FirstOffers = Shop->GetOffers();
	if (!TestEqual(TEXT("固定六个槽位"), FirstOffers.Num(), 6))
	{
		return false;
	}
	TestEqual(TEXT("首次打开不扣金币"), Economy->GetGold(), 0);
	TestFalse(TEXT("没钱不能购买"), Shop->TryBuyOffer(0, Reason));
	TestFalse(TEXT("没钱不能刷新"), Shop->TryRefreshOffers(Reason));
	TestEqual(TEXT("失败购买不发卡"), Board->PrototypeBackpack->GetItemCount(), 0);
	TestEqual(TEXT("失败刷新保留报价"), Shop->GetOffers()[0].Price, FirstOffers[0].Price);
	TestFalse(TEXT("失败购买不标售罄"), Shop->GetOffers()[0].bSold);

	Economy->TryAddGold(1000, Reason);
	TestTrue(TEXT("有钱后可以买下商品"), Shop->TryBuyOffer(0, Reason));
	TestEqual(TEXT("只按展示价格扣款"), Economy->GetGold(), 1000 - FirstOffers[0].Price);
	TestTrue(TEXT("购买槽位标记售罄"), Shop->GetOffers()[0].bSold);
	TestFalse(TEXT("同一槽位不能重复购买"), Shop->TryBuyOffer(0, Reason));
	FCastleBuildingDescriptor Purchased;
	TestTrue(TEXT("购买所得卡牌进入背包"), Board->PrototypeBackpack->TryGetItemAt(0, Purchased));
	TestTrue(TEXT("购买的卡牌和商品完全相同"), Purchased.Definition == FirstOffers[0].Descriptor.Definition
		&& Purchased.Number == FirstOffers[0].Descriptor.Number && Purchased.Color == FirstOffers[0].Descriptor.Color);

	Shop->CloseShop();
	TestFalse(TEXT("商店关闭时拒绝交易"), Shop->TryBuyOffer(1, Reason));
	TestTrue(TEXT("可以重新打开商店"), Shop->TryOpenShop(Reason));
	TestTrue(TEXT("重开保留售罄槽位"), Shop->GetOffers()[0].bSold);
	TestEqual(TEXT("重开不修改商品"), Shop->GetOffers()[1].Descriptor.Number, FirstOffers[1].Descriptor.Number);

	Board->PrototypeBackpack->TrySetCapacity(1, Reason);
	const int32 GoldBeforeFull = Economy->GetGold();
	TestFalse(TEXT("满包拒绝购买"), Shop->TryBuyOffer(1, Reason));
	TestEqual(TEXT("满包不扣款"), Economy->GetGold(), GoldBeforeFull);
	TestFalse(TEXT("满包不移走商品"), Shop->GetOffers()[1].bSold);
	int32 SellPrice = 0;
	TestTrue(TEXT("出售报价来自接口"), Shop->TryGetSellPrice(0, SellPrice, Reason));
	TestTrue(TEXT("可出售背包卡牌"), Shop->TrySellBackpackItem(0, Reason));
	TestEqual(TEXT("出售获得准确金币"), Economy->GetGold(), GoldBeforeFull + SellPrice);
	TestEqual(TEXT("出售释放背包容量"), Board->PrototypeBackpack->GetItemCount(), 0);
	TestFalse(TEXT("无效槽位不能重复卖"), Shop->TrySellBackpackItem(0, Reason));

	// 全局房间额度和背包槽位容量是两套限制，两张一级卡可先占满测试设定的房间额度。
	int32 AddedIndex = INDEX_NONE;
	Board->PrototypeBackpack->TrySetCapacity(10, Reason);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		TestTrue(TEXT("向建筑栏添加一张一级卡"), Board->PrototypeBackpack->TryAddBuilding(
			Board->FindBuildingDefinition(ECastleBuildingType::ArcherTower), 1, ECastleTileColor::Red, 3, AddedIndex, Reason));
	}
	Shop->RoomLimit = 2;
	TestEqual(TEXT("库存两张一级卡计入全局房间总量"), Shop->GetOwnedRoomCount(), 2);
	TestFalse(TEXT("全局上限拒绝新购买"), Shop->TryBuyOffer(1, Reason));
	Shop->RoomLimit = 30;
	const int32 GoldBeforeRefresh = Economy->GetGold();
	TestTrue(TEXT("刷新成功"), Shop->TryRefreshOffers(Reason));
	TestEqual(TEXT("刷新扣除固定费用"), Economy->GetGold(), GoldBeforeRefresh - Shop->RefreshCost);
	for (const FCastleShopOffer& Offer : Shop->GetOffers())
	{
		TestFalse(TEXT("刷新后全部可购买"), Offer.bSold);
		TestTrue(TEXT("所有商品使用一级类"), Offer.Descriptor.Level == 1);
		TestTrue(TEXT("所有房间均为五色 1～9 数字"), Offer.Descriptor.Color >= ECastleTileColor::Red
			&& Offer.Descriptor.Color <= ECastleTileColor::Purple && Offer.Descriptor.Number >= 1 && Offer.Descriptor.Number <= 9);
		TestEqual(TEXT("所有一级房间使用统一买价"), Offer.Price, 5 + Offer.Descriptor.Number * 2);
	}
	// 即使卖出能释放容量，也必须先保证收款不会溢出，不能先删除卡牌再发现无法入账。
	Economy->TryAddGold(MAX_int32 - Economy->GetGold(), Reason);
	TestFalse(TEXT("金币已满时出售失败"), Shop->TrySellBackpackItem(0, Reason));
	TestEqual(TEXT("出售失败保留所有卡牌"), Board->PrototypeBackpack->GetItemCount(), 2);
	TestEqual(TEXT("出售失败保留余额"), Economy->GetGold(), MAX_int32);
	return true;
}

/** 购买后的随机卡必须能经过实际背包槽位入口变成建筑，不能停留在显示或接口层。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShopConstructionTest, "MovingCastle.Logic.ShopToConstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShopConstructionTest::RunTest(const FString& Parameters)
{
	// UE 5.8 的 CreateWorld 已经负责 InitializeNewWorld，初始化参数一次性传入，
	// 不能创建后再初始化一次，否则会重复生成同名 WorldSettings。
	const UWorld::InitializationValues WorldSettings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &WorldSettings);
	if (!TestNotNull(TEXT("创建独立建造测试世界"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	CastleBuildingTests::FScopedNativeBuildingModels TestModels(World);
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	if (!TestNotNull(TEXT("在真实 World 中生成棋盘"), Board))
	{
		return false;
	}
	Board->GridSize = FIntVector(1, 1, 3);
	UCastleEconomyComponent* Economy = NewObject<UCastleEconomyComponent>();
	UCastleShopComponent* Shop = NewObject<UCastleShopComponent>();
	Board->BuildingDefinitions = { CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower) };
	FText Reason;
	Economy->TryAddGold(100, Reason);
	if (!TestTrue(TEXT("商店初始化"), Shop->InitializeShop(Economy, Board, Reason))
		|| !TestTrue(TEXT("商店自动出牌"), Shop->TryOpenShop(Reason)))
	{
		return false;
	}
	const FCastleBuildingDescriptor Card = Shop->GetOffers()[0].Descriptor;
	FCastleGroundCell Cell;
	Cell.Color = Card.Color;
	Cell.Number = Card.Number;
	FCastleGroundGenerationResult Ground;
	Ground.Cells.Add(Cell);
	TestTrue(TEXT("棋盘接收与商品匹配的测试地面"), Board->ApplyGroundGenerationResult(Ground, Reason));
	if (!TestTrue(TEXT("购买随机卡牌"), Shop->TryBuyOffer(0, Reason)))
	{
		return false;
	}
	FCastleGridCursorTarget Target;
	Target.bValid = true;
	Target.Surface = ECastleGridCursorSurface::Ground;
	Target.Face = ECastleGridFace::Up;
	Target.AdjacentCell = Target.Cell + FIntVector(0, 0, 1);
	Shop->CloseShop();
	ACastleBuildingBase* Placed = Board->PlaceBackpackBuildingAtCursor(Target, 0, Reason);
	if (!TestNotNull(TEXT("购买牌能从实际背包槽位成功放置"), Placed))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestEqual(TEXT("放置消耗背包卡牌"), Board->PrototypeBackpack->GetItemCount(), 0);
	TestEqual(TEXT("场上新增一个占用格"), Board->GetOccupiedRoomCellCount(), 1);
	TestTrue(TEXT("Actor 保留购买的随机属性"), Placed->GetBuildingColor() == Card.Color && Placed->GetBuildingNumber() == Card.Number);
	TestEqual(TEXT("买牌到建造总量保持不变"), Shop->GetOwnedRoomCount(), 1);
	return true;
}

/** 配置错误及随机生成失败不得清空旧商品或扣款；相同种子可以复现同一批商品。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShopGenerationTest, "MovingCastle.Logic.ShopGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShopGenerationTest::RunTest(const FString& Parameters)
{
	ACastleBoard* Board = NewObject<ACastleBoard>();
	CastleBuildingTests::ConfigureBoard(Board);
	UCastleEconomyComponent* Economy = NewObject<UCastleEconomyComponent>();
	UCastleShopComponent* Shop = NewObject<UCastleShopComponent>();
	UCastleShopComponent* Twin = NewObject<UCastleShopComponent>();
	FText Reason;
	Shop->PricingStrategyClass = UObject::StaticClass();
	TestFalse(TEXT("不实现定价接口的类不可使用"), Shop->InitializeShop(Economy, Board, Reason));
	Shop->PricingStrategyClass = UCastleSimpleCardPricing::StaticClass();
	Shop->InitializeShop(Economy, Board, Reason);
	Twin->InitializeShop(Economy, Board, Reason);
	Shop->TryOpenShop(Reason);
	Twin->TryOpenShop(Reason);
	const TArray<FCastleShopOffer> Offers = Shop->GetOffers();
	const TArray<FCastleShopOffer> TwinOffers = Twin->GetOffers();
	if (Offers.Num() != 6 || TwinOffers.Num() != 6)
	{
		AddError(TEXT("商品生成未完成，不能继续比较。"));
		return false;
	}
	for (int32 Index = 0; Index < 6; ++Index)
	{
		TestTrue(TEXT("相同种子可复现完整商品"), Offers[Index].Descriptor.Definition == TwinOffers[Index].Descriptor.Definition
			&& Offers[Index].Descriptor.Color == TwinOffers[Index].Descriptor.Color
			&& Offers[Index].Descriptor.Number == TwinOffers[Index].Descriptor.Number
			&& Offers[Index].Price == TwinOffers[Index].Price);
	}
	Economy->TryAddGold(100, Reason);
	const auto OriginalDefinitions = Board->BuildingDefinitions;
	Board->BuildingDefinitions = { nullptr };
	TestFalse(TEXT("错误商品池不生成替代牌"), Shop->TryRefreshOffers(Reason));
	TestEqual(TEXT("生成失败不扣款"), Economy->GetGold(), 100);
	TestEqual(TEXT("生成失败保留整批旧商品"), Shop->GetOffers().Num(), 6);
	TestTrue(TEXT("旧商品描述未修改"), Shop->GetOffers()[0].Descriptor.Definition == Offers[0].Descriptor.Definition);
	Board->BuildingDefinitions = OriginalDefinitions;
	TestTrue(TEXT("修复商品池后刷新可成功"), Shop->TryRefreshOffers(Reason));
	Twin->TryRefreshOffers(Reason);
	TestEqual(TEXT("失败刷新不消耗随机流"), Shop->GetOffers()[0].Descriptor.Number, Twin->GetOffers()[0].Descriptor.Number);

	UCastleSimpleCardPricing* Pricing = NewObject<UCastleSimpleCardPricing>();
	FCastleBuildingDescriptor InvalidCard = Offers[0].Descriptor;
	InvalidCard.Definition = nullptr;
	int32 Price = 0;
	TestFalse(TEXT("无效卡牌明确拒绝定价"), ICastleCardPricing::Execute_TryCalculatePrice(Pricing, InvalidCard, ECastleCardPriceKind::Buy, Price, Reason));
	FCastleBuildingDescriptor Archer;
	Archer.Definition = Board->FindBuildingDefinition(ECastleBuildingType::ArcherTower);
	Archer.Color = ECastleTileColor::Blue;
	Archer.Number = 3;
	Archer.Level = 2;
	TestTrue(TEXT("有效卡牌可通过接口报价"), ICastleCardPricing::Execute_TryCalculatePrice(Pricing, Archer, ECastleCardPriceKind::Buy, Price, Reason));
	TestEqual(TEXT("数字和等级参与测试买价"), Price, 21);
	ICastleCardPricing::Execute_TryCalculatePrice(Pricing, Archer, ECastleCardPriceKind::Sell, Price, Reason);
	TestEqual(TEXT("卖价为买价一半向下取整"), Price, 10);
	return true;
}

/** 验证奖励免费换批复用事务且不扣款，非法房间上限和无效配置均显式拒绝。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorFoundationTest, "MovingCastle.Logic.DirectorFoundation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDirectorFoundationTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("房间上限设置失败：收到无效值"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("主塔回血失败"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("字段 NodeMesh 值 None 无效"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("字段 NodeCountMin/NodeCountMax"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("字段 NodeLabelWidgetClass 值 None 无效"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("奖励换批失败"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("商店奖励初始化失败"), EAutomationExpectedErrorFlags::Contains, 1);
	ACastleBoard* Board = NewObject<ACastleBoard>();
	CastleBuildingTests::ConfigureBoard(Board);
	UCastleEconomyComponent* Economy = NewObject<UCastleEconomyComponent>();
	UCastleShopComponent* Shop = NewObject<UCastleShopComponent>();
	FText Reason;
	TestTrue(TEXT("商店初始化"), Shop->InitializeShop(Economy, Board, Reason));
	TestTrue(TEXT("奖励免费整批换牌成功"), Shop->TryRefreshOffersForReward(Reason));
	TestFalse(TEXT("奖励换批本身不擅自打开商店"), Shop->IsShopOpen());
	TestTrue(TEXT("商店打开"), Shop->TryOpenShop(Reason));
	TestEqual(TEXT("奖励换牌不扣除金币"), Economy->GetGold(), 0);
	TestEqual(TEXT("奖励换牌生成完整六件商品"), Shop->GetOffers().Num(), 6);
	// 对照商店推进完全相同次数；失败后重试必须与它逐槽一致，而非只保留旧数量。
	UCastleShopComponent* Twin = NewObject<UCastleShopComponent>();
	Twin->InitializeShop(Economy, Board, Reason);
	Twin->TryRefreshOffersForReward(Reason);
	const TArray<FCastleShopOffer> PreviousOffers = Shop->GetOffers();
	const auto PreviousDefinitions = Board->BuildingDefinitions;
	Board->BuildingDefinitions.Reset();
	TestFalse(TEXT("无商品池时奖励换批拒绝"), Shop->TryRefreshOffersForReward(Reason));
	TestEqual(TEXT("失败仍保留完整旧商品"), Shop->GetOffers().Num(), PreviousOffers.Num());
	for (int32 Index = 0; Index < PreviousOffers.Num(); ++Index)
	{
		TestEqual(TEXT("失败保留每槽数字"), Shop->GetOffers()[Index].Descriptor.Number, PreviousOffers[Index].Descriptor.Number);
		TestEqual(TEXT("失败保留每槽颜色"), Shop->GetOffers()[Index].Descriptor.Color, PreviousOffers[Index].Descriptor.Color);
		TestEqual(TEXT("失败保留每槽报价"), Shop->GetOffers()[Index].Price, PreviousOffers[Index].Price);
	}
	Board->BuildingDefinitions = PreviousDefinitions;
	TestTrue(TEXT("修复商品池后奖励换批成功"), Shop->TryRefreshOffersForReward(Reason));
	Twin->TryRefreshOffersForReward(Reason);
	for (int32 Index = 0; Index < 6; ++Index)
	{
		TestEqual(TEXT("失败不消耗正式随机流数字"), Shop->GetOffers()[Index].Descriptor.Number, Twin->GetOffers()[Index].Descriptor.Number);
		TestEqual(TEXT("失败不消耗正式随机流颜色"), Shop->GetOffers()[Index].Descriptor.Color, Twin->GetOffers()[Index].Descriptor.Color);
	}
	TestFalse(TEXT("非正房间上限被拒绝"), Shop->TrySetRoomLimit(0, Reason));
	TestEqual(TEXT("非法上限不改写现值"), Shop->RoomLimit, 30);
	TestTrue(TEXT("有效房间上限可以设置"), Shop->TrySetRoomLimit(15, Reason));
	TestEqual(TEXT("房間上限写入新值"), Shop->RoomLimit, 15);

	UCastleMainTowerComponent* Tower = NewObject<UCastleMainTowerComponent>();
	Tower->MaxHealth = 100.0f;
	TestTrue(TEXT("主塔生命初始化"), Tower->InitializeHealth());
	TestTrue(TEXT("主塔受到伤害"), Tower->ApplyDamage(40.0f));
	const int32 AttackCountBeforeHealing = Tower->AttackCount;
	TestTrue(TEXT("主塔可以回血"), Tower->ApplyHealing(25.0f));
	TestEqual(TEXT("回血增加生命"), Tower->CurrentHealth, 85.0f);
	TestEqual(TEXT("回血不增加攻击次数"), Tower->AttackCount, AttackCountBeforeHealing);
	TestTrue(TEXT("超出上限的回血成功且封顶"), Tower->ApplyHealing(50.0f));
	TestEqual(TEXT("生命不超过最大值"), Tower->CurrentHealth, 100.0f);
	TestEqual(TEXT("过量回血不增加攻击次数"), Tower->AttackCount, AttackCountBeforeHealing);
	TestFalse(TEXT("负回血拒绝"), Tower->ApplyHealing(-1.0f));
	TestFalse(TEXT("非有限回血拒绝"), Tower->ApplyHealing(std::numeric_limits<float>::infinity()));
	TestEqual(TEXT("失败回血保留生命"), Tower->CurrentHealth, 100.0f);

	UCastleDirectorConfigDataAsset* Config = NewObject<UCastleDirectorConfigDataAsset>();
	TestFalse(TEXT("缺失资产和界面配置拒绝启动"), Config->ValidateConfig(Reason));
	Config->NodeMesh = GetMutableDefault<UStaticMesh>();
	Config->NPCConfiguration = GetMutableDefault<UMassEntityConfigAsset>();
	Config->HUDWidgetClass = UCastleDirectorHUDWidget::StaticClass();
	Config->RewardWidgetClass = UCastleRewardChoiceWidget::StaticClass();
	Config->GameOverWidgetClass = UCastleGameOverWidget::StaticClass();
	Config->DispatchConfig = LoadObject<UCastleTowerDispatchConfigDataAsset>(nullptr, TEXT("/Game/MovingCastle/Data/DA_CastleTowerDispatch"));
	Config->TowerUpgradeRewardWidgetClass = UCastleTowerUpgradeRewardWidget::StaticClass();
	Config->WildcardRoomPickerClass = UCastleWildcardRoomPickerWidget::StaticClass();
	Config->NodeLabelWidgetClass = nullptr;
	TestFalse(TEXT("缺少NodeLabelWidgetClass拒绝启动"), Config->ValidateConfig(Reason));
	Config->NodeLabelWidgetClass = UCastleDirectorNodeLabelWidget::StaticClass();
	AddExpectedError(TEXT("字段 CombatConfig 值 None 无效"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("缺战斗配置拒绝启动"), Config->ValidateConfig(Reason));
	Config->CombatConfig = LoadObject<UCastleCombatConfigDataAsset>(nullptr, TEXT("/Game/MovingCastle/Data/Combat/DA_CombatConfig.DA_CombatConfig"));
	TestTrue(TEXT("完整有效配置通过校验"), Config->ValidateConfig(Reason));
	Config->NodeCountMin = 1;
	TestFalse(TEXT("非法节点范围拒绝启动"), Config->ValidateConfig(Reason));
	TestTrue(TEXT("配置错误明确写出字段名"), Reason.ToString().Contains(TEXT("NodeCountMin/NodeCountMax")));
	Config->NodeCountMin = 2;
	// 逐个改变单一字段，错误数固定，证明校验不是只覆盖节点数量一个分支。
	AddExpectedError(TEXT("配置资产"), EAutomationExpectedErrorFlags::Contains, 6);
	Config->NodeHeightOffset = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("无限高度拒绝"), Config->ValidateConfig(Reason));
	Config->NodeHeightOffset = 1200.0f;
	Config->NodeMeshScale = 0.0f;
	TestFalse(TEXT("零网格缩放拒绝"), Config->ValidateConfig(Reason));
	Config->NodeMeshScale = 4.0f;
	Config->InitialNPCCount = 0;
	AddExpectedError(TEXT("成长表必须含连续四级"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("非正初始NPC数量拒绝"), Config->ValidateConfig(Reason));
	Config->InitialNPCCount = 8;
	Config->EliteEveryNSteps = 1;
	TestFalse(TEXT("非法精英间隔拒绝"), Config->ValidateConfig(Reason));
	Config->EliteEveryNSteps = 5;
	Config->NodeArcDegrees = 181.0f;
	TestFalse(TEXT("超出圆弧角度拒绝"), Config->ValidateConfig(Reason));
	Config->NodeArcDegrees = 60.0f;
	Config->HealFraction = 1.1f;
	TestFalse(TEXT("超出回血比例拒绝"), Config->ValidateConfig(Reason));
	Config->HealFraction = 0.3f;
	Config->ExpPerLevel = 0;
	TestFalse(TEXT("零升级经验拒绝"), Config->ValidateConfig(Reason));
	Config->ExpPerLevel = 100;
	Config->TowerLevels[1].RoomLimit = -1;
	TestFalse(TEXT("负房间上限拒绝"), Config->ValidateConfig(Reason));
	Config->TowerLevels[1].RoomLimit = 250;
	UCastleShopComponent* RewardShop = NewObject<UCastleShopComponent>();
	Board->BuildingDefinitions.Reset();
	TestFalse(TEXT("首个商店奖励初始化失败明确拒绝"), RewardShop->TryOpenForReward(Economy, Board, Reason));
	TestFalse(TEXT("失败不留下已初始化状态"), RewardShop->IsInitialized());
	TestFalse(TEXT("失败不打开商店"), RewardShop->IsShopOpen());
	TestEqual(TEXT("失败不留下商品"), RewardShop->GetOffers().Num(), 0);
	Board->BuildingDefinitions = PreviousDefinitions;
	TestTrue(TEXT("修复配置后完整奖励事务成功"), RewardShop->TryOpenForReward(Economy, Board, Reason));
	TestTrue(TEXT("奖励事务打开商店"), RewardShop->IsShopOpen());
	TestEqual(TEXT("奖励事务仍不扣金币"), Economy->GetGold(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
