#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CastleShopSkyIsland.h"
#include "CastleShopSkyIslandDataAsset.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Castle/Components/CastleEconomyComponent.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorSettings.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Items/CastleBadgeDataAsset.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/Items/CastleRoomGroupDataAsset.h"
#include "Castle/UI/Items/CastleBadgeReplaceModalWidget.h"
#include "Castle/UI/Items/CastleWildcardRoomPickerWidget.h"
#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastlePlayerController.h"
#include "Components/PanelWidget.h"
#include "Components/TextRenderComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	constexpr TCHAR ShopAssetPath[] = TEXT("/Game/MovingCastle/Data/SkyIslands/DA_ShopSkyIsland.DA_ShopSkyIsland");
	constexpr TCHAR RoomPackFolder[] = TEXT("/Game/MovingCastle/Model/Items/RoomPack");
	const FName SoldAmountParameter(TEXT("SoldAmount"));

	/** 用正式资产名验证引用关系，避免漏配后仍由别的有效资源凑齐数量。 */
	template <typename AssetType>
	AssetType* LoadRoomPackAsset(const FString& Name)
	{
		return LoadObject<AssetType>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), RoomPackFolder, *Name, *Name));
	}

	template <typename WidgetType>
	WidgetType* FindShopWidget(UWorld& World)
	{
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&World, Widgets, WidgetType::StaticClass(), true);
		return Widgets.Num() == 1 ? Cast<WidgetType>(Widgets[0]) : nullptr;
	}
}

/** MainMap 中让真实导演生成、传送、射线交互和离岛，使用现有库存及两种正式 WBP。 */
class FCastleShopSkyIslandScenario final : public IAutomationLatentCommand
{
public:
	FCastleShopSkyIslandScenario(FAutomationTestBase& InTest, TStrongObjectPtr<UCastleDirectorConfigDataAsset>&& InConfig)
		: Test(InTest), Config(MoveTemp(InConfig)) {}

	virtual ~FCastleShopSkyIslandScenario() override { StopConfiguringTestIslands(); }

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
			if (FPlatformTime::Seconds() < WaitUntil) { return false; }
			Test.AddError(TEXT("商店空岛测试需要 MainMap 的导演完成开局。"));
			return true;
		}
		if (Stage == 0)
		{
			if (!PrepareAndEnter(*World, *Director)) { return true; }
			WaitUntil = FPlatformTime::Seconds() + 5.0;
			++Stage;
			return false;
		}
		if (Stage == 1)
		{
			if (Director->GetPhase() == ECastleDirectorPhase::Editing && FPlatformTime::Seconds() < WaitUntil) { return false; }
			StopConfiguringTestIslands();
			if (!Test.TestEqual(TEXT("真实导演进入商店岛"), Director->GetPhase(), ECastleDirectorPhase::SkyIsland)
				|| !VerifyPurchases(*World, *Director))
			{
				return true;
			}
			WaitUntil = FPlatformTime::Seconds() + 5.0;
			++Stage;
			return false;
		}
		if (Director->GetPhase() == ECastleDirectorPhase::SkyIsland && FPlatformTime::Seconds() < WaitUntil) { return false; }
		Test.TestEqual(TEXT("商店离岛恢复编辑"), Director->GetPhase(), ECastleDirectorPhase::Editing);
		Test.TestEqual(TEXT("商店离岛推进一步"), Director->GetClearedSteps(), 1);
		Test.TestFalse(TEXT("空岛与商品随访问销毁"), static_cast<bool>(TActorIterator<ACastleShopSkyIsland>(World)));
		return true;
	}

