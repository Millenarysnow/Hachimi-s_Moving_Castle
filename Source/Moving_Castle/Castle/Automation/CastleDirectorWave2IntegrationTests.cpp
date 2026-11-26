#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "Castle/Automation/CastlePIEAutomation.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Items/CastleBadgeDataAsset.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/Items/CastleItemGrantLibrary.h"
#include "Castle/Items/CastlePendingRoomReward.h"
#include "Castle/SkyIslands/CastleSkyIslandTestFixture.h"
#include "Castle/SkyIslands/Shop/CastleShopSkyIslandDataAsset.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"
#include "Castle/Support/CastleSupportFlightActors.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "Castle/UI/Director/CastleTowerUpgradeRewardWidget.h"
#include "Castle/UI/Items/CastleWildcardRoomPickerWidget.h"
#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleBuildingTestUtilities.h"
#include "CastleMainTowerComponent.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleMassTraits.h"
#include "MassEntityConfigAsset.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Components/PanelWidget.h"

/** 使用 MainMap 的实际主塔、正式控件和库存。只有战斗清场事件与空岛类型由测试显式驱动。 */
class FCastleDirectorWave2Scenario final : public IAutomationLatentCommand
{
public:
	explicit FCastleDirectorWave2Scenario(FAutomationTestBase& InTest) : Test(InTest) {}

	bool Update() override
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
			Test.AddError(TEXT("正式地图导演未能启动。"));
			return true;
		}
		if (Stage == 0) { VerifyGrowthAndEnter(*Director); ++Stage; return false; }
		if (Stage == 1)
		{
			Test.TestEqual(TEXT("离开后恢复编辑"), Director->Phase, ECastleDirectorPhase::Editing);
			Test.TestEqual(TEXT("离开只推进一步"), Director->ClearedSteps, BeforeIsland + 1);
			Test.TestFalse(TEXT("离开恢复主塔建造面"), Director->Board->IsPlacementSurfacePresentationHidden());
			Test.TestTrue(TEXT("离开准备并激活下一批节点"), Director->Nodes.Num() >= 2);
			Test.TestTrue(TEXT("玩家返回入岛前的主塔视角位置"), Director->Controller->GetPawn()->GetActorLocation().Equals(ReturnLocation, 5.0f));
			Test.TestFalse(TEXT("测试空岛已销毁"), static_cast<bool>(TActorIterator<ACastleSkyIslandTestFixture>(World)));
			return true;
		}
		return true;
	}

