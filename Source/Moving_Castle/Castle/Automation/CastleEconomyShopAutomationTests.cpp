// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildings.h"
#include "CastleBuildingTestUtilities.h"
#include "CastleCardPricing.h"
#include "CastleEconomyComponent.h"
#include "CastleShopComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

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

	// 二级合成卡按两个格子计入全局额度，不能因为只占一个背包槽而多买。
	int32 AddedIndex = INDEX_NONE;
	Board->PrototypeBackpack->TrySetCapacity(10, Reason);
	Board->PrototypeBackpack->TryAddBuilding(Board->FindBuildingDefinition(ECastleBuildingType::ArcherTower), 2, ECastleTileColor::Red, 3, AddedIndex, Reason);
	Shop->RoomLimit = 2;
	TestEqual(TEXT("合成卡按实际格数计入总量"), Shop->GetOwnedRoomCount(), 2);
	TestFalse(TEXT("全局上限拒绝新购买"), Shop->TryBuyOffer(1, Reason));
	Shop->RoomLimit = 30;
	const int32 GoldBeforeRefresh = Economy->GetGold();
	TestTrue(TEXT("刷新成功"), Shop->TryRefreshOffers(Reason));
	TestEqual(TEXT("刷新扣除固定费用"), Economy->GetGold(), GoldBeforeRefresh - Shop->RefreshCost);
	for (const FCastleShopOffer& Offer : Shop->GetOffers())
	{
		TestFalse(TEXT("刷新后全部可购买"), Offer.bSold);
		TestTrue(TEXT("所有商品使用一级类"), Offer.Descriptor.Level == 1);
		TestTrue(TEXT("道路属性固定"), Offer.Descriptor.GetBuildingType() != ECastleBuildingType::Road
			|| (Offer.Descriptor.Color == ECastleTileColor::Gray && Offer.Descriptor.Number == 0));
	}
	// 即使卖出能释放容量，也必须先保证收款不会溢出，不能先删除卡牌再发现无法入账。
	Economy->TryAddGold(MAX_int32 - Economy->GetGold(), Reason);
	TestFalse(TEXT("金币已满时出售失败"), Shop->TrySellBackpackItem(0, Reason));
	TestEqual(TEXT("出售失败保留卡牌"), Board->PrototypeBackpack->GetItemCount(), 1);
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
	Target.Face = ECastleGridFace::Up;
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

#endif // WITH_DEV_AUTOMATION_TESTS
