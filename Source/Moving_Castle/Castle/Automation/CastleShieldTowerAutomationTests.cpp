// 盾塔接入回归：使用正式 DA 和等级蓝图，贯通商店、背包、合成、触发、回收与出售。

#if WITH_DEV_AUTOMATION_TESTS

#include "CastleBuildingTestUtilities.h"
#include "CastleBackpackComponent.h"
#include "CastleEconomyComponent.h"
#include "CastleShopComponent.h"
#include "BuildingMergeComponent.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

/** 验证实际资产在各系统之间始终保留盾塔身份，不借助临时模型绕过正式蓝图。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShieldTowerFrameworkTest, "MovingCastle.Logic.ShieldTower.FrameworkIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShieldTowerFrameworkTest::RunTest(const FString& Parameters)
{
	UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_ShieldTower.DA_ShieldTower"));
	if (!TestNotNull(TEXT("正式盾塔定义存在"), Definition)) { return false; }
	FText Reason;
	if (!TestTrue(TEXT("正式盾塔定义通过共同校验"), Definition->ValidateConfiguration(Reason))) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("盾塔拥有独立类别"), Definition->BuildingType, ECastleBuildingType::ShieldTower);
	UClass* BoardClass = LoadClass<ACastleBoard>(nullptr, TEXT("/Game/MovingCastle/BP/BP_CastleBoard.BP_CastleBoard_C"));
	if (!TestNotNull(TEXT("正式棋盘蓝图可加载"), BoardClass)) { return false; }
	TestTrue(TEXT("正式棋盘定义池已接入盾塔"), BoardClass->GetDefaultObject<ACastleBoard>()->FindBuildingDefinition(ECastleBuildingType::ShieldTower) == Definition);

	// 独立游戏世界保留真实物理、Actor 生命周期与动画事件，不改写任何用户关卡。
	const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建盾塔接入测试世界"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	if (!TestNotNull(TEXT("创建测试棋盘"), Board)) { return false; }
	Board->GridSize = FIntVector(2, 1, 2);
	// 单类型商品池是本测试显式输入，保证每个商品都能验证盾塔的购买路径。
	Board->BuildingDefinitions = { Definition };
	UCastleEconomyComponent* Economy = NewObject<UCastleEconomyComponent>();
	UCastleShopComponent* Shop = NewObject<UCastleShopComponent>();
	if (!Economy->TryAddGold(100, Reason) || !Shop->InitializeShop(Economy, Board, Reason)
		|| !Shop->TryOpenShop(Reason)) { AddError(Reason.ToString()); return false; }
	if (!TestEqual(TEXT("盾塔参与标准六槽商品生成"), Shop->GetOffers().Num(), 6)) { return false; }
	for (const FCastleShopOffer& Offer : Shop->GetOffers())
	{
		TestTrue(TEXT("商品保留正式盾塔定义和一级身份"), Offer.Descriptor.Definition == Definition && Offer.Descriptor.Level == 1);
	}
	const FCastleBuildingDescriptor Card = Shop->GetOffers()[0].Descriptor;
	const int32 PurchasePrice = Shop->GetOffers()[0].Price;
	if (!TestTrue(TEXT("盾塔商品可直接购买进背包"), Shop->TryBuyOffer(0, Reason))) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("盾塔使用现有统一定价扣款"), Economy->GetGold(), 100 - PurchasePrice);
	Shop->CloseShop();

	// 显式提供与已购卡同属性的两格地面及第二张卡，验证同定义合成而非随机碰运气。
	FCastleGroundGenerationResult Ground;
	for (int32 X = 0; X < 2; ++X)
	{
		FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
		Cell.Coordinate = FIntVector(X, 0, 0);
		Cell.Color = Card.Color;
		Cell.Number = Card.Number;
	}
	if (!Board->ApplyGroundGenerationResult(Ground, Reason)) { AddError(Reason.ToString()); return false; }
	ACastleBuildingBase* First = Board->PlaceBuildingFromBackpack(FIntVector::ZeroValue, Board->PrototypeBackpack, 0, true, Reason);
	if (!TestNotNull(TEXT("已购盾塔从真实背包放置"), First) || !CastleBuildingTests::CompletePlacement(First)) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("放置消费一张背包卡"), Board->PrototypeBackpack->GetItemCount(), 0);
	if (!TestNotNull(TEXT("盾塔实际实例有统一效果组件"), First->BuildingEffect.Get())) { return false; }
	TestEqual(TEXT("盾塔保留基础触发组件"), First->BuildingEffect->GetClass(), UCastleBuildingEffectComponent::StaticClass());
	TestTrue(TEXT("盾塔可连续接收统一触发"), First->TriggerBuildingEffect(FCastleBuildingTriggerContext())
		&& First->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
	TestEqual(TEXT("两次触发有可观察计数"), First->BuildingEffect->TriggerCount, int64(2));
	int32 AddedIndex = INDEX_NONE;
	if (!Board->PrototypeBackpack->TryAddItem(Card, AddedIndex, Reason)) { AddError(Reason.ToString()); return false; }
	ACastleBuildingBase* Second = Board->PlaceBuildingFromBackpack(FIntVector(1, 0, 0), Board->PrototypeBackpack, AddedIndex, true, Reason);
	if (!TestNotNull(TEXT("第二张同属性盾塔可放置"), Second) || !CastleBuildingTests::CompletePlacement(Second)) { AddError(Reason.ToString()); return false; }
	ACastleBuildingBase* Merged = CastleBuildingTests::FindPlacedLevel(Board, 2);
	if (!TestNotNull(TEXT("飞入完成后合成二级盾塔"), Merged)) { return false; }
	TestTrue(TEXT("合成直接生成盾塔二级蓝图"), Merged->GetClass() == Definition->FindLevel(2)->BuildingClass);
	TestEqual(TEXT("盾塔合成占格守恒"), Board->GetOccupiedRoomCellCount(), 2);
	TestTrue(TEXT("合成保留正式定义身份"), Merged->GetBuildingDefinition() == Definition);
	TestEqual(TEXT("合成新实例不继承旧触发次数"), Merged->BuildingEffect->TriggerCount, int64(0));
	Merged->BuildingMerge->TickComponent(100.0f, LEVELTICK_All, nullptr);
	if (!Board->RemoveBuildingToBackpack(Merged, Board->PrototypeBackpack, Reason)) { AddError(Reason.ToString()); return false; }
	FCastleBuildingDescriptor Recovered;
	if (!TestTrue(TEXT("二级盾塔可回收到背包"), Board->PrototypeBackpack->TryGetItemAt(0, Recovered))) { return false; }
	TestTrue(TEXT("回收保留盾塔定义和二级"), Recovered.Definition == Definition && Recovered.Level == 2);
	TestEqual(TEXT("回收释放全部占用"), Board->GetOccupiedRoomCellCount(), 0);

	// 二级卡再次走正式生成入口，再回收出售，覆盖卡牌完整流转而不改变盾塔玩法。
	ACastleBuildingBase* Restored = Board->PlaceBuildingFromBackpack(FIntVector::ZeroValue, Board->PrototypeBackpack, 0, false, Reason);
	if (!TestNotNull(TEXT("回收的二级盾塔可重新放置"), Restored) || !CastleBuildingTests::CompletePlacement(Restored)) { return false; }
	if (!Board->RemoveBuildingToBackpack(Restored, Board->PrototypeBackpack, Reason) || !Shop->TryOpenShop(Reason)) { AddError(Reason.ToString()); return false; }
	int32 SellPrice = 0;
	if (!Shop->TryGetSellPrice(0, SellPrice, Reason)) { AddError(Reason.ToString()); return false; }
	const int32 GoldBeforeSale = Economy->GetGold();
	if (!TestTrue(TEXT("盾塔回收卡可按统一规则出售"), Shop->TrySellBackpackItem(0, Reason))) { AddError(Reason.ToString()); return false; }
	TestEqual(TEXT("出售收益使用展示报价"), Economy->GetGold(), GoldBeforeSale + SellPrice);
	TestEqual(TEXT("出售后移除对应卡牌"), Board->PrototypeBackpack->GetItemCount(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
