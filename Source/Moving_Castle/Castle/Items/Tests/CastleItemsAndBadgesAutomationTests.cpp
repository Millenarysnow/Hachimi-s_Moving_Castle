#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Automation/CastlePIEAutomation.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "CastleBackpackComponent.h"
#include "CastleBackpackTestObserver.h"
#include "CastleBuildingTestUtilities.h"
#include "CastleBuildingDefinition.h"
#include "CastleMassFragments.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"
#include "Components/Building/Effects/CastleSpeedRoomEffectComponent.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Items/CastleItemGrantLibrary.h"
#include "Items/CastlePendingRoomReward.h"
#include "Items/CastleRoomGroupDataAsset.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "UI/Items/CastleBadgeBarWidget.h"
#include "UI/Items/CastleBadgeReplaceModalWidget.h"
#include "UI/Items/CastleTempBuildBarWidget.h"
#include "UI/Items/CastleWildcardRoomPickerWidget.h"
#include "UI/Cards/CastleShopCardWidget.h"

namespace CastleItemTests
{
	UCastleBuildingDefinition* LoadDefinition(const TCHAR* Name)
	{
		return LoadObject<UCastleBuildingDefinition>(nullptr, *FString::Printf(
			TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), Name, Name));
	}

	/** 所有入库测试用正式定义，确保与游戏的描述符校验一致。 */
	bool FillBackpack(UCastleBackpackComponent& Backpack, UCastleBuildingDefinition& Definition, FText& Reason)
	{
		int32 Index;
		for (int32 Slot = 0; Slot < 10; ++Slot)
		{
			if (!Backpack.TryAddBuilding(&Definition, 1, ECastleTileColor::Red, 4, Index, Reason))
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleTempBackpackTest, "MovingCastle.Items.Backpack.OverflowFIFO",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleTempBackpackTest::RunTest(const FString& Parameters)
{
	auto* Definition = CastleItemTests::LoadDefinition(TEXT("NormalRoom"));
	auto* Backpack = NewObject<UCastleBackpackComponent>();
	FText Reason;
	if (!TestNotNull(TEXT("正式房间定义"), Definition)
		|| !TestTrue(TEXT("建筑栏填满十张"), CastleItemTests::FillBackpack(*Backpack, *Definition, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}

	bool bTemp;
	int32 Index;
	FCastleBuildingDescriptor First;
	FCastleBuildingDescriptor Second;
	if (!Definition->CreateDescriptor(1, ECastleTileColor::Blue, 7, First, Reason)
		|| !Definition->CreateDescriptor(1, ECastleTileColor::Green, 8, Second, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestFalse(TEXT("商店入口在满包时拒绝"), Backpack->TryAddItem(First, Index, Reason));
	TestEqual(TEXT("拒绝购买后临时栏未变"), Backpack->GetTempItemCount(), 0);
	TestTrue(TEXT("道具入口把第一张追加到临时栏"), Backpack->TryAddItemAllowOverflow(First, bTemp, Index, Reason));
	TestTrue(TEXT("第一张进临时栏"), bTemp);
	TestEqual(TEXT("返回临时栏队首索引"), Index, 0);
	TestTrue(TEXT("道具入口继续追加第二张"), Backpack->TryAddItemAllowOverflow(Second, bTemp, Index, Reason));
	TestEqual(TEXT("临时栏张数为二"), Backpack->GetTempItemCount(), 2);

	FCastleBuildingDescriptor Unspecified = First;
	Unspecified.Color = ECastleTileColor::Gray;
	const FCastleBuildingDescriptor InvalidBatch[] = {First, Unspecified};
	AddExpectedError(TEXT("拒绝整批建筑卡牌"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("未确定颜色的描述拒绝整批入库"), Backpack->TryAddItemsAllowOverflow(InvalidBatch, Reason));
	TestEqual(TEXT("失败批次不增加临时栏张数"), Backpack->GetTempItemCount(), 2);

	FCastleBuildingDescriptor Removed;
	FCastleBuildingDescriptor Promoted;
	auto* Observer = NewObject<UCastleBackpackTestObserver>();
	Observer->Backpack = Backpack;
	Backpack->OnBackpackChanged.AddDynamic(Observer, &UCastleBackpackTestObserver::Observe);
	TestTrue(TEXT("事务暂取原槽卡牌"), Backpack->TryRemoveItemAt(0, Removed, Reason));
	TestEqual(TEXT("暂取阶段不补位"), Backpack->GetItemCount(), 9);
	TestEqual(TEXT("暂取阶段不减少临时栏"), Backpack->GetTempItemCount(), 2);
	TestTrue(TEXT("失败后可以回插原卡"), Backpack->TryInsertItemAt(0, Removed, Reason));
	TestEqual(TEXT("回滚后建筑栏仍有十张"), Backpack->GetItemCount(), 10);
	TestEqual(TEXT("回滚后不改临时栏"), Backpack->GetTempItemCount(), 2);
	FCastleBuildingDescriptor Restored;
	if (TestTrue(TEXT("回滚可读原槽"), Backpack->TryGetItemAt(0, Restored)))
	{
		TestEqual(TEXT("原槽恢复原定义"), Restored.Definition, Removed.Definition);
		TestEqual(TEXT("原槽恢复原色"), Restored.Color, Removed.Color);
		TestEqual(TEXT("原槽恢复原数字"), Restored.Number, Removed.Number);
	}
	TestEqual(TEXT("暂取与回滚均不广播"), Observer->ObservedCounts.Num(), 0);

	TestTrue(TEXT("重新取出第一个建筑栏槽位"), Backpack->TryRemoveItemAt(0, Removed, Reason));
	TestEqual(TEXT("提交前不通知"), Observer->ObservedCounts.Num(), 0);
	TestEqual(TEXT("成功提交填补一张卡"), Backpack->CommitAutoRefillFromTempBar(), 1);
	TestEqual(TEXT("补位后建筑栏依然有十张"), Backpack->GetItemCount(), 10);
	TestEqual(TEXT("补位后临时栏剩一张"), Backpack->GetTempItemCount(), 1);
	if (TestEqual(TEXT("提交时只广播一次"), Observer->ObservedCounts.Num(), 1))
	{
		TestEqual(TEXT("提交事件只能观察到完成补位的库存"), Observer->ObservedCounts[0], FIntPoint(10, 1));
	}
	if (TestTrue(TEXT("读取补入的最后一格"), Backpack->TryGetItemAt(9, Promoted)))
	{
		TestEqual(TEXT("第一张以 FIFO 顺序补入"), Promoted.Number, First.Number);
	}
	Observer->ObservedCounts.Reset();
	TestTrue(TEXT("使用不可回滚入口消费一张卡"), Backpack->TryConsumeItemAt(0, Removed, Reason));
	TestEqual(TEXT("第二次补位清空临时栏"), Backpack->GetTempItemCount(), 0);
	if (TestEqual(TEXT("单步消费只广播一次"), Observer->ObservedCounts.Num(), 1))
	{
		TestEqual(TEXT("消费事件收到最终库存"), Observer->ObservedCounts[0], FIntPoint(10, 0));
	}
	if (TestTrue(TEXT("读取第二次补入的最后一格"), Backpack->TryGetItemAt(9, Promoted)))
	{
		TestEqual(TEXT("第二张按进入顺序补入"), Promoted.Number, Second.Number);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleWildcardItemTest, "MovingCastle.Items.Wildcard.SelectionAndCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleWildcardItemTest::RunTest(const FString& Parameters)
{
	auto* Definition = CastleItemTests::LoadDefinition(TEXT("NormalRoom"));
	if (!TestNotNull(TEXT("普通房间已配置"), Definition))
	{
		return false;
	}
	const TArray<TObjectPtr<UCastleBuildingDefinition>> Pool{Definition};
	auto* Backpack = NewObject<UCastleBackpackComponent>();
	FText Reason;
	FCastleWildcardRoomOptions Options;
	FCastleWildcardRoomSpec Spec;
	Spec.DefaultMaxNumber = 9;
	if (!TestTrue(TEXT("空集展开全部合法选项"), Spec.TryResolveOptions(Pool, Options, Reason)))
	{
		return false;
	}
	TestEqual(TEXT("全部五色"), Options.Colors.Num(), 5);
	TestEqual(TEXT("完整九个数字"), Options.Numbers.Num(), 9);

	Spec.AllowedColors = {ECastleTileColor::Red, ECastleTileColor::Blue};
	Spec.AllowedNumbers = {7, 8, 9};
	UCastlePendingRoomReward* Reward;
	if (!TestTrue(TEXT("准备红蓝色与大数字待领卡"), UCastleItemGrantLibrary::TryCreateWildcardReward(
		INVTEXT("红蓝测试卡"), Spec, Backpack, Pool, Reward, Reason))
		|| !TestNotNull(TEXT("奖励句柄"), Reward))
	{
		return false;
	}
	TestEqual(TEXT("点击前不改变库存"), Backpack->GetItemCount(), 0);
	AddExpectedError(TEXT("当前奖励没有打开的属性选择"), EAutomationExpectedErrorFlags::Contains, 1);
	bool bTemp;
	int32 Index;
	TestFalse(TEXT("不能绕过获取直接确认"), Reward->TryConfirmClaim(Definition, ECastleTileColor::Red, 7, bTemp, Index, Reason));
	TestTrue(TEXT("点击获取后列出选项"), Reward->TryBeginClaim(Options, Reason));
	TestEqual(TEXT("只有两种颜色"), Options.Colors.Num(), 2);
	TestEqual(TEXT("只有三种数字"), Options.Numbers.Num(), 3);
	AddExpectedError(TEXT("万能卡确认失败"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("未选颜色不能入库"), Reward->TryConfirmClaim(Definition, ECastleTileColor::Gray, 7, bTemp, Index, Reason));
	TestFalse(TEXT("配置以外的数字不能入库"), Reward->TryConfirmClaim(Definition, ECastleTileColor::Red, 6, bTemp, Index, Reason));
	TestTrue(TEXT("取消使奖励回到待领取"), Reward->TryCancelClaim(Reason));
	TestEqual(TEXT("取消时库存仍为空"), Backpack->GetItemCount(), 0);
	TestTrue(TEXT("取消后可以再次打开"), Reward->TryBeginClaim(Options, Reason));
	TestTrue(TEXT("以合法组合确认领取"), Reward->TryConfirmClaim(Definition, ECastleTileColor::Blue, 9, bTemp, Index, Reason));
	TestTrue(TEXT("奖励句柄全局标记为已领取"), Reward->IsClaimed());
	TestEqual(TEXT("库存只获得一张确定牌"), Backpack->GetItemCount(), 1);

	FCastleBuildingDescriptor Card;
	if (TestTrue(TEXT("读取具体卡牌"), Backpack->TryGetItemAt(0, Card)))
	{
		TestEqual(TEXT("具体建筑定义"), Card.Definition.Get(), Definition);
		TestEqual(TEXT("具体颜色"), Card.Color, ECastleTileColor::Blue);
		TestEqual(TEXT("具体数字"), Card.Number, 9);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleRoomGroupItemTest, "MovingCastle.Items.RoomGroups.AllTemplates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleRoomGroupItemTest::RunTest(const FString& Parameters)
{
	TArray<TObjectPtr<UCastleBuildingDefinition>> Pool;
	for (const TCHAR* Name : {TEXT("NormalRoom"), TEXT("ArcherTower"), TEXT("Farm"), TEXT("SpeedRoom")})
	{
		UCastleBuildingDefinition* Definition = CastleItemTests::LoadDefinition(Name);
		if (!TestNotNull(Name, Definition))
		{
			return false;
		}
		Pool.Add(Definition);
	}
	FRandomStream Random(2704);
	// 漏配和反序列化后越界的模板必须在准备牌面前失败，且保留调用方的随机流和库存。
	auto* InvalidGroup = NewObject<UCastleRoomGroupDataAsset>();
	InvalidGroup->DisplayName = INVTEXT("验证卡包稀有度");
	auto* UnchangedBackpack = NewObject<UCastleBackpackComponent>();
	const int32 SeedBeforeReject = Random.GetCurrentSeed();
	UCastlePendingRoomReward* RejectedReward = nullptr;
	FText InvalidReason;
	AddExpectedError(TEXT("的卡包稀有度无效"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("未配置稀有度的房间组拒绝发放"), UCastleItemGrantLibrary::TryGrantRoomGroup(
		InvalidGroup, UnchangedBackpack, Pool, Random, RejectedReward, InvalidReason));
	TestTrue(TEXT("稀有度错误指出资产路径"), InvalidReason.ToString().Contains(InvalidGroup->GetPathName()));
	InvalidGroup->Rarity = static_cast<ECastleRoomPackRarity>(255);
	TestFalse(TEXT("越界稀有度的房间组拒绝发放"), UCastleItemGrantLibrary::TryGrantRoomGroup(
		InvalidGroup, UnchangedBackpack, Pool, Random, RejectedReward, InvalidReason));
	TestNull(TEXT("非法稀有度未创建待领奖励"), RejectedReward);
	TestEqual(TEXT("非法稀有度未消耗随机流"), Random.GetCurrentSeed(), SeedBeforeReject);
	TestEqual(TEXT("非法稀有度未发放卡牌"), UnchangedBackpack->GetItemCount() + UnchangedBackpack->GetTempItemCount(), 0);
	for (int32 Template = 0; Template <= static_cast<int32>(ECastleRoomGroupType::NPCSpeedRoom); ++Template)
	{
		auto* Backpack = NewObject<UCastleBackpackComponent>();
		auto* Group = NewObject<UCastleRoomGroupDataAsset>();
		Group->DisplayName = FText::AsNumber(Template);
		Group->GroupType = static_cast<ECastleRoomGroupType>(Template);
		// 这些临时模板仅用于验证发牌语义，提供一个显式合法的展示档位。
		Group->Rarity = ECastleRoomPackRarity::White;
		UCastlePendingRoomReward* Pending = nullptr;
		FText Reason;
		if (!TestTrue(TEXT("从正式定义池发放房间组"), UCastleItemGrantLibrary::TryGrantRoomGroup(
			Group, Backpack, Pool, Random, Pending, Reason)))
		{
			AddError(Reason.ToString());
			return false;
		}
		const auto Type = Group->GroupType;
		if (Type == ECastleRoomGroupType::ThreeNumberFourArchers || Type == ECastleRoomGroupType::ThreeConsecutiveFarms)
		{
			TestNull(TEXT("三张直接发放，不创建待领项"), Pending);
			const auto Cards = Backpack->GetItems();
			if (!TestEqual(TEXT("获得三张确定牌"), Cards.Num(), 3))
			{
				return false;
			}
			for (int32 Index = 0; Index < Cards.Num(); ++Index)
			{
				const bool bFarm = Type == ECastleRoomGroupType::ThreeConsecutiveFarms;
				TestEqual(TEXT("三张卡均为指定房间类型"), Cards[Index].GetBuildingType(),
					bFarm ? ECastleBuildingType::Farm : ECastleBuildingType::ArcherTower);
				TestEqual(TEXT("箭塔数字四，农田数字连续"), Cards[Index].Number, bFarm ? Cards[0].Number + Index : 4);
				if (bFarm)
				{
					TestEqual(TEXT("三张农田同色"), Cards[Index].Color, Cards[0].Color);
					TestTrue(TEXT("农田数字在一至六"), Cards[Index].Number >= 1 && Cards[Index].Number <= 6);
				}
			}
		}
		else if (Type == ECastleRoomGroupType::HighNumberRoom || Type == ECastleRoomGroupType::NPCSpeedRoom)
		{
			TestNull(TEXT("默认直接发放房间"), Pending);
			FCastleBuildingDescriptor Card;
			if (!TestTrue(TEXT("获得一张确定牌"), Backpack->TryGetItemAt(0, Card)))
			{
				return false;
			}
			if (Type == ECastleRoomGroupType::HighNumberRoom)
			{
				TestTrue(TEXT("大数字限制在七至九"), Card.Number >= 7 && Card.Number <= 9);
			}
			else
			{
				TestEqual(TEXT("加速房使用原生建筑类型"), Card.GetBuildingType(), ECastleBuildingType::SpeedRoom);
				TestEqual(TEXT("加速房只有一级"), Card.Level, 1);
			}
		}
		else
		{
			if (!TestNotNull(TEXT("自定义模板生成待领取句柄"), Pending))
			{
				return false;
			}
			TestEqual(TEXT("点击前无卡进包"), Backpack->GetItemCount(), 0);
			FCastleWildcardRoomOptions Options;
			if (!TestTrue(TEXT("点击获取解析本卡的选项"), Pending->TryBeginClaim(Options, Reason)))
			{
				return false;
			}
			const bool bAllDefinitions = Type == ECastleRoomGroupType::FullyCustomRoom;
			TestEqual(TEXT("固定型只有一个定义，全自定义使用定义池"), Options.Definitions.Num(), bAllDefinitions ? Pool.Num() : 1);
			TestEqual(TEXT("默认可选五色"), Options.Colors.Num(), 5);
			TestEqual(TEXT("万能颜色只锁定数字，其余默认六个数字"), Options.Numbers.Num(),
				Type == ECastleRoomGroupType::CustomColorNormalRoom ? 1 : 6);
			if (Type == ECastleRoomGroupType::RandomFunctionCustomRoom)
			{
				const auto Kind = Options.Definitions[0]->BuildingType;
				TestTrue(TEXT("随机功能限定在功能塔"), Kind == ECastleBuildingType::ArcherTower || Kind == ECastleBuildingType::Farm);
			}
			bool bTemp;
			int32 Index;
			TestTrue(TEXT("全部选定后领取确定牌"),
				Pending->TryConfirmClaim(Options.Definitions[0], Options.Colors[0], Options.Numbers[0], bTemp, Index, Reason));
			TestEqual(TEXT("本次领取只提交一张"), Backpack->GetItemCount(), 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgeItemTest, "MovingCastle.Items.Badges.CapacityEffectsAndWorldLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBadgeItemTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	FWorldContext* Context = Instance->GetWorldContext();
	UWorld* FirstWorld = Instance->GetWorld();
	UWorld* SecondWorld = nullptr;
	ON_SCOPE_EXIT
	{
		Instance->Shutdown();
		GEngine->DestroyWorldContext(Instance->GetWorld());
		if (SecondWorld) { SecondWorld->DestroyWorld(false); }
		if (FirstWorld) { FirstWorld->DestroyWorld(false); }
	};
	UCastleBadgeSubsystem* Badges = Instance->GetSubsystem<UCastleBadgeSubsystem>();
	if (!TestNotNull(TEXT("真实 GameInstance 创建徽章子系统"), Badges))
	{
		return false;
	}
	TestEqual(TEXT("默认幸运值"), Badges->GetGambleLuckBonus(), 0.0);
	TestEqual(TEXT("默认蓝房数字"), Badges->GetBlueRoomNumberBonus(), int64{0});
	TestEqual(TEXT("默认升级赠牌"), Badges->GetUpgradeBonusRoomCards(), int64{0});
	TestEqual(TEXT("默认主塔血量"), Badges->GetMainTowerHealthMultiplier(), 1.0);
	TestEqual(TEXT("默认箭塔射速"), Badges->GetArcherFireRateMultiplier(), 1.0);
	TestEqual(TEXT("无徽章时常规房间射速"), Badges->GetGlobalTowerFireRateMultiplier(true), 1.0f);
	TestEqual(TEXT("无徽章时常规召唤物速度"), Badges->GetSummonSpeedMultiplier(true), 1.0f);
	TestEqual(TEXT("无徽章时敌人正常攻速"), Badges->GetEnemyAttackSpeedMultiplier(0.2f), 1.0f);

	FText Reason;
	bool bReplace;
	FCastleBadgeDefinition Badge;
	for (int32 Index = 0; Index < UCastleBadgeSubsystem::MaxBadges; ++Index)
	{
		FCastleBadgeDefinition::TryMakeStandard(static_cast<ECastleBadgeType>(Index), Badge);
		TestTrue(TEXT("前六枚直接加入"), Badges->TryAcquireBadge(Badge, bReplace, Reason));
		TestFalse(TEXT("前六枚无需选择"), bReplace);
	}
	TestTrue(TEXT("持有一枚幸运徽章"), FMath::IsNearlyEqual(Badges->GetGambleLuckBonus(), 0.15, 1e-6));
	TestEqual(TEXT("蓝房数字加一"), Badges->GetBlueRoomNumberBonus(), int64{1});
	TestEqual(TEXT("升级赠两张卡"), Badges->GetUpgradeBonusRoomCards(), int64{2});
	TestTrue(TEXT("主塔生命为 1.2 倍"), FMath::IsNearlyEqual(Badges->GetMainTowerHealthMultiplier(), 1.2, 1e-6));
	TestTrue(TEXT("箭塔射速为 1.2 倍"), FMath::IsNearlyEqual(Badges->GetArcherFireRateMultiplier(), 1.2, 1e-6));
	TestEqual(TEXT("未触发时为 1 倍"), Badges->GetGlobalTowerFireRateMultiplier(false), 1.0f);
	TestEqual(TEXT("首次掉血后全房间射速翻倍"), Badges->GetGlobalTowerFireRateMultiplier(true), 2.0f);
	TestEqual(TEXT("首次掉血后召唤物移动翻倍"), Badges->GetSummonSpeedMultiplier(true), 2.0f);

	FCastleBadgeDefinition::TryMakeStandard(ECastleBadgeType::LowMainTowerHealth, Badge);
	TestTrue(TEXT("第七枚成为候选"), Badges->TryAcquireBadge(Badge, bReplace, Reason));
	TestTrue(TEXT("第七枚需要替换选择"), bReplace);
	TestEqual(TEXT("候选徽章仍无效果"), Badges->GetEnemyAttackSpeedMultiplier(0.2f), 1.0f);
	Badges->DiscardPendingBadge();
	TestEqual(TEXT("舍弃后持有仍是六枚"), Badges->GetBadges().Num(), 6);
	TestTrue(TEXT("再次获取第七种徽章"), Badges->TryAcquireBadge(Badge, bReplace, Reason));
	TestTrue(TEXT("替换首槽"), Badges->TryReplaceBadgeAt(0, Badge, Reason));
	TestEqual(TEXT("替换后仍是六枚"), Badges->GetBadges().Num(), 6);
	TestEqual(TEXT("主塔 20% 生命时敌人攻速减半"), Badges->GetEnemyAttackSpeedMultiplier(0.20f), 0.5f);
	TestEqual(TEXT("主塔 21% 生命时攻速正常"), Badges->GetEnemyAttackSpeedMultiplier(0.21f), 1.0f);

	// 将同一 GameInstance 绑定到第二个游戏世界，再读取同一组槽位。
	SecondWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("第二个游戏世界"), SecondWorld))
	{
		return false;
	}
	SecondWorld->SetGameInstance(Instance);
	Context->SetCurrentWorld(SecondWorld);
	auto* AfterTravel = SecondWorld->GetGameInstance()->GetSubsystem<UCastleBadgeSubsystem>();
	TestEqual(TEXT("新世界继续使用原子系统"), AfterTravel, Badges);
	TestEqual(TEXT("六枚徽章完整保留"), AfterTravel->GetBadges().Num(), 6);
	TestEqual(TEXT("保留替换后的第一槽"), AfterTravel->GetBadges()[0].Type, ECastleBadgeType::LowMainTowerHealth);
	TestEqual(TEXT("新一场战斗没有保留触发标志"), AfterTravel->GetGlobalTowerFireRateMultiplier(false), 1.0f);

	for (int32 Index = 0; Index <= static_cast<int32>(ECastleBadgeType::LowMainTowerHealth); ++Index)
	{
		Badges->ClearAllBadgesForTesting();
		const auto Type = static_cast<ECastleBadgeType>(Index);
		FCastleBadgeDefinition::TryMakeStandard(Type, Badge);
		TestTrue(TEXT("第一枚进入空槽"), Badges->TryAcquireBadge(Badge, bReplace, Reason));
		TestTrue(TEXT("第二枚同名徽章进入另一槽"), Badges->TryAcquireBadge(Badge, bReplace, Reason));
		switch (Type)
		{
		case ECastleBadgeType::GambleLuck:
			TestTrue(TEXT("两枚幸运合计 0.30"), FMath::IsNearlyEqual(Badges->GetGambleLuckBonus(), 0.30, 1e-6));
			break;
		case ECastleBadgeType::BlueRoomNumber:
			TestEqual(TEXT("两枚蓝房数字加二"), Badges->GetBlueRoomNumberBonus(), int64{2});
			break;
		case ECastleBadgeType::UpgradeRoomCards:
			TestEqual(TEXT("两枚合计赠四张"), Badges->GetUpgradeBonusRoomCards(), int64{4});
			break;
		case ECastleBadgeType::MainTowerHealth:
			TestTrue(TEXT("两枚主塔生命徽章加算至 1.4 倍"),
				FMath::IsNearlyEqual(Badges->GetMainTowerHealthMultiplier(), 1.4, 1e-6));
			break;
		case ECastleBadgeType::ArcherFireRate:
			TestTrue(TEXT("两枚箭塔射速徽章加算至 1.4 倍"),
				FMath::IsNearlyEqual(Badges->GetArcherFireRateMultiplier(), 1.4, 1e-6));
			break;
		case ECastleBadgeType::FirstMainTowerDamage:
			TestEqual(TEXT("两枚首次掉血徽章仍固定两倍"), Badges->GetGlobalTowerFireRateMultiplier(true), 2.0f);
			TestEqual(TEXT("两枚首次掉血徽章仍固定两倍移速"), Badges->GetSummonSpeedMultiplier(true), 2.0f);
			break;
		case ECastleBadgeType::LowMainTowerHealth:
			TestEqual(TEXT("两枚低血量压制仍固定减半"), Badges->GetEnemyAttackSpeedMultiplier(0.20f), 0.5f);
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleSpeedRoomItemTest, "MovingCastle.Items.SpeedRoom.NPCRefreshAndExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleSpeedRoomItemTest::RunTest(const FString& Parameters)
{
	auto* Definition = CastleItemTests::LoadDefinition(TEXT("SpeedRoom"));
	if (!TestNotNull(TEXT("真实一级加速房定义"), Definition))
	{
		return false;
	}
	const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->BeginTearingDown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	auto* Support = World->GetSubsystem<UCastleSupportTowerSubsystem>();
	auto* Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
	auto* Mass = World->GetSubsystem<UMassEntitySubsystem>();
	if (!Support || !Units || !Mass)
	{
		AddError(TEXT("测试世界缺少支援塔或 Mass 子系统。"));
		return false;
	}
	auto* Config = NewObject<UMassEntityConfigAsset>(World);
	Config->GetMutableConfig().AddTrait(*NewObject<UCastleMassNPCTrait>(Config));
	TArray<FCastleMassUnitHandle> Handles;
	FText Reason;
	if (!TestTrue(TEXT("生成真实 Mass NPC"), Units->SpawnUnits(Config, {FTransform::Identity}, Handles, Reason))
		|| !TestEqual(TEXT("NPC 数量"), Handles.Num(), 1))
	{
		AddError(Reason.ToString());
		return false;
	}

	FCastleBuildingDescriptor Descriptor;
	if (!TestTrue(TEXT("生成可放置的加速房描述符"), Definition->CreateDescriptor(1, ECastleTileColor::Red, 4, Descriptor, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	Board->GridSize = FIntVector(1, 1, 2);
	CastleBuildingTests::ConfigureBoard(Board);
	FCastleGroundGenerationResult Ground;
	FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
	Cell.Coordinate = FIntVector::ZeroValue;
	Cell.Color = ECastleTileColor::Red;
	Cell.Number = 4;
	if (!TestTrue(TEXT("建立正式放置格"), Board->ApplyGroundGenerationResult(Ground, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	ACastleBuildingBase* Building = Board->PlaceBuilding(FIntVector::ZeroValue, Descriptor, true, Reason);
	if (!TestNotNull(TEXT("正式加速房可放置"), Building) || !CastleBuildingTests::CompletePlacement(Building))
	{
		AddError(Reason.ToString());
		return false;
	}
	auto* Effect = Cast<UCastleSpeedRoomEffectComponent>(Building->BuildingEffect);
	if (!TestNotNull(TEXT("建筑绑定原生加速组件"), Effect))
	{
		return false;
	}
	TestFalse(TEXT("一级加速房没有合成目标"), Building->CanMergeToNextLevel());
	AddExpectedError(TEXT("主动调用没有 NPC"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("主动调用未产生加速"), Building->TriggerBuildingEffect(FCastleBuildingTriggerContext{}));
	TestEqual(TEXT("失败调用恢复正式触发计数"), Effect->TriggerCount, int64{0});

	FCastleBuildingTriggerContext Entry;
	Entry.Source = ECastleBuildingTriggerSource::NPCEntry;
	Entry.NPC = Handles[0];
	auto ReadMultiplier = [&]()
	{
		return Mass->GetMutableEntityManager().GetFragmentDataChecked<FCastleMassUnitFragment>(Entry.NPC.Entity).SupportMovementMultiplier;
	};
	// 世界推进真实游戏时间，Tick 内共用支援倍率刷新路径。
	auto AdvanceWorld = [World, Support](const int32 Steps)
	{
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			World->Tick(LEVELTICK_All, 0.1f);
			Support->Tick(0.1f);
		}
	};
	TestTrue(TEXT("真实 NPC 进入触发加速"), Building->TriggerBuildingEffect(Entry));
	TestTrue(TEXT("没有空投时孤立加速仍唤醒支援 Tick"), Support->IsTickable());
	Support->Tick(0.0f);
	TestEqual(TEXT("移动倍率提升至 1.5"), ReadMultiplier(), 1.5f);
	AdvanceWorld(30);
	TestTrue(TEXT("再次经过刷新加速时长"), Building->TriggerBuildingEffect(Entry));
	AdvanceWorld(30);
	TestTrue(TEXT("已超出第一次的五秒期限"), World->GetTimeSeconds() > 5.0);
	TestEqual(TEXT("第二次仍在五秒内，倍率维持 1.5"), ReadMultiplier(), 1.5f);
	AdvanceWorld(25);
	TestEqual(TEXT("第二次增益到期后恢复 1.0"), ReadMultiplier(), 1.0f);
	TestEqual(TEXT("两次真实进入均记录成功"), Effect->TriggerCount, int64{2});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleItemWidgetAssetTest, "MovingCastle.Items.UI.WBPBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleItemWidgetAssetTest::RunTest(const FString& Parameters)
{
	// 生成类仍须继承原生基类，设计器中的必需子控件由正式资产提供。
	auto Verify = [this](const TCHAR* Name, UClass* Parent, const std::initializer_list<const TCHAR*> Bindings)
	{
		const FString Path = FString::Printf(TEXT("/Game/MovingCastle/UI/Items/%s.%s_C"), Name, Name);
		UClass* Class = LoadClass<UUserWidget>(nullptr, *Path);
		auto* Generated = Cast<UWidgetBlueprintGeneratedClass>(Class);
		if (!TestNotNull(Name, Generated) || !TestEqual(TEXT("占位 WBP 使用预期 C++ 父类"), Generated->GetSuperClass(), Parent))
		{
			return;
		}
		const UWidgetTree* Tree = Generated->GetWidgetTreeArchetype();
		if (TestNotNull(TEXT("WBP 已生成 WidgetTree"), Tree))
		{
			for (const TCHAR* Binding : Bindings)
			{
				TestNotNull(Binding, Tree->FindWidget(FName(Binding)));
			}
		}
	};
	Verify(TEXT("WBP_TempBuildBar"), UCastleTempBuildBarWidget::StaticClass(),
		{TEXT("StackCount"), TEXT("FrontCard")});
	Verify(TEXT("WBP_BadgeBar"), UCastleBadgeBarWidget::StaticClass(),
		{TEXT("BadgeSlots")});
	Verify(TEXT("WBP_BadgeReplaceModal"), UCastleBadgeReplaceModalWidget::StaticClass(),
		{TEXT("PendingBadgeLabel"), TEXT("ReplaceSlots"), TEXT("DiscardButton")});
	Verify(TEXT("WBP_WildcardRoomPicker"), UCastleWildcardRoomPickerWidget::StaticClass(),
		{TEXT("ClaimButton"), TEXT("ClaimLabel"), TEXT("PickerPanel"), TEXT("RewardTitle"), TEXT("StatusText"),
		TEXT("DefinitionChoices"), TEXT("ColorChoices"), TEXT("NumberChoices"), TEXT("ConfirmButton"), TEXT("CancelButton"),
		TEXT("CloseButton"), TEXT("OptionStyleTemplate"), TEXT("OptionTextTemplate"), TEXT("ColorStyleTemplate"),
		TEXT("PreviewCardSlot"), TEXT("PreviewPlaceholder")});

	// 卡牌初始化依赖本地玩家视口，采用主地图真实棋盘与玩家上下文。
	if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Deadline = FPlatformTime::Seconds() + 45.0]()
	{
		UWorld* World = GEditor->PlayWorld;
		auto* Player = World ? World->GetFirstPlayerController() : nullptr;
		if (!Player && FPlatformTime::Seconds() < Deadline) { return false; }
		const auto* Board = World ? Cast<ACastleBoard>(UGameplayStatics::GetActorOfClass(World, ACastleBoard::StaticClass())) : nullptr;
		if (!TestNotNull(TEXT("万能卡摄影棋盘"), Board) || !TestNotNull(TEXT("万能卡本地玩家"), Player)) { return true; }
		auto* Definition = CastleItemTests::LoadDefinition(TEXT("NormalRoom"));
		auto* Backpack = NewObject<UCastleBackpackComponent>(World->GetGameInstance());
		const auto PickerClass = LoadClass<UCastleWildcardRoomPickerWidget>(nullptr,
			TEXT("/Game/MovingCastle/UI/Items/WBP_WildcardRoomPicker.WBP_WildcardRoomPicker_C"));
		const auto TempClass = LoadClass<UCastleTempBuildBarWidget>(nullptr,
			TEXT("/Game/MovingCastle/UI/Items/WBP_TempBuildBar.WBP_TempBuildBar_C"));
		if (!Definition || !PickerClass || !TempClass)
		{
			AddError(TEXT("缺少验收使用的普通房间或已保存的 WBP 类。"));
			return true;
		}
		auto* Picker = CreateWidget<UCastleWildcardRoomPickerWidget>(Player, PickerClass);
		auto* Temp = CreateWidget<UCastleTempBuildBarWidget>(Player, TempClass);
		FText Reason;
		if (!TestNotNull(TEXT("实例化 WBP 万能卡"), Picker) || !TestNotNull(TEXT("实例化 WBP 临时栏"), Temp)
			|| !TestTrue(TEXT("WBP 临时栏的完整绑定"), Temp->BindBackpack(Backpack))
			|| !TestTrue(TEXT("建筑栏填满十张"), CastleItemTests::FillBackpack(*Backpack, *Definition, Reason)))
		{
			return true;
		}
		TestEqual(TEXT("没有临时卡时折叠"), Temp->GetVisibility(), ESlateVisibility::Collapsed);

		FCastleWildcardRoomSpec Spec;
		Spec.AllowedNumbers = {7};
		const TArray<TObjectPtr<UCastleBuildingDefinition>> Pool{Definition};
		UCastlePendingRoomReward* Reward = nullptr;
		if (!TestTrue(TEXT("准备颜色待选，建筑与数字固定的万能卡"),
			UCastleItemGrantLibrary::TryCreateWildcardReward(INVTEXT("颜色房间"), Spec, Backpack, Pool, Reward, Reason))
			|| !TestTrue(TEXT("WBP 万能卡的完整绑定"), Picker->BindReward(Reward, Reason)))
		{
			return true;
		}
		auto* Claim = Cast<UButton>(Picker->GetWidgetFromName(TEXT("ClaimButton")));
		auto* Confirm = Cast<UButton>(Picker->GetWidgetFromName(TEXT("ConfirmButton")));
		auto* Close = Cast<UButton>(Picker->GetWidgetFromName(TEXT("CloseButton")));
		auto* Panel = Picker->GetWidgetFromName(TEXT("PickerPanel"));
		auto* PreviewSlot = Cast<USizeBox>(Picker->GetWidgetFromName(TEXT("PreviewCardSlot")));
		auto* Placeholder = Picker->GetWidgetFromName(TEXT("PreviewPlaceholder"));
		auto* Definitions = Cast<UPanelWidget>(Picker->GetWidgetFromName(TEXT("DefinitionChoices")));
		auto* Colors = Cast<UPanelWidget>(Picker->GetWidgetFromName(TEXT("ColorChoices")));
		auto* Numbers = Cast<UPanelWidget>(Picker->GetWidgetFromName(TEXT("NumberChoices")));
		if (!Claim || !Confirm || !Close || !Panel || !PreviewSlot || !Placeholder || !Definitions || !Colors || !Numbers)
		{
			AddError(TEXT("实例化的万能卡 WBP 缺少必需的布局或按钮。"));
			return true;
		}
		TestEqual(TEXT("点击获取前 WBP 属性面板折叠"), Panel->GetVisibility(), ESlateVisibility::Collapsed);
		Claim->OnClicked.Broadcast();
		TestEqual(TEXT("点击获取后显示 WBP 属性面板"), Panel->GetVisibility(), ESlateVisibility::Visible);
		TestFalse(TEXT("未选颜色时禁用确认"), Confirm->GetIsEnabled());
		TestNull(TEXT("未选齐时没有实例化预览卡"), PreviewSlot->GetContent());
		TestEqual(TEXT("未选齐时显示线稿"), Placeholder->GetVisibility(), ESlateVisibility::HitTestInvisible);
		if (!TestEqual(TEXT("固定定义只生成一个按钮"), Definitions->GetChildrenCount(), 1)
			|| !TestEqual(TEXT("固定数字只生成一个按钮"), Numbers->GetChildrenCount(), 1)
			|| !TestEqual(TEXT("只开放五种颜色"), Colors->GetChildrenCount(), 5))
		{
			return true;
		}
		TestFalse(TEXT("唯一建筑按钮自动锁定"), Definitions->GetChildAt(0)->GetIsEnabled());
		TestFalse(TEXT("唯一数字按钮自动锁定"), Numbers->GetChildAt(0)->GetIsEnabled());
		Close->OnClicked.Broadcast();
		TestEqual(TEXT("右上关闭取消本次选择"), Claim->GetVisibility(), ESlateVisibility::Visible);
		TestEqual(TEXT("取消后无卡进入临时栏"), Backpack->GetTempItemCount(), 0);
		Claim->OnClicked.Broadcast();
		UButton* Yellow = Cast<UButton>(Colors->GetChildAt(3));
		if (!TestNotNull(TEXT("第四个选项是可点击颜色按钮"), Yellow))
		{
			return true;
		}
		Yellow->OnClicked.Broadcast();
		TestTrue(TEXT("选完颜色后允许确认"), Confirm->GetIsEnabled());
		auto* CardPreview = Cast<UCastleShopCardWidget>(PreviewSlot->GetContent());
		if (!TestNotNull(TEXT("选择黄色后已挂载正式 3D 卡牌"), CardPreview)) { return true; }
		TestEqual(TEXT("预览建筑与唯一候选一致"), CardPreview->GetDescriptor().Definition.Get(), Definition);
		TestEqual(TEXT("预览卡应用黄色"), CardPreview->GetDescriptor().Color, ECastleTileColor::Yellow);
		TestEqual(TEXT("预览卡保留锁定数字"), CardPreview->GetDescriptor().Number, 7);
		TestEqual(TEXT("真实预览替代线稿"), Placeholder->GetVisibility(), ESlateVisibility::Collapsed);
		Confirm->OnClicked.Broadcast();
		TestTrue(TEXT("WBP 确认后奖励已领取"), Reward->IsClaimed());
		TestNull(TEXT("领取后释放预览卡"), PreviewSlot->GetContent());
		if (TestEqual(TEXT("满包时 WBP 确认发入临时栏"), Backpack->GetTempItemCount(), 1))
		{
			const FCastleBuildingDescriptor Card = Backpack->GetTempItems()[0];
			TestEqual(TEXT("颜色选项提交实际牌面"), Card.Color, ECastleTileColor::Yellow);
			TestEqual(TEXT("入库数字保留锁定值"), Card.Number, 7);
		}
		TestEqual(TEXT("临时栏收到事件后显示"), Temp->GetVisibility(), ESlateVisibility::HitTestInvisible);
		if (auto* Count = Cast<UTextBlock>(Temp->GetWidgetFromName(TEXT("StackCount"))))
		{
			TestEqual(TEXT("叠牌文本显示当前数量"), Count->GetText().ToString(), FString(TEXT("临时建筑栏 (1)")));
		}
		FCastleBuildingDescriptor Removed;
		TestTrue(TEXT("单步消费自动补位"), Backpack->TryConsumeItemAt(0, Removed, Reason));
		TestEqual(TEXT("队列清空后 WBP 恢复隐藏"), Temp->GetVisibility(), ESlateVisibility::Collapsed);
		return true;
	}));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
	return true;
}

#endif