private:
	void VerifyRoomPackDisplay(const ACastleShopSkyIsland& Island, const UCastleShopSkyIslandOffer& Offer)
	{
		Test.TestTrue(TEXT("房间组使用配置的卡包网格"), Offer.GetStaticMesh() == Island.Config->RoomPackMesh.Get());
		if (Test.TestNotNull(TEXT("卡包货位具有独立动态材质"), Offer.OfferMaterialInstance.Get()))
		{
			const auto* Source = Island.Config->RoomPackMaterials.FindChecked(Offer.RoomGroup->Rarity).Get();
			Test.TestTrue(TEXT("卡包动态材质使用商品稀有度对应的父材质"), Offer.OfferMaterialInstance->Parent.Get() == Source);
			Test.TestTrue(TEXT("动态材质应用到包体材质槽"), Offer.GetMaterial(0) == Offer.OfferMaterialInstance.Get());
		}
		const FVector RelativeLocation = Offer.GetRelativeLocation();
		const FBox Box = Offer.Bounds.GetBox();
		const double FloorZ = Island.GetActorLocation().Z;
		Test.TestTrue(TEXT("卡包包围盒底面贴地"), FMath::Abs(Box.Min.Z - FloorZ) <= 1.0);
		Test.TestTrue(TEXT("卡包顶面在 180 至 200 cm"), Box.Max.Z - FloorZ >= 180.0 && Box.Max.Z - FloorZ <= 200.0);
		Test.TestTrue(TEXT("卡包几何高度在 180 至 200 cm"), Box.GetSize().Z >= 180.0 && Box.GetSize().Z <= 200.0);
		if (Test.TestNotNull(TEXT("卡包保留价格牌"), Offer.Sign.Get()))
		{
			Test.TestTrue(TEXT("卡包顶面低于独立价格牌"), Box.Max.Z < Offer.Sign->GetComponentLocation().Z);
		}
		// 用素材导入后实测的 +Y 正面轴，检查两侧货位均朝向中轴线。
		const FVector Front = Offer.GetRelativeRotation().RotateVector(FVector::RightVector);
		const FVector TowardAisle = RelativeLocation.Y < 0.0 ? FVector::RightVector : -FVector::RightVector;
		Test.TestTrue(TEXT("卡包印字面正对中央通道"), Front.Equals(TowardAisle, 0.001));
		Test.TestTrue(TEXT("卡包等比缩放 14.5 倍"), Offer.GetRelativeScale3D().Equals(FVector(14.5), 0.001));
		Test.TestEqual(TEXT("卡包仅开启查询碰撞"), Offer.GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
		Test.TestFalse(TEXT("卡包组件禁用导航影响"), Offer.CanEverAffectNavigation());
		if (RelativeLocation.Y < 0.0) { bCheckedLeftPack = true; }
		else { bCheckedRightPack = true; }
		Test.AddInfo(FString::Printf(TEXT("卡包货位 Y=%.0f，Yaw=%.0f°，实测世界包围盒尺寸 %s cm，底距地板 %.3f cm。"),
			RelativeLocation.Y, Offer.GetRelativeRotation().Yaw, *Box.GetSize().ToString(), Box.Min.Z - FloorZ));
	}

	void VerifySoldAmount(const UCastleShopSkyIslandOffer& Offer, float Expected)
	{
		float SoldAmount = -1.0f;
		if (Test.TestTrue(TEXT("卡包动态材质可读取 SoldAmount"), Offer.OfferMaterialInstance
			&& Offer.OfferMaterialInstance->GetScalarParameterValue(SoldAmountParameter, SoldAmount)))
		{
			Test.TestEqual(TEXT("卡包售罄材质状态匹配交易结果"), SoldAmount, Expected);
		}
		Test.TestTrue(TEXT("卡包模型仍可见"), Offer.IsVisible());
	}

	bool PrepareAndEnter(UWorld& World, UCastleDirectorSubsystem& Director)
	{
		auto* Controller = Cast<ACastlePlayerController>(World.GetFirstPlayerController());
		auto* Authored = LoadObject<UCastleShopSkyIslandDataAsset>(nullptr, ShopAssetPath);
		if (!Test.TestTrue(TEXT("导演使用独立内存配置"), Director.GetConfig() == Config.Get())
			|| !Test.TestNotNull(TEXT("本局控制器"), Controller)
			|| !Test.TestNotNull(TEXT("商店专属 DA"), Authored))
		{
			return false;
		}

		// 用实际配置生成同种子的两套货架，再单独整理确定的四个商品，逐条覆盖不同交易入口。
		if (!VerifyDeterminism(World, Director, *Controller)) { return false; }
		TradeConfig.Reset(DuplicateObject(Authored, GetTransientPackage()));
		TradeConfig->RoomGroupPool.Reset();
		for (UCastleRoomGroupDataAsset* Group : Authored->RoomGroupPool)
		{
			if (Group && (Group->GetFName() == TEXT("DA_RoomGroup_07_TripleThreeArchers")
				|| Group->GetFName() == TEXT("DA_RoomGroup_19_FullyCustomRoom")))
			{
				TradeConfig->RoomGroupPool.Add(Group);
			}
		}
		TradeConfig->BadgePool.Reset();
		for (UCastleBadgeDataAsset* Badge : Authored->BadgePool)
		{
			if (Badge && (Badge->Badge.Type == ECastleBadgeType::FirepowerCard || Badge->Badge.Type == ECastleBadgeType::BlueAbacus))
			{
				TradeConfig->BadgePool.Add(Badge);
			}
		}
		if (!Test.TestEqual(TEXT("交易夹具含两种房间组"), TradeConfig->RoomGroupPool.Num(), 2)
			|| !Test.TestEqual(TEXT("交易夹具含两种徽章"), TradeConfig->BadgePool.Num(), 2))
		{
			return false;
		}
		// SpawnActor 返回后导演才初始化空岛。给本世界新生成的测试岛指定配置，避免更改类默认对象。
		ConfiguredWorld = &World;
		SpawnHandle = World.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* Actor)
		{
			if (auto* Island = Cast<ACastleShopSkyIsland>(Actor)) { Island->ConfigAsset = TradeConfig.Get(); }
		}));

		// 第一步的正式节点全是战斗。只在测试中将一个已登记节点明确改成奖励节点。
		const TActorIterator<ACastleDirectorNodeActor> Node(&World);
		if (!Node || !Node->Configure(*Config, Director.GetNextStep(), ECastleDirectorNodeKind::RewardIsland))
		{
			Test.AddError(TEXT("无法指定测试期间的真实奖励节点。"));
			return false;
		}
		return Test.TestTrue(TEXT("控制台找到商店岛验收命令"),
			IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleTestShopSkyIsland"), *GLog, &World));
	}

	bool VerifyDeterminism(UWorld& World, UCastleDirectorSubsystem& Director, ACastlePlayerController& Controller)
	{
		const FCastleSkyIslandContext Context{&Director, &Controller, ECastleSkyIslandType::Shop, 1, 25496};
		TArray<FText> Previous;
		for (int32 Copy = 0; Copy < 2; ++Copy)
		{
			auto* Island = World.SpawnActor<ACastleShopSkyIsland>(FVector(50000, 5000 * Copy, 20000), FRotator::ZeroRotator);
			if (!Test.TestNotNull(TEXT("确定性生成测试空岛"), Island)) { return false; }
			FText Reason;
			if (!Test.TestTrue(TEXT("正式商品 DA 能初始化"), Island->InitializeIsland(Context, Reason)))
			{
				Test.AddError(Reason.ToString());
				Island->Destroy();
				return false;
			}
			Test.TestEqual(TEXT("四个实体商品"), Island->Offers.Num(), 4);
			TSet<FString> Names;
			bool bHasRoom = false;
			bool bHasBadge = false;
			for (int32 Index = 0; Index < Island->Offers.Num(); ++Index)
			{
				const auto* Offer = Island->Offers[Index].Get();
				bHasRoom |= Offer->RoomGroup != nullptr;
				bHasBadge |= Offer->Badge != nullptr;
				if (Offer->RoomGroup)
				{
					VerifyRoomPackDisplay(*Island, *Offer);
					VerifySoldAmount(*Offer, 0.0f);
				}
				Names.Add(Offer->ProductName.ToString());
				const FText Prompt = Offer->GetInteractPrompt();
				if (Copy == 0) { Previous.Add(Prompt); }
				else if (Previous.IsValidIndex(Index))
				{
					Test.TestEqual(TEXT("相同种子生成相同货位与价格"), Prompt.ToString(), Previous[Index].ToString());
				}
			}
			Test.TestTrue(TEXT("正式池兼有房间组和徽章"), bHasRoom && bHasBadge);
			Test.TestEqual(TEXT("正式池货位优先不重复"), Names.Num(), 4);
			Island->Destroy();
		}
		return true;
	}

	bool VerifyPurchases(UWorld& World, UCastleDirectorSubsystem& Director)
	{
		const TActorIterator<ACastleShopSkyIsland> Island(&World);
		const TActorIterator<ACastleBoard> Board(&World);
		auto* Controller = Cast<ACastlePlayerController>(World.GetFirstPlayerController());
		if (!Island || !Board || !Controller || !Controller->GetEconomy() || !Board->PrototypeBackpack)
		{
			Test.AddError(TEXT("入岛后缺少货架或本局背包账户。"));
			return false;
		}
		if (!Test.TestTrue(TEXT("真实空岛使用四项交易夹具"), Island->Config == TradeConfig.Get())) { return false; }
		UCastleShopSkyIslandOffer* Fixed = nullptr;
		UCastleShopSkyIslandOffer* Wildcard = nullptr;
		TArray<UCastleShopSkyIslandOffer*> BadgeOffers;
		for (UCastleShopSkyIslandOffer* Offer : Island->Offers)
		{
			if (Offer->Badge) { BadgeOffers.Add(Offer); }
			else if (Offer->RoomGroup->bChooseInPicker) { Wildcard = Offer; }
			else { Fixed = Offer; }
		}
		if (!Test.TestNotNull(TEXT("确定房间组台座"), Fixed) || !Test.TestNotNull(TEXT("万能卡台座"), Wildcard)
			|| !Test.TestEqual(TEXT("两个不同的徽章商品"), BadgeOffers.Num(), 2))
		{
			return false;
		}
		auto& Wallet = *Controller->GetEconomy();
		auto& Backpack = *Board->PrototypeBackpack;
		FText Reason;
		const auto Interact = [&Director, &Reason](const UPrimitiveComponent& Target)
		{
			// 包底轴心在地板上，从中央通道一侧瞄准世界包围盒中心才与玩家准星一致。
			const FVector Center = Target.Bounds.Origin;
			const double TowardAisle = Target.GetRelativeLocation().Y < 0.0 ? 160.0 : -160.0;
			return Director.TryInteractWithSkyIsland(Center + FVector(0, TowardAisle, 0), Center, Reason);
		};
		for (const UCastleShopSkyIslandOffer* Pack : {Fixed, Wildcard})
		{
			VerifyRoomPackDisplay(**Island, *Pack);
			VerifySoldAmount(*Pack, 0.0f);
			Test.TestTrue(TEXT("在售准星显示 30 金币"), Pack->GetInteractPrompt().ToString().EndsWith(TEXT("　30 金币")));
		}
		Test.TestTrue(TEXT("真实 PIE 覆盖左右两侧的房间包朝向"), bCheckedLeftPack && bCheckedRightPack);
		const auto CardCount = [&Backpack]() { return Backpack.GetItemCount() + Backpack.GetTempItemCount(); };
		const int32 InitialCards = CardCount();
		Test.TestTrue(TEXT("耗尽余额以验证交易拒绝"), Wallet.TrySpendGold(Wallet.GetGold(), Reason));
		Test.TestFalse(TEXT("余额不足拒绝购买"), Interact(*Fixed));
		Test.TestEqual(TEXT("拒绝后余额未变"), Wallet.GetGold(), 0);
		Test.TestEqual(TEXT("拒绝后没有发牌"), CardCount(), InitialCards);
		Test.TestFalse(TEXT("被拒绝商品仍在售"), Fixed->bSold);
		VerifySoldAmount(*Fixed, 0.0f);
		Test.TestTrue(TEXT("测试账户入账"), Wallet.TryAddGold(200, Reason));
		Test.TestTrue(TEXT("购买确定房间组"), Interact(*Fixed));
		Test.TestEqual(TEXT("房间组扣费"), Wallet.GetGold(), 200 - Fixed->Price);
		Test.TestEqual(TEXT("三张确定箭塔入建筑栏或临时栏"), CardCount(), InitialCards + 3);
		Test.TestTrue(TEXT("房间组台座售罄"), Fixed->bSold);
		VerifySoldAmount(*Fixed, 1.0f);
		Test.TestTrue(TEXT("售罄后价格牌更新"), Fixed->Sign && Fixed->Sign->Text.ToString().EndsWith(TEXT("SOLD OUT")));
		Test.TestFalse(TEXT("售罄房间组拒绝重复购买"), Interact(*Fixed));
		Test.TestEqual(TEXT("售罄房间包仍可被射线命中并返回提示"), Reason.ToString(),
			Fixed->ProductName.ToString() + TEXT("（已售罄）"));

		const int32 BeforeWildcard = Wallet.GetGold();
		if (!Test.TestTrue(TEXT("首次购买打开万能卡弹窗"), Interact(*Wildcard))
			|| !PressPickerButton(World, TEXT("CancelButton")))
		{
			return false;
		}
		Test.TestEqual(TEXT("取消后金币不变"), Wallet.GetGold(), BeforeWildcard);
		Test.TestEqual(TEXT("取消后没有写入建筑卡"), CardCount(), InitialCards + 3);
		Test.TestFalse(TEXT("取消后商品仍在售"), Wildcard->bSold);
		VerifySoldAmount(*Wildcard, 0.0f);
		if (!Test.TestTrue(TEXT("取消后可重新打开同一商品"), Interact(*Wildcard))
			|| !Test.TestTrue(TEXT("模拟弹窗期间余额减少"), Wallet.TrySpendGold(BeforeWildcard, Reason))
			|| !PressPickerButton(World, TEXT("ConfirmButton")))
		{
			return false;
		}
		Test.TestEqual(TEXT("确认检查最新余额"), Wallet.GetGold(), 0);
		Test.TestEqual(TEXT("确认时余额不足不发牌"), CardCount(), InitialCards + 3);
		Test.TestFalse(TEXT("确认被拒绝后仍有货"), Wildcard->bSold);
		VerifySoldAmount(*Wildcard, 0.0f);

		FCastleBuildingDescriptor Example;
		if (!Test.TestTrue(TEXT("读取可用于填满背包的确定卡"), Backpack.TryGetItemAt(0, Example))) { return false; }
		TArray<FCastleBuildingDescriptor> Fill;
		Fill.Init(Example, Backpack.Capacity - Backpack.GetItemCount());
		if (!Fill.IsEmpty() && !Test.TestTrue(TEXT("将正常建筑栏填满"), Backpack.TryAddItemsAllowOverflow(Fill, Reason))) { return false; }
		const int32 BeforeTemp = Backpack.GetTempItemCount();
		Test.TestTrue(TEXT("补足确认时的余额"), Wallet.TryAddGold(BeforeWildcard, Reason));
		// 模拟选择期间背包配置失效。已经授权的扣款应退回，原弹窗仍可在修复后确认。
		Test.AddExpectedError(TEXT("背包容量有效、卡牌批次非空"), EAutomationExpectedErrorFlags::Contains, 1);
		Test.AddExpectedError(TEXT("发放失败，已退还"), EAutomationExpectedErrorFlags::Contains, 1);
		{
			TGuardValue<int32> InvalidCapacity(Backpack.Capacity, 0);
			if (!PressPickerButton(World, TEXT("ConfirmButton"))) { return false; }
		}
		Test.TestEqual(TEXT("发牌失败后退还扣款"), Wallet.GetGold(), BeforeWildcard);
		Test.TestEqual(TEXT("发牌失败不追加临时栏"), Backpack.GetTempItemCount(), BeforeTemp);
		Test.TestFalse(TEXT("发牌失败商品仍在售"), Wildcard->bSold);
		VerifySoldAmount(*Wildcard, 0.0f);
		if (!PressPickerButton(World, TEXT("ConfirmButton"))) { return false; }
		Test.TestEqual(TEXT("确认后只扣一次房间组售价"), Wallet.GetGold(), BeforeWildcard - Wildcard->Price);
		Test.TestEqual(TEXT("满栏时确认的万能卡进入临时栏"), Backpack.GetTempItemCount(), BeforeTemp + 1);
		Test.TestTrue(TEXT("确认后万能卡台座售罄"), Wildcard->bSold);
		VerifySoldAmount(*Wildcard, 1.0f);

		auto* Badges = UCastleBadgeSubsystem::GetForWorld(&World);
		if (!Test.TestNotNull(TEXT("本局徽章子系统"), Badges)) { return false; }
		Badges->ClearAllBadgesForTesting();
		Test.TestTrue(TEXT("未满六枚时可购买徽章"), Interact(*BadgeOffers[0]));
		Test.TestEqual(TEXT("未满徽章栏直接入槽"), Badges->GetBadges().Num(), 1);
		Test.TestTrue(TEXT("已入槽徽章商品售罄"), BadgeOffers[0]->bSold);
		const auto FirstBadge = BadgeOffers[0]->Badge->Badge;
		bool bNeedsReplacement = false;
		for (int32 Index = 1; Index < UCastleBadgeSubsystem::MaxBadges; ++Index)
		{
			if (!Test.TestTrue(TEXT("为满栏场景填充已有徽章"), Badges->TryAcquireBadge(FirstBadge, bNeedsReplacement, Reason))) { return false; }
		}
		const int32 BeforeBadge = Wallet.GetGold();
		Test.TestTrue(TEXT("满六枚时购买进入正式替换流程"), Interact(*BadgeOffers[1]));
		Test.TestEqual(TEXT("替换弹窗打开时徽章已扣款"), Wallet.GetGold(), BeforeBadge - BadgeOffers[1]->Price);
		Test.TestTrue(TEXT("待替换徽章对应商品已售罄"), BadgeOffers[1]->bSold);
		auto* Modal = FindShopWidget<UCastleBadgeReplaceModalWidget>(World);
		if (!Test.TestNotNull(TEXT("正式徽章替换 WBP"), Modal)) { return false; }
		Test.TestTrue(TEXT("满额替换 WBP 可见"), Modal->IsVisible());
		auto* Slots = Cast<UPanelWidget>(Modal->GetWidgetFromName(TEXT("ReplaceSlots")));
		auto* Replace = Slots ? Cast<UButton>(Slots->GetChildAt(0)) : nullptr;
		if (!Test.TestNotNull(TEXT("第一槽替换按钮"), Replace)) { return false; }
		Replace->OnClicked.Broadcast();
		const auto Owned = Badges->GetBadges();
		Test.TestEqual(TEXT("替换完成仍为六枚"), Owned.Num(), UCastleBadgeSubsystem::MaxBadges);
		Test.TestTrue(TEXT("新徽章写入所选槽"), !Owned.IsEmpty() && Owned[0].Type == BadgeOffers[1]->Badge->Badge.Type);
		Test.TestEqual(TEXT("替换提交不重复扣款"), Wallet.GetGold(), BeforeBadge - BadgeOffers[1]->Price);

		auto* Departure = FindObjectFast<UCastleSkyIslandDepartureComponent>(*Island, TEXT("DeparturePortal"));
		return Test.TestNotNull(TEXT("商店离开门"), Departure) && Test.TestTrue(TEXT("登记离开商店岛"), Interact(*Departure));
	}

	bool PressPickerButton(UWorld& World, const FName ButtonName)
	{
		auto* Picker = FindShopWidget<UCastleWildcardRoomPickerWidget>(World);
		if (!Test.TestNotNull(TEXT("正式万能卡 WBP 位于主视口"), Picker)) { return false; }
		if (ButtonName == TEXT("ConfirmButton"))
		{
			// 通过现有控件的按钮委托选择属性，确保校验和结算顺序与玩家点击一致。
			for (const FName Panel : {FName(TEXT("DefinitionChoices")), FName(TEXT("ColorChoices")), FName(TEXT("NumberChoices"))})
			{
				const auto* Choices = Cast<UPanelWidget>(Picker->GetWidgetFromName(Panel));
				auto* Choice = Choices ? Cast<UButton>(Choices->GetChildAt(0)) : nullptr;
				if (!Test.TestNotNull(Panel.ToString(), Choice)) { return false; }
				Choice->OnClicked.Broadcast();
			}
		}
		auto* Button = Cast<UButton>(Picker->GetWidgetFromName(ButtonName));
		if (!Test.TestNotNull(ButtonName.ToString(), Button)) { return false; }
		Button->OnClicked.Broadcast();
		return true;
	}

	void StopConfiguringTestIslands()
	{
		if (auto* World = ConfiguredWorld.Get()) { World->RemoveOnActorSpawnedHandler(SpawnHandle); }
		ConfiguredWorld.Reset();
		SpawnHandle.Reset();
	}

	FAutomationTestBase& Test;
	TStrongObjectPtr<UCastleDirectorConfigDataAsset> Config;
	TStrongObjectPtr<UCastleShopSkyIslandDataAsset> TradeConfig;
	TWeakObjectPtr<UWorld> ConfiguredWorld;
	FDelegateHandle SpawnHandle;
	bool bCheckedLeftPack = false;
	bool bCheckedRightPack = false;
	int32 Stage = 0;
	double WaitUntil = FPlatformTime::Seconds() + 30.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShopSkyIslandIntegrationTest, "MovingCastle.SkyIslands.Shop.Transactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShopSkyIslandIntegrationTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<UCastleDirectorSettings>();
	const auto Previous = Settings->DirectorConfig;
	auto* Source = Previous.LoadSynchronous();
	if (!TestNotNull(TEXT("正式导演配置"), Source)) { return false; }
	TStrongObjectPtr<UCastleDirectorConfigDataAsset> Config(DuplicateObject(Source, GetTransientPackage()));
	Config->SkyIslandClasses.Add(ECastleSkyIslandType::Shop, ACastleShopSkyIsland::StaticClass());
	// 找到开局各有效节点序号都抽到商店的测试种子，避免测试依赖迭代器枚举顺序。
	bool bFoundSeed = false;
	for (int32 Seed = 0; Seed < 100000 && !bFoundSeed; ++Seed)
	{
		bFoundSeed = true;
		for (int32 Index = 0; Index < Config->NodeCountMax; ++Index)
		{
			FRandomStream Random(static_cast<int32>(static_cast<uint32>(Seed) ^ 0x9e3779b9u
				^ (static_cast<uint32>(Index) * 0x85ebca6bu)));
			bFoundSeed &= Random.RandRange(0, 2) == static_cast<int32>(ECastleSkyIslandType::Shop);
		}
		if (bFoundSeed) { Config->RunSeed = Seed; }
	}
	if (!TestTrue(TEXT("指定商店节点的测试种子"), bFoundSeed)) { return false; }
	Settings->DirectorConfig = Config.Get();
	// 只等待本用例显式启动的 PIE 导演，避免全量队列中引擎等待到另一个测试的游戏世界。
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/MovingCastle/Maps/MainMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCastleShopSkyIslandScenario(*this, MoveTemp(Config)));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Settings, Previous, bRequested = false]() mutable
	{
		// 场景结束时等待已挂载的界面及世界真正销毁，再恢复全局 PIE 启动配置。
		if (!GEditor->PlayWorld)
		{
			Settings->DirectorConfig = Previous;
			return true;
		}
		if (!bRequested) { GEditor->RequestEndPlayMap(); bRequested = true; }
		return false;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShopSkyIslandAssetTest, "MovingCastle.SkyIslands.Shop.ConfigValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShopSkyIslandAssetTest::RunTest(const FString& Parameters)
{
	auto* Asset = LoadObject<UCastleShopSkyIslandDataAsset>(nullptr, ShopAssetPath);
	if (!TestNotNull(TEXT("商店配置资产"), Asset)) { return false; }
	FText Reason;
	if (!TestTrue(TEXT("正式配置通过校验"), Asset->Validate(Reason))) { return false; }
	TestEqual(TEXT("引用 26 个房间组"), Asset->RoomGroupPool.Num(), 26);
	TestEqual(TEXT("引用 25 个徽章"), Asset->BadgePool.Num(), 25);
	TestEqual(TEXT("默认货位数"), Asset->OfferCount, 4);
	TestEqual(TEXT("房间组售价"), Asset->DefaultRoomGroupPrice, 30);
	TestEqual(TEXT("徽章售价"), Asset->DefaultBadgePrice, 50);
	TestTrue(TEXT("正式商店明确引用 SM_RoomPack"), Asset->RoomPackMesh.Get() == LoadRoomPackAsset<UStaticMesh>(TEXT("SM_RoomPack")));

	const UMaterial* Master = LoadRoomPackAsset<UMaterial>(TEXT("M_RoomPack_Master"));
	TestNotNull(TEXT("共用房间包母材质"), Master);
	const auto VerifyTexture = [this](const UTexture2D* Texture, const FString& Name, const bool bSRgb,
		const TextureCompressionSettings Compression, const bool bFlipGreen)
	{
		if (!TestNotNull(Name, Texture)) { return; }
		TestEqual(Name + TEXT(" 的 sRGB 设置"), static_cast<bool>(Texture->SRGB), bSRgb);
		TestEqual(Name + TEXT(" 的压缩设置"), Texture->CompressionSettings.GetValue(), Compression);
		TestEqual(Name + TEXT(" 的法线绿通道设置"), static_cast<bool>(Texture->bFlipGreenChannel), bFlipGreen);
		TestEqual(Name + TEXT(" 的源图宽"), Texture->Source.GetSizeX(), int64{1024});
		TestEqual(Name + TEXT(" 的源图高"), Texture->Source.GetSizeY(), int64{1024});
	};
	const TPair<ECastleItemRarity, const TCHAR*> ExpectedMaterials[] = {
		{ECastleItemRarity::Common, TEXT("01_White")}, {ECastleItemRarity::Uncommon, TEXT("02_Blue")},
		{ECastleItemRarity::Rare, TEXT("03_Purple")}, {ECastleItemRarity::Epic, TEXT("04_Gold")},
		{ECastleItemRarity::Legendary, TEXT("05_Holo")}
	};
	for (const auto& Entry : ExpectedMaterials)
	{
		const FString Tier(Entry.Value);
		const auto* Instance = LoadRoomPackAsset<UMaterialInstanceConstant>(TEXT("MI_RoomPack_") + Tier);
		if (!TestNotNull(Tier, Instance)) { continue; }
		TestTrue(Tier + TEXT(" 显式绑定对应实例"), Asset->RoomPackMaterials.FindChecked(Entry.Key).Get() == Instance);
		TestTrue(Tier + TEXT(" 使用房间包母材质"), Instance->Parent.Get() == Master);
		const FString ColorName = TEXT("T_RoomPack_") + Tier + TEXT("_BC");
		const FString MaskName = TEXT("T_RoomPack_") + Tier + TEXT("_ORM");
		const auto* BaseColor = LoadRoomPackAsset<UTexture2D>(ColorName);
		const auto* Masks = LoadRoomPackAsset<UTexture2D>(MaskName);
		VerifyTexture(BaseColor, ColorName, true, TC_Default, false);
		VerifyTexture(Masks, MaskName, false, TC_Masks, false);
		UTexture* Parameter = nullptr;
		TestTrue(Tier + TEXT(" 使用该档基础色"), Instance->GetTextureParameterValue(FName(TEXT("PackBaseColor")), Parameter)
			&& Parameter == BaseColor);
		TestTrue(Tier + TEXT(" 使用该档 ORM"), Instance->GetTextureParameterValue(FName(TEXT("PackORM")), Parameter)
			&& Parameter == Masks);
	}
	VerifyTexture(LoadRoomPackAsset<UTexture2D>(TEXT("T_RoomPack_Foil_N")), TEXT("T_RoomPack_Foil_N"), false, TC_Normalmap, true);

	// 非法变体只改临时副本，编码预期错误并断言原配置不会被修正。
	auto* Invalid = DuplicateObject(Asset, GetTransientPackage());
	Invalid->OfferCount = 3;
	TestTrue(TEXT("允许 3 个货位"), Invalid->Validate(Reason));
	AddExpectedError(TEXT("商店空岛配置"), EAutomationExpectedErrorFlags::Contains, 7);
	Invalid->OfferCount = 2;
	TestFalse(TEXT("拒绝不足 3 个货位"), Invalid->Validate(Reason));
	Invalid->OfferCount = 4;
	Invalid->DefaultBadgePrice = -1;
	TestFalse(TEXT("拒绝负售价"), Invalid->Validate(Reason));
	Invalid->DefaultBadgePrice = 50;
	Invalid->RoomGroupPool[0] = nullptr;
	TestFalse(TEXT("拒绝无效商品指针"), Invalid->Validate(Reason));
	Invalid->RoomGroupPool = Asset->RoomGroupPool;
	Invalid->BadgePool.Reset();
	TestFalse(TEXT("拒绝空商品池"), Invalid->Validate(Reason));
	Invalid->BadgePool = Asset->BadgePool;
	Invalid->RoomPackMesh = nullptr;
	TestFalse(TEXT("拒绝缺失房间包网格"), Invalid->Validate(Reason));
	Invalid->RoomPackMesh = Asset->RoomPackMesh;
	Invalid->RoomPackMaterials[ECastleItemRarity::Epic] = nullptr;
	TestFalse(TEXT("拒绝任一档材质缺失"), Invalid->Validate(Reason));
	Invalid->RoomPackMaterials[ECastleItemRarity::Epic] = UMaterial::GetDefaultMaterial(MD_Surface);
	TestFalse(TEXT("拒绝材质没有售罄控制参数"), Invalid->Validate(Reason));
	Invalid->RoomPackMaterials = Asset->RoomPackMaterials;
	auto* InvalidGroup = DuplicateObject(Asset->RoomGroupPool[0].Get(), GetTransientPackage());
	Invalid->RoomGroupPool[0] = InvalidGroup;
	AddExpectedError(TEXT("的卡包稀有度无效"), EAutomationExpectedErrorFlags::Contains, 2);
	InvalidGroup->Rarity = ECastleItemRarity::None;
	TestFalse(TEXT("拒绝房间组漏配稀有度"), Invalid->Validate(Reason));
	InvalidGroup->Rarity = static_cast<ECastleItemRarity>(255);
	TestFalse(TEXT("拒绝房间组稀有度越界"), Invalid->Validate(Reason));
	return true;
}

#endif
