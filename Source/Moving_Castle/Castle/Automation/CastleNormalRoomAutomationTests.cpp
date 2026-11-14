// 普通房间全链路回归：正式资产、全部合法属性、统一定价、商品生成及非法地面拒绝。

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingDefinition.h"
#include "CastleBuildings.h"
#include "CastleCardPricing.h"
#include "CastleEconomyComponent.h"
#include "CastleShopComponent.h"
#include "Engine/Blueprint.h"
#include "Misc/AutomationTest.h"
#include "UObject/SoftObjectPath.h"

/** 用正式资产穷举五色 1～9，防止普通房间仍在某个入口被当成灰色 0 号特例。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleNormalRoomRulesTest, "MovingCastle.Logic.NormalRoomRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleNormalRoomRulesTest::RunTest(const FString& Parameters)
{
	UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_NormalRoom.DA_NormalRoom"));
	if (!TestNotNull(TEXT("加载正式普通房间定义"), Definition)) { return false; }
	FText Reason;
	if (!TestTrue(TEXT("正式普通房间定义校验通过"), Definition->ValidateConfiguration(Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestEqual(TEXT("普通房间保留旧类别序号 0"), static_cast<uint8>(Definition->BuildingType), uint8(0));
	TestEqual(TEXT("正式显示名为普通房间"), Definition->DisplayName.ToString(), FString(TEXT("普通房间")));
	TestEqual(TEXT("普通房间定义仅配置一级"), Definition->Levels.Num(), 1);
	UClass* RoomClass = LoadClass<ACastleBuildingBase>(nullptr,
		TEXT("/Game/MovingCastle/BP/BP_NormalRoom_lv1.BP_NormalRoom_lv1_C"));
	if (!TestNotNull(TEXT("加载正式普通房间蓝图"), RoomClass)) { return false; }
	TestEqual(TEXT("定义直接引用新命名蓝图"), Definition->FindLevel(1)->BuildingClass.Get(), RoomClass);
	TestEqual(TEXT("新蓝图直接继承普通房间原生类"), RoomClass->GetSuperClass(), ACastleNormalRoomBuilding::StaticClass());
	// 软引用反序列化会先修正 CoreRedirects，再加载对象；直接裸字符串加载不等价于这条链路。
	FSoftObjectPath LegacyNativeClass(TEXT("/Script/Moving_Castle.CastleRoadBuilding"));
	LegacyNativeClass.FixupCoreRedirects();
	TestEqual(TEXT("旧原生道路类软引用可重定向到普通房间"), Cast<UClass>(LegacyNativeClass.TryLoad()), ACastleNormalRoomBuilding::StaticClass());
	FSoftObjectPath LegacyDefinition(TEXT("/Game/MovingCastle/Data/Buildings/DA_Road.DA_Road"));
	LegacyDefinition.FixupCoreRedirects();
	TestEqual(TEXT("旧道路 DA 软引用可加载新普通房间定义"), Cast<UCastleBuildingDefinition>(LegacyDefinition.TryLoad()), Definition);
	FSoftObjectPath LegacyBlueprintClass(TEXT("/Game/MovingCastle/BP/BP_Road_lv1.BP_Road_lv1_C"));
	LegacyBlueprintClass.FixupCoreRedirects();
	TestEqual(TEXT("旧道路蓝图生成类软引用可重定向"), Cast<UClass>(LegacyBlueprintClass.TryLoad()), RoomClass);
	TestTrue(TEXT("普通房间默认对象明确不允许升级"), RoomClass->GetDefaultObject<ACastleBuildingBase>()->IsNormalRoom()
		&& !RoomClass->GetDefaultObject<ACastleBuildingBase>()->CanMergeToNextLevel());

	UCastleSimpleCardPricing* Pricing = NewObject<UCastleSimpleCardPricing>();
	FCastleBuildingDescriptor Card;
	for (uint8 Color = static_cast<uint8>(ECastleTileColor::Red); Color <= static_cast<uint8>(ECastleTileColor::Purple); ++Color)
	{
		for (int32 Number = 1; Number <= 9; ++Number)
		{
			if (!TestTrue(TEXT("五色 1～9 全部可以创建普通房间卡"),
				Definition->CreateDescriptor(1, static_cast<ECastleTileColor>(Color), Number, Card, Reason))) { return false; }
			int32 Price = 0;
			TestTrue(TEXT("普通房间可以购买报价"), Pricing->TryCalculatePrice_Implementation(Card, ECastleCardPriceKind::Buy, Price, Reason));
			TestEqual(TEXT("普通房间购买价为 5 + 数字×2"), Price, 5 + Number * 2);
			TestTrue(TEXT("普通房间可以出售报价"), Pricing->TryCalculatePrice_Implementation(Card, ECastleCardPriceKind::Sell, Price, Reason));
			TestEqual(TEXT("出售为买价一半向下取整"), Price, (5 + Number * 2) / 2);
		}
	}
	TestFalse(TEXT("普通房间拒绝灰色，即使数字有效"), Definition->CreateDescriptor(1, ECastleTileColor::Gray, 3, Card, Reason));
	TestFalse(TEXT("普通房间拒绝 0 号，即使颜色有效"), Definition->CreateDescriptor(1, ECastleTileColor::Red, 0, Card, Reason));
	TestFalse(TEXT("普通房间拒绝越界数字"), Definition->CreateDescriptor(1, ECastleTileColor::Blue, 10, Card, Reason));
	TestFalse(TEXT("普通房间拒绝越界颜色"), Definition->CreateDescriptor(1, static_cast<ECastleTileColor>(255), 3, Card, Reason));
	TestFalse(TEXT("普通房间拒绝二级描述"), Definition->CreateDescriptor(2, ECastleTileColor::Red, 3, Card, Reason));

	// 两个棋盘路径包含兼容入口；加载后必须解析到同一正式定义池，不能只修新路径。
	for (const TCHAR* BoardPath : { TEXT("/Game/MovingCastle/BP/BP_CastleBoard.BP_CastleBoard"),
		TEXT("/Game/MovingCastle/BP_CastleBoard.BP_CastleBoard") })
	{
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, BoardPath);
		if (!TestNotNull(TEXT("新旧棋盘资产路径均可加载"), Blueprint)) { return false; }
		const ACastleBoard* Defaults = Blueprint->GeneratedClass->GetDefaultObject<ACastleBoard>();
		TestTrue(TEXT("棋盘定义池通过完整校验"), Defaults->ValidateBuildingDefinitions(Reason));
		TestEqual(TEXT("棋盘引用正式普通房间 DA"), Defaults->FindBuildingDefinition(ECastleBuildingType::NormalRoom), Definition);
	}

	// 将商品池限定为正式普通房间，保证每次随机生成都实际覆盖该类别。
	ACastleBoard* Board = NewObject<ACastleBoard>();
	Board->BuildingDefinitions = { Definition };
	UCastleEconomyComponent* Economy = NewObject<UCastleEconomyComponent>();
	UCastleShopComponent* Shop = NewObject<UCastleShopComponent>();
	if (!TestTrue(TEXT("绑定仅普通房间商品池"), Shop->InitializeShop(Economy, Board, Reason))) { return false; }
	Economy->TryAddGold(100, Reason);
	if (!TestTrue(TEXT("打开普通房间商店"), Shop->TryOpenShop(Reason))) { return false; }
	for (int32 Refresh = 0; Refresh < 10; ++Refresh)
	{
		if (Refresh > 0 && !TestTrue(TEXT("普通房间商品可以连续刷新"), Shop->TryRefreshOffers(Reason))) { return false; }
		TestEqual(TEXT("每次生成六个商品"), Shop->GetOffers().Num(), 6);
		for (const FCastleShopOffer& Offer : Shop->GetOffers())
		{
			TestTrue(TEXT("普通房间商品保留定义和一级五色数字"), Offer.Descriptor.Definition == Definition
				&& Offer.Descriptor.Level == 1 && Offer.Descriptor.Color >= ECastleTileColor::Red
				&& Offer.Descriptor.Color <= ECastleTileColor::Purple && Offer.Descriptor.Number >= 1 && Offer.Descriptor.Number <= 9);
			TestEqual(TEXT("商品显示报价符合统一公式"), Offer.Price, 5 + Offer.Descriptor.Number * 2);
		}
	}
	return true;
}

/** 棋盘接收边界必须拒绝非法地面并保留旧状态，不能偷偷替换颜色或数字。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleNormalRoomGroundValidationTest, "MovingCastle.Logic.NormalRoomGroundValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleNormalRoomGroundValidationTest::RunTest(const FString& Parameters)
{
	ACastleBoard* Board = NewObject<ACastleBoard>();
	Board->GridSize = FIntVector(1, 1, 1);
	FCastleGroundGenerationResult Ground;
	FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
	Cell.Color = ECastleTileColor::Red;
	Cell.Number = 3;
	FText Reason;
	if (!TestTrue(TEXT("接收五色有效地面"), Board->ApplyGroundGenerationResult(Ground, Reason))) { return false; }
	AddExpectedError(TEXT("拒绝非法地面"), EAutomationExpectedErrorFlags::Contains, 3);
	Cell.Color = ECastleTileColor::Gray;
	TestFalse(TEXT("灰色即使非零也必须拒绝"), Board->ApplyGroundGenerationResult(Ground, Reason));
	Cell.Color = ECastleTileColor::Red;
	Cell.Number = 0;
	TestFalse(TEXT("有效颜色的 0 号地面必须拒绝"), Board->ApplyGroundGenerationResult(Ground, Reason));
	Cell.Color = static_cast<ECastleTileColor>(255);
	Cell.Number = 3;
	TestFalse(TEXT("越界颜色必须拒绝"), Board->ApplyGroundGenerationResult(Ground, Reason));
	FCastleGroundCell Preserved;
	TestTrue(TEXT("拒绝后旧地面仍然存在"), Board->TryGetGroundCell(FIntVector::ZeroValue, Preserved));
	TestTrue(TEXT("拒绝后没有修正或提交错误输入"), Preserved.Color == ECastleTileColor::Red && Preserved.Number == 3);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