private:
	void VerifyGrowthAndEnter(UCastleDirectorSubsystem& Director)
	{
		FText Reason;
		auto* Shop = Director.Controller->GetShop();
		Test.TestEqual(TEXT("开局 8 名普通 NPC"), Director.RunNPCCount, 8);
		Test.TestEqual(TEXT("开局可标两个路径点"), Director.PatrolRoute->GetMaxPlayerWaypoints(), 2);
		const int32 Balloons[] = {5, 10, 20, 40};
		const int32 Rooms[] = {0, 2, 5, 10};
		const float Health[] = {1000.0f, 2000.0f, 4000.0f, 8000.0f};
		Director.Config = DuplicateObject(Director.Config.Get(), Director.GetWorld());
		Director.Config->RewardExp = Director.Config->ExpPerLevel;
		for (int32 Level = 1; Level <= 4; ++Level)
		{
			if (Level > 1)
			{
				Director.Tower->ApplyDamage(25.0f);
				Director.DestroyNodes();
				Director.SetPhase(ECastleDirectorPhase::Reward);
				if (!Test.TestTrue(TEXT("经验奖励成功提交"), Director.TryChooseReward(ECastleDirectorReward::Experience, Reason)))
				{
					Test.AddError(Reason.ToString());
					return;
				}
				Test.TestEqual(TEXT("各级待领房间、路径点和 NPC 分行"), Director.UpgradeRewards.Num(), Rooms[Level - 1] + 1 + (Level == 4 ? 1 : 0));
				Director.ClaimTowerUpgradeReward(0);
				Test.TestEqual(TEXT("逐次领取路径点"), Director.PatrolRoute->GetMaxPlayerWaypoints(), Level + 1);
				if (Level == 2) { VerifyWildcardPicker(Director); }
				if (Level == 4) { Director.ClaimTowerUpgradeReward(0); }
				Director.DiscardTowerUpgradeRewards();
				Test.TestTrue(TEXT("关闭丢弃全部未领取卡"), Director.UpgradeRewards.IsEmpty() && Director.UpgradeRoomRewards.IsEmpty());
			}
			Test.TestEqual(TEXT("主塔等级"), Director.Level, Level);
			Test.TestEqual(TEXT("开放层数"), Director.Board->GetUnlockedLayerCount(), Level * 2 + 1);
			Test.TestEqual(TEXT("房间上限"), Shop->RoomLimit, Level * 100 + 50);
			Test.TestEqual(TEXT("商店数字上限"), Director.GetCurrentMaxShopNumber(), Level + 2);
			Test.TestEqual(TEXT("气球容量"), Director.Tower->MaxBalloons, Balloons[Level - 1]);
			Test.TestEqual(TEXT("最大生命"), Director.Tower->MaxHealth, Health[Level - 1]);
			Test.TestEqual(TEXT("升级补满生命"), Director.Tower->CurrentHealth, Health[Level - 1]);
		}
		Test.TestEqual(TEXT("Lv4 领取后有 13 名 NPC"), Director.RunNPCCount, 13);
		// 按正式精英关号注入两次清场，覆盖前两个精英关的解锁与奖励。
		for (int32 EliteStage = 1; EliteStage <= 2; ++EliteStage)
		{
			const int32 StageNumber = EliteStage * Director.Config->EliteEveryNSteps;
			Director.DestroyNodes();
			Director.ClearedSteps = StageNumber - 1;
			Director.SetPhase(ECastleDirectorPhase::Battle);
			Director.Tower->ApplyDamage(100.0f);
			Director.HandleBattleCleared(ECastleBattleKind::Elite, StageNumber);
			Test.TestEqual(TEXT("精英自动提交后回编辑"), Director.Phase, ECastleDirectorPhase::Editing);
			Test.TestEqual(TEXT("满级经验不会越界"), Director.Level, 4);
			Test.TestEqual(TEXT("精英同一笔奖励补血"), Director.Tower->CurrentHealth, 8000.0f);
			Test.TestTrue(TEXT("精英同一笔奖励免费换批打开"), Shop->IsShopOpen() && Shop->GetOffers().Num() == 6);
			Test.TestTrue(TEXT("精英解锁标签写入战斗池"), Director.Combat->HasUnlockedEnemyTag(
				EliteStage == 1 ? FName(TEXT("Unlock.MiasmaWelp")) : FName(TEXT("Unlock.LavaGoliath"))));
			Shop->CloseShop();
		}
		VerifyBadgesAndSale(Director);
		VerifySupportTravel(Director);
		Director.DestroyNodes();
		// 测试只改变内存中的 DA，用权重产生确定的第 2 步休息岛。
		Director.ClearedSteps = 1;
		Director.Config->NodeCountMin = Director.Config->NodeCountMax = 2;
		Director.Config->NormalNodeWeight = Director.Config->RewardNodeWeight = 0;
		Director.Config->RestNodeWeight = 1;
		if (!Test.TestTrue(TEXT("准备确定性空岛节点"), Director.PrepareNodes(2, Director.Nodes, Reason))) { return; }
		for (ACastleDirectorNodeActor* Node : Director.Nodes) { Node->ActivateNode(); }
		ACastleDirectorNodeActor* IslandNode = Director.Nodes.Last();
		// 正式资产已登记四类玩法；仅清空当前用例的世界内副本，独立验证缺少映射的拒绝路径。
		Director.Config->SkyIslandClasses.Reset();
		Test.AddExpectedError(TEXT("尚未配置具体玩法类"), EAutomationExpectedErrorFlags::Contains, 1);
		Test.TestFalse(TEXT("配置副本留空时拒绝进入"), Director.TryEnterSkyIsland(*IslandNode, *Director.Config, Reason));
		Test.TestEqual(TEXT("拒绝后仍可选择其他节点"), Director.Phase, ECastleDirectorPhase::Editing);
		Director.Config->SkyIslandClasses.Add(ECastleSkyIslandType::Rest, ACastleSkyIslandTestFixture::StaticClass());
		BeforeIsland = Director.ClearedSteps;
		ReturnLocation = Director.Controller->GetPawn()->GetActorLocation();
		Director.HandleNodeTouched(IslandNode);
		Director.Tick(0.0f);
		if (!Test.TestNotNull(TEXT("节点生成配置夹具"), Director.ActiveSkyIsland.Get())) { return; }
		Test.TestEqual(TEXT("进入空岛阶段"), Director.Phase, ECastleDirectorPhase::SkyIsland);
		const FVector Arrival = Director.ActiveSkyIsland->GetPlayerArrivalTransform().GetLocation();
		Test.TestTrue(TEXT("玩家位于到达锚点"), Director.Controller->GetPawn()->GetActorLocation().Equals(Arrival, 5.0f));
		const auto* Item = Director.ActiveSkyIsland->FindComponentByClass<UCastleSkyIslandFixtureInteractComponent>();
		Test.TestTrue(TEXT("准星可调用普通测试物件"), Item && Director.TryInteractWithSkyIsland(Arrival, Item->GetComponentLocation(), Reason));
		if (Item) { Test.TestEqual(TEXT("测试物件的实际交互次数"), Item->GetInteractionCount(), 1); }
		const auto* Portal = Director.ActiveSkyIsland->FindComponentByClass<UCastleSkyIslandDepartureComponent>();
		Test.TestTrue(TEXT("准星真实命中离开传送门"), Portal && Director.TryInteractWithSkyIsland(Arrival, Portal->GetComponentLocation(), Reason));
		Director.Tick(0.0f);
	}

	void VerifyWildcardPicker(UCastleDirectorSubsystem& Director)
	{
		auto* Rows = Cast<UPanelWidget>(Director.TowerUpgradeWidget->GetWidgetFromName(TEXT("RewardRows")));
		if (!Test.TestNotNull(TEXT("正式 WBP 绑定动态奖励行容器"), Rows)) { return; }
		auto* RewardButton = Cast<UButton>(Rows->GetChildAt(0));
		if (!Test.TestNotNull(TEXT("每张自定义房有独立领取按钮"), RewardButton)) { return; }
		const int32 Before = Director.Board->PrototypeBackpack->GetItemCount();
		const int32 BeforeTemp = Director.Board->PrototypeBackpack->GetTempItems().Num();
		RewardButton->OnClicked.Broadcast();
		auto* Picker = Director.WildcardPicker.Get();
		if (!Test.TestNotNull(TEXT("点击一张牌立即打开正式万能卡 WBP"), Picker)) { return; }
		auto* Cancel = Cast<UButton>(Picker->GetWidgetFromName(TEXT("CancelButton")));
		if (!Test.TestNotNull(TEXT("绑定取消按钮"), Cancel)) { return; }
		Cancel->OnClicked.Broadcast();
		Test.TestTrue(TEXT("取消恢复升级面板且保留该张卡"), !Director.WildcardPicker && Director.UpgradeRewards.Num() == 2);
		Test.TestEqual(TEXT("取消未写入未定义卡"), Director.Board->PrototypeBackpack->GetItemCount(), Before);
		Test.TestEqual(TEXT("取消也不写入临时栏"), Director.Board->PrototypeBackpack->GetTempItems().Num(), BeforeTemp);
		RewardButton->OnClicked.Broadcast();
		Picker = Director.WildcardPicker.Get();
		if (!Test.TestNotNull(TEXT("取消后可重新选择同一项"), Picker)) { return; }
		const auto Select = [&](const FName PanelName, const int32 Index, const int32 ExpectedCount)
		{
			auto* Options = Cast<UPanelWidget>(Picker->GetWidgetFromName(PanelName));
			if (!Test.TestNotNull(PanelName.ToString(), Options)) { return false; }
			Test.TestEqual(PanelName.ToString(), Options->GetChildrenCount(), ExpectedCount);
			auto* Button = Cast<UButton>(Options->GetChildAt(Index));
			if (!Test.TestNotNull(TEXT("选项按钮已生成"), Button)) { return false; }
			Button->OnClicked.Broadcast();
			return true;
		};
		auto* Confirm = Cast<UButton>(Picker->GetWidgetFromName(TEXT("ConfirmButton")));
		if (Test.TestNotNull(TEXT("绑定确认按钮"), Confirm)
			&& Select(TEXT("DefinitionChoices"), 0, Director.Board->BuildingDefinitions.Num())
			&& Select(TEXT("ColorChoices"), 2, 4) && Select(TEXT("NumberChoices"), 3, 4))
		{
			Confirm->OnClicked.Broadcast();
			Test.TestTrue(TEXT("只移除已确认的自定义房"), !Director.WildcardPicker && Director.UpgradeRewards.Num() == 1);
			const auto* Backpack = Director.Board->PrototypeBackpack.Get();
			Test.TestEqual(TEXT("确认后主栏和临时栏合计只增加一张确定牌"),
				Backpack->GetItemCount() + Backpack->GetTempItems().Num(), Before + BeforeTemp + 1);
			if (Before == Backpack->Capacity)
			{
				Test.TestEqual(TEXT("主栏已满则确定牌进入临时栏"), Backpack->GetTempItems().Num(), BeforeTemp + 1);
				Test.TestEqual(TEXT("临时栏牌面保留所选数字"), Backpack->GetTempItems().Last().Number, 4);
			}
		}
	}

	void VerifyBadgesAndSale(UCastleDirectorSubsystem& Director)
	{
		auto* Badges = UCastleBadgeSubsystem::GetForWorld(Director.GetWorld());
		const auto* ShopCatalog = LoadObject<UCastleShopSkyIslandDataAsset>(nullptr,
			TEXT("/Game/MovingCastle/Data/SkyIslands/DA_ShopSkyIsland.DA_ShopSkyIsland"));
		if (!Test.TestNotNull(TEXT("正式 GameInstance 徽章组件"), Badges)
			|| !Test.TestNotNull(TEXT("正式徽章资产目录"), ShopCatalog)) { return; }
		// 主地图 HUD 已订阅徽章库存。使用带真实 PNG 的同一资产发放入口，触发界面与导演更新。
		const auto FindBadge = [ShopCatalog](const ECastleBadgeType Type) -> const UCastleBadgeDataAsset*
		{
			const auto* Found = ShopCatalog->BadgePool.FindByPredicate([Type](const UCastleBadgeDataAsset* Asset)
			{
				return IsValid(Asset) && Asset->Badge.Type == Type;
			});
			return Found ? Found->Get() : nullptr;
		};
		FText Reason;
		bool bReplace = false;
		for (const ECastleBadgeType Type : {ECastleBadgeType::BalloonRack, ECastleBadgeType::QuickstringPass,
			ECastleBadgeType::BlueAbacus, ECastleBadgeType::FirepowerCard,
			ECastleBadgeType::BurstAlarm, ECastleBadgeType::EmergencyBallast})
		{
			const auto* Asset = FindBadge(Type);
			if (!Test.TestTrue(TEXT("正式徽章入口发放"), UCastleItemGrantLibrary::TryGrantBadge(Asset, Badges, bReplace, Reason))) { return; }
		}
		Test.TestTrue(TEXT("主塔同步 +20% 生命"), FMath::IsNearlyEqual(Director.Tower->MaxHealth, 9600.0f));
		auto* Mass = Director.GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
		Director.SetPhase(ECastleDirectorPhase::Battle);
		Director.Tower->ApplyDamage(0.0f);
		Test.TestFalse(TEXT("零伤害不会开启首次受击加成"), Director.IsFirstTowerDamageTriggeredInBattle());
		Director.Tower->ApplyDamage(Director.Tower->MaxHealth * 0.85f);
		Test.TestTrue(TEXT("首次实际受伤开启本场增益"), Director.IsFirstTowerDamageTriggeredInBattle());
		Test.TestEqual(TEXT("低血量攻速传入 Mass"), Mass->GetGlobalEnemyAttackSpeedMultiplier(), 0.5f);
		Director.Tower->ApplyHealing(Director.Tower->MaxHealth);
		Test.TestEqual(TEXT("恢复生命后解除低血压制"), Mass->GetGlobalEnemyAttackSpeedMultiplier(), 1.0f);
		Director.SetPhase(ECastleDirectorPhase::Editing);
		Test.TestFalse(TEXT("战斗结束时关闭首次受击增益"), Director.IsFirstTowerDamageTriggeredInBattle());
		const auto* UpgradeBadge = FindBadge(ECastleBadgeType::WorkshopRefund);
		if (!Test.TestTrue(TEXT("第七枚徽章使用正式替换入口"),
			UCastleItemGrantLibrary::TryGrantBadge(UpgradeBadge, Badges, bReplace, Reason))) { return; }
		Test.TestTrue(TEXT("第七枚徽章等待确认替换"), bReplace);
		Test.TestTrue(TEXT("替换一枚后合成徽章生效"), Badges->TryReplaceBadgeAt(3, UpgradeBadge->Badge, Reason));
		Test.TestEqual(TEXT("升级徽章赠两张牌"), Badges->GetUpgradeBonusRoomCards(), int64{2});
		auto* Shop = Director.Controller->GetShop();
		if (!Test.TestTrue(TEXT("打开商店验证真实出售"), Shop->TryOpenShop(Reason))) { return; }
		auto* Backpack = Director.Board->PrototypeBackpack.Get();
		const auto* Definition = Director.Board->FindBuildingDefinition(ECastleBuildingType::NormalRoom);
		FCastleBuildingDescriptor Card;
		if (!Test.TestTrue(TEXT("测试卡由正式定义构建"), Definition && Definition->CreateDescriptor(1, ECastleTileColor::Blue, 3, Card, Reason))) { return; }
		TArray<FCastleBuildingDescriptor> Cards;
		Cards.Init(Card, Backpack->Capacity + 1);
		Test.TestTrue(TEXT("房间卡整批进包及临时栏"), Backpack->TryAddItemsAllowOverflow(Cards, Reason));
		const int32 BeforeTempCount = Backpack->GetTempItems().Num();
		Test.TestFalse(TEXT("非法槽位出售拒绝"), Shop->TrySellBackpackItem(-1, Reason));
		Test.TestEqual(TEXT("失败不触发补位"), Backpack->GetTempItems().Num(), BeforeTempCount);
		Test.TestTrue(TEXT("出售入账"), Shop->TrySellBackpackItem(0, Reason));
		Test.TestEqual(TEXT("售出后临时栏队首补入"), Backpack->GetTempItems().Num(), BeforeTempCount - 1);
		Test.TestEqual(TEXT("补位后建筑栏仍满"), Backpack->GetItemCount(), Backpack->Capacity);
		Shop->CloseShop();

		// 前侧真实塔格提供颜色或数字匹配；选蓝牌以验证数值仅用于实际结算。
		const FIntVector Cell = Director.Board->GetBoardCenterCoordinate();
		const FVector Center = Director.Board->GetGridWorldLocation(Cell);
		FCastleGridCursorTarget Target;
		if (!Test.TestTrue(TEXT("准星定位空闲主塔建造面"), Director.Board->TryResolveGridCursorTarget(
			Center - Director.Board->GetActorForwardVector() * Director.Board->CellSize * 2.0, Center, Target))) { return; }
		const FCastleTowerSideCell* Side = Director.Board->FindTowerSideCell(Target.Cell, Target.Face);
		if (!Test.TestNotNull(TEXT("命中真实塔侧格"), Side)) { return; }
		const auto* Archer = Director.Board->FindBuildingDefinition(ECastleBuildingType::ArcherTower);
		FCastleBuildingDescriptor BlueArcher;
		if (!Test.TestTrue(TEXT("生成蓝色箭塔测试牌"), Archer && Archer->CreateDescriptor(
			1, ECastleTileColor::Blue, Side->Number, BlueArcher, Reason))) { return; }
		FCastleBuildingDescriptor Existing;
		if (!Backpack->TryRemoveItemAt(0, Existing, Reason) || !Backpack->TryInsertItemAt(0, BlueArcher, Reason)) { Test.AddError(Reason.ToString()); return; }
		const int32 BeforePlacement = Backpack->GetTempItems().Num();
		Test.TestNull(TEXT("无效槽位不会放置或补位"), Director.Board->PlaceBuildingFromBackpack(
			Target.AdjacentCell, Backpack, -1, false, Reason));
		Test.TestEqual(TEXT("失败保留临时栏"), Backpack->GetTempItems().Num(), BeforePlacement);
		ACastleBuildingBase* Building = Director.Board->PlaceBackpackBuildingAtCursor(Target, 0, Reason);
		if (!Test.TestNotNull(TEXT("从满包正式建造入口放置"), Building)) { Test.AddError(Reason.ToString()); return; }
		Test.TestEqual(TEXT("成功放置后临时栏补位"), Backpack->GetTempItems().Num(), BeforePlacement - 1);
		Test.TestEqual(TEXT("蓝塔有效数字增加 1"), Building->GetEffectiveBuildingNumber(), static_cast<int64>(BlueArcher.Number) + 1);
		auto* Support = Director.GetWorld()->GetSubsystem<UCastleSupportTowerSubsystem>();
		Test.TestTrue(TEXT("箭塔实际冷却使用 1.2 倍"), FMath::IsNearlyEqual(Support->GetTowerFireRateMultiplier(*Building), 1.2f));
		Director.SetPhase(ECastleDirectorPhase::Battle);
		Director.Tower->ApplyDamage(1.0f);
		Test.TestTrue(TEXT("首次受击叠加箭塔射速"), FMath::IsNearlyEqual(Support->GetTowerFireRateMultiplier(*Building), 2.4f));
		Director.SetPhase(ECastleDirectorPhase::Editing);

		// 沿刚完成箭塔的外侧继续放置同号同色塔，正式飞入结束回调执行合成。
		CastleBuildingTests::CompletePlacement(Building);
		FCastleGridCursorTarget MergeTarget = Target;
		MergeTarget.Surface = ECastleGridCursorSurface::Building;
		MergeTarget.Cell = Target.AdjacentCell;
		MergeTarget.AdjacentCell += Target.AdjacentCell - Target.Cell;
		const int32 BeforeMerge = Backpack->GetTempItems().Num();
		ACastleBuildingBase* Partner = Director.Board->PlaceBuildingAtCursor(MergeTarget, BlueArcher, true, Reason);
		if (!Test.TestNotNull(TEXT("放置参加合成的第二座箭塔"), Partner)) { Test.AddError(Reason.ToString()); return; }
		CastleBuildingTests::CompletePlacement(Partner);
		const auto CardsAfterMerge = Backpack->GetTempItems();
		Test.TestEqual(TEXT("成功合成赠两张卡至已满建筑栏的临时栏"), CardsAfterMerge.Num(), BeforeMerge + 2);
		for (int32 Index = BeforeMerge; Index < CardsAfterMerge.Num(); ++Index)
		{
			const auto& Reward = CardsAfterMerge[Index];
			Test.TestTrue(TEXT("赠牌是有效一级房间且数字在当前 1–6"),
				Reward.Level == 1 && Reward.Number >= 1 && Reward.Number <= Director.GetCurrentMaxShopNumber());
		}
	}

	/** 同一批飞行实体跨过首次受击事件，等时测量位置，排除重新生成和航点切换的影响。 */
	void VerifySupportTravel(UCastleDirectorSubsystem& Director)
	{
		FText Reason;
		ACastleBoard* Board = Director.Board.Get();
		UWorld* World = Director.GetWorld();
		const FIntVector Top = Board->GetBoardCenterCoordinate() + FIntVector(0, 0, Board->GetUnlockedLayerCount() - 1);
		const FVector Center = Board->GetGridWorldLocation(Top);
		const auto Place = [&](const ECastleBuildingType Type, const FVector& Direction) -> ACastleBuildingBase*
		{
			// 选三级成长后新开放的塔顶层，直接用该侧格色号定义正式一级测试塔。
			FCastleGridCursorTarget Target;
			if (!Test.TestTrue(TEXT("高层塔侧射线命中"), Board->TryResolveGridCursorTarget(
				Center + Direction * Board->CellSize * 2.0, Center, Target)
				&& Target.Surface == ECastleGridCursorSurface::MainTowerSide)) { return nullptr; }
			const auto* Cell = Board->FindTowerSideCell(Target.Cell, Target.Face);
			const auto* Definition = Board->FindBuildingDefinition(Type);
			FCastleBuildingDescriptor Card;
			if (!Test.TestTrue(TEXT("正式定义生成蓝色测试塔"), Cell && Definition
				&& Definition->CreateDescriptor(1, ECastleTileColor::Blue, Cell->Number, Card, Reason))) { return nullptr; }
			ACastleBuildingBase* Building = Board->PlaceBuildingAtCursor(Target, Card, false, Reason);
			if (!Test.TestTrue(TEXT("测试塔完成正式放置"), CastleBuildingTests::CompletePlacement(Building)))
			{
				Test.AddError(Reason.ToString());
				return nullptr;
			}
			return Building;
		};
		ACastleBuildingBase* Cannon = Place(ECastleBuildingType::CannonTower, FVector(1, 0, 0));
		ACastleBuildingBase* Hive = Place(ECastleBuildingType::BeeHive, FVector(0, 1, 0));
		ACastleBuildingBase* Balloon = Place(ECastleBuildingType::BalloonTower, FVector(0, -1, 0));
		if (!Cannon || !Hive || !Balloon) { return; }
		auto* Support = World->GetSubsystem<UCastleSupportTowerSubsystem>();
		auto* Mass = World->GetSubsystem<UCastleMassUnitSubsystem>();
		auto* EnemyConfig = NewObject<UMassEntityConfigAsset>();
		auto* Trait = NewObject<UCastleMassEnemyTrait>(EnemyConfig);
		Trait->MaxHealth = 1000.0f;
		EnemyConfig->GetMutableConfig().AddTrait(*Trait);
		TArray<FCastleMassUnitHandle> Enemies;
		if (!Test.TestTrue(TEXT("生成不移动的正式 Mass 飞行目标"), Mass->SpawnUnits(EnemyConfig,
			{FTransform(Hive->GetActorLocation() + FVector(0, 0, 3000))}, Enemies, Reason)))
		{
			Test.AddError(Reason.ToString());
			return;
		}
		ON_SCOPE_EXIT
		{
			Test.TestTrue(TEXT("回收飞行对象"), Support->EndBattleSupport());
			Test.TestTrue(TEXT("回收固定 Mass 目标"), Mass->DespawnUnits(Enemies));
			Director.SetPhase(ECastleDirectorPhase::Editing);
		};
		Director.SetPhase(ECastleDirectorPhase::Battle);
		Test.TestEqual(TEXT("首次受击前炮塔共用冷却倍率为 1"), Support->GetTowerFireRateMultiplier(*Cannon), 1.0f);
		if (!Test.TestTrue(TEXT("真实蜂巢和空投塔请求发射"), Support->QueueLaunch(*Hive) && Support->QueueLaunch(*Balloon))) { return; }
		Support->Tick(0.0f);
		TActorIterator<ACastleBeeSwarmVisual> Bees(World);
		TActorIterator<ACastleSupplyBalloon> Balloons(World);
		if (!Test.TestTrue(TEXT("实际生成两类飞行对象"), Bees && Balloons)) { return; }
		const FVector BeeStart = Bees->GetActorLocation();
		const FVector BalloonStart = Balloons->GetActorLocation();
		constexpr float StepSeconds = 0.01f;
		Support->Tick(StepSeconds);
		const FVector BeeMid = Bees->GetActorLocation();
		const FVector BalloonMid = Balloons->GetActorLocation();
		Director.Tower->ApplyDamage(1.0f);
		Test.TestEqual(TEXT("首次受击后炮塔共用冷却倍率为 2"), Support->GetTowerFireRateMultiplier(*Cannon), 2.0f);
		Support->Tick(StepSeconds);
		const double BeeBase = FVector::Dist(BeeStart, BeeMid);
		const double BalloonBase = FVector::Dist(BalloonStart, BalloonMid);
		Test.TestTrue(TEXT("同一蜂群实际飞行位移翻倍"), BeeBase > 0.0
			&& FMath::IsNearlyEqual(FVector::Dist(BeeMid, Bees->GetActorLocation()), 2.0 * BeeBase, 0.01));
		Test.TestTrue(TEXT("同一气球实际飞行位移翻倍"), BalloonBase > 0.0
			&& FMath::IsNearlyEqual(FVector::Dist(BalloonMid, Balloons->GetActorLocation()), 2.0 * BalloonBase, 0.01));
	}

	FAutomationTestBase& Test;
	double Started = FPlatformTime::Seconds();
	int32 Stage = 0;
	int32 BeforeIsland = 0;
	FVector ReturnLocation = FVector::ZeroVector;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDirectorWave2IntegrationTest, "MovingCastle.Director.Wave2Integration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDirectorWave2IntegrationTest::RunTest(const FString&)
{
	if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FEngineWaitLatentCommand(0.2f));
	ADD_LATENT_AUTOMATION_COMMAND(FCastleDirectorWave2Scenario(*this));
	// 下一项地图测试开跑前必须等待本次 PIE 世界完全退出。
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
	return true;
}
#endif
