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
#include "Components/Button.h"
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
#include "PhysicsEngine/BodySetup.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	constexpr TCHAR ShopAssetPath[] = TEXT("/Game/MovingCastle/Data/SkyIslands/DA_ShopSkyIsland.DA_ShopSkyIsland");
	constexpr TCHAR RoomPackFolder[] = TEXT("/Game/MovingCastle/Model/Items/RoomPack");
	const FName SoldAmountParameter(TEXT("SoldAmount"));

	/** 由正式对象路径加载，避免漏配后仍由别的有效资源凑齐数量。 */
	template <typename AssetType>
	AssetType* LoadNamedAsset(const TCHAR* Folder, const FString& Name)
	{
		return LoadObject<AssetType>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), Folder, *Name, *Name));
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
	FCastleShopSkyIslandScenario(FAutomationTestBase& InTest, TStrongObjectPtr<UCastleDirectorConfigDataAsset>&& InConfig,
		const bool bInTestRoomGroup)
		: Test(InTest), Config(MoveTemp(InConfig)), bTestRoomGroup(bInTestRoomGroup) {}

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
		if (Test.TestTrue(TEXT("商品动态材质可读取 SoldAmount"), Offer.OfferMaterialInstance
			&& Offer.OfferMaterialInstance->GetScalarParameterValue(SoldAmountParameter, SoldAmount)))
		{
			Test.TestEqual(TEXT("商品售罄材质状态匹配交易结果"), SoldAmount, Expected);
		}
		Test.TestTrue(TEXT("商品模型仍可见"), Offer.IsVisible());
	}

	void VerifyBadgeDisplay(const ACastleShopSkyIsland& Island, const UCastleShopSkyIslandOffer& Offer)
	{
		const UStaticMesh* Mesh = Offer.Badge->DisplayMesh;
		Test.TestTrue(TEXT("徽章货位使用该徽章的正式网格"), Offer.GetStaticMesh() == Mesh);
		if (Test.TestNotNull(TEXT("徽章货位具有独立动态材质"), Offer.OfferMaterialInstance.Get()))
		{
			Test.TestTrue(TEXT("徽章动态材质使用网格自己的原始材质"), Offer.OfferMaterialInstance->Parent.Get() == Mesh->GetMaterial(0));
			Test.TestTrue(TEXT("动态材质应用到徽章网格的材质槽"), Offer.GetMaterial(0) == Offer.OfferMaterialInstance.Get());
		}
		const FBox Box = Offer.Bounds.GetBox();
		const FVector Front = Offer.GetRelativeRotation().RotateVector(FVector::RightVector);
		const FVector TowardAisle = Offer.GetRelativeLocation().Y < 0.0 ? FVector::RightVector : -FVector::RightVector;
		Test.TestTrue(TEXT("徽章正面朝向中央通道"), Front.Equals(TowardAisle, 0.001));
		Test.TestTrue(TEXT("徽章底边贴地"), FMath::Abs(Box.Min.Z - Island.GetActorLocation().Z) <= 1.0);
		Test.TestTrue(TEXT("徽章高度在 180 至 200 cm"), Box.GetSize().Z >= 180.0 && Box.GetSize().Z <= 200.0);
		Test.TestEqual(TEXT("徽章仅开启查询碰撞"), Offer.GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
		Test.TestEqual(TEXT("徽章阻挡准星查询"), Offer.GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
		Test.TestFalse(TEXT("徽章组件禁用导航影响"), Offer.CanEverAffectNavigation());
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
		// 固定为普通档，保证两个房间组和两个徽章均能上架，仍调用正式的同档不重复抽样。
		for (FCastleShopRarityWeights& Row : TradeConfig->RarityWeightsByTowerLevel)
		{
			Row = {100, 0, 0, 0, 0};
		}
		TradeConfig->RoomGroupPool.Reset();
		for (UCastleRoomGroupDataAsset* Group : Authored->RoomGroupPool)
		{
			if (Group && (Group->GetFName() == TEXT("DA_RoomGroup_01_RandomSupply")
				|| Group->GetFName() == TEXT("DA_RoomGroup_04_CustomNormalRoom")))
			{
				TradeConfig->RoomGroupPool.Add(Group);
			}
		}
		TradeConfig->BadgePool.Reset();
		for (UCastleBadgeDataAsset* Badge : Authored->BadgePool)
		{
			if (Badge && (Badge->Badge.Type == ECastleBadgeType::FirepowerCard || Badge->Badge.Type == ECastleBadgeType::StartingFunds))
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
				}
				else { VerifyBadgeDisplay(*Island, *Offer); }
				VerifySoldAmount(*Offer, 0.0f);
				const int32 AssetPrice = Offer->RoomGroup ? Offer->RoomGroup->ShopPrice : Offer->Badge->ShopPrice;
				Test.TestEqual(TEXT("初始化货位按各自资产定价"), Offer->Price, AssetPrice);
				Test.TestTrue(TEXT("价格牌显示资产单价"), Offer->Sign->Text.ToString().EndsWith(FString::Printf(TEXT("%d GOLD"), AssetPrice)));
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
			Test.TestEqual(TEXT("房间组从单品资产读取售价"), Pack->Price, Pack->RoomGroup->ShopPrice);
			Test.TestTrue(TEXT("准星提示显示单品售价"), Pack->GetInteractPrompt().ToString().EndsWith(FString::Printf(TEXT("　%d 金币"), Pack->Price)));
		}
		Test.TestTrue(TEXT("真实 PIE 覆盖左右两侧的房间包朝向"), bCheckedLeftPack && bCheckedRightPack);
		// 保留正式购房和徽章交易两条用例，房间组规则引擎的失败不会挡住独立的徽章验收。
		const bool bPurchased = bTestRoomGroup
			? VerifyRoomGroupPurchases(World, Wallet, Backpack, *Fixed, *Wildcard, Interact, Reason)
			: VerifyBadgePurchases(World, **Island, Wallet, BadgeOffers, Interact, Reason);
		if (!bPurchased) { return false; }

		auto* Departure = FindObjectFast<UCastleSkyIslandDepartureComponent>(*Island, TEXT("DeparturePortal"));
		return Test.TestNotNull(TEXT("商店离开门"), Departure) && Test.TestTrue(TEXT("登记离开商店岛"), Interact(*Departure));
	}

	/** 自定义卡延迟扣费、取消、拒绝与失败退款均走真实按钮和背包。 */
	bool VerifyRoomGroupPurchases(UWorld& World, UCastleEconomyComponent& Wallet, UCastleBackpackComponent& Backpack,
		UCastleShopSkyIslandOffer& FixedOffer, UCastleShopSkyIslandOffer& WildcardOffer,
		TFunctionRef<bool(const UPrimitiveComponent&)> Interact, FText& Reason)
	{
		auto* Fixed = &FixedOffer;
		auto* Wildcard = &WildcardOffer;
		const auto CardCount = [&Backpack]() { return Backpack.GetItemCount() + Backpack.GetTempItemCount(); };
		const int32 InitialCards = CardCount();
		Test.TestTrue(TEXT("耗尽余额以验证交易拒绝"), Wallet.TrySpendGold(Wallet.GetGold(), Reason));
		Test.TestFalse(TEXT("余额不足拒绝购买"), Interact(*Fixed));
		Test.TestEqual(TEXT("拒绝后余额未变"), Wallet.GetGold(), 0);
		Test.TestEqual(TEXT("拒绝后没有发牌"), CardCount(), InitialCards);
		Test.TestFalse(TEXT("被拒绝商品仍在售"), Fixed->bSold);
		VerifySoldAmount(*Fixed, 0.0f);
		Test.TestTrue(TEXT("测试账户入账"), Wallet.TryAddGold(200, Reason));
		if (!Test.TestTrue(TEXT("购买随机补给房间组"), Interact(*Fixed))) { return false; }
		Test.TestEqual(TEXT("房间组扣费"), Wallet.GetGold(), 200 - Fixed->Price);
		Test.TestEqual(TEXT("三张随机房间入建筑栏或临时栏"), CardCount(), InitialCards + 3);
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
		return true;
	}

	/** 通过可见性射线购买两个真实徽章，覆盖各自定价、售罄材质与手动替换。 */
	bool VerifyBadgePurchases(UWorld& World, const ACastleShopSkyIsland& Island, UCastleEconomyComponent& Wallet,
		const TArray<UCastleShopSkyIslandOffer*>& BadgeOffers,
		TFunctionRef<bool(const UPrimitiveComponent&)> Interact, FText& Reason)
	{
		auto* Badges = UCastleBadgeSubsystem::GetForWorld(&World);
		if (!Test.TestNotNull(TEXT("本局徽章子系统"), Badges)) { return false; }
		Badges->ClearAllBadgesForTesting();
		for (const UCastleShopSkyIslandOffer* BadgeOffer : BadgeOffers)
		{
			VerifyBadgeDisplay(Island, *BadgeOffer);
			VerifySoldAmount(*BadgeOffer, 0.0f);
			Test.TestEqual(TEXT("徽章从单品资产读取售价"), BadgeOffer->Price, BadgeOffer->Badge->ShopPrice);
		}
		if (!Test.TestTrue(TEXT("为两枚不同价格的徽章补足余额"), Wallet.TryAddGold(300, Reason))) { return false; }
		const int32 BeforeFirstBadge = Wallet.GetGold();
		if (!Test.TestTrue(TEXT("未满六枚时可购买徽章"), Interact(*BadgeOffers[0]))) { return false; }
		Test.TestEqual(TEXT("首枚徽章按照单品售价扣费"), Wallet.GetGold(), BeforeFirstBadge - BadgeOffers[0]->Price);
		Test.TestEqual(TEXT("未满徽章栏直接入槽"), Badges->GetBadges().Num(), 1);
		Test.TestTrue(TEXT("已入槽徽章商品售罄"), BadgeOffers[0]->bSold);
		VerifySoldAmount(*BadgeOffers[0], 1.0f);
		VerifySoldAmount(*BadgeOffers[1], 0.0f);
		Test.TestFalse(TEXT("已售徽章仍可命中并拒绝重复购买"), Interact(*BadgeOffers[0]));
		Test.TestEqual(TEXT("售罄徽章返回售罄提示"), Reason.ToString(), BadgeOffers[0]->ProductName.ToString() + TEXT("（已售罄）"));
		Test.TestEqual(TEXT("重复购买拒绝后余额不变"), Wallet.GetGold(), BeforeFirstBadge - BadgeOffers[0]->Price);
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
		VerifySoldAmount(*BadgeOffers[1], 1.0f);
		Test.TestTrue(TEXT("售罄徽章价格牌更新"), BadgeOffers[1]->Sign->Text.ToString().EndsWith(TEXT("SOLD OUT")));
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
		return true;
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
	const bool bTestRoomGroup;
	bool bCheckedLeftPack = false;
	bool bCheckedRightPack = false;
	int32 Stage = 0;
	double WaitUntil = FPlatformTime::Seconds() + 30.0;
};

/** 两条交易用例共享开局准备，各自从一局干净的 MainMap 开始。 */
static bool QueueShopSkyIslandScenario(FAutomationTestBase& Test, const bool bTestRoomGroup)
{
	auto* Settings = GetMutableDefault<UCastleDirectorSettings>();
	const auto Previous = Settings->DirectorConfig;
	auto* Source = Previous.LoadSynchronous();
	if (!Test.TestNotNull(TEXT("正式导演配置"), Source)) { return false; }
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
	if (!Test.TestTrue(TEXT("指定商店节点的测试种子"), bFoundSeed)) { return false; }
	Settings->DirectorConfig = Config.Get();
	// 只等待本用例显式启动的 PIE 导演，避免全量队列中引擎等待到另一个测试的游戏世界。
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/MovingCastle/Maps/MainMap")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCastleShopSkyIslandScenario(Test, MoveTemp(Config), bTestRoomGroup));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShopSkyIslandIntegrationTest, "MovingCastle.SkyIslands.Shop.Transactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShopSkyIslandIntegrationTest::RunTest(const FString& Parameters)
{
	return QueueShopSkyIslandScenario(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShopSkyIslandBadgeTransactionTest, "MovingCastle.SkyIslands.Shop.BadgeTransactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShopSkyIslandBadgeTransactionTest::RunTest(const FString& Parameters)
{
	return QueueShopSkyIslandScenario(*this, false);
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
	TestEqual(TEXT("固定四个货位"), Asset->OfferCount, 4);
	TestTrue(TEXT("正式商店明确引用 SM_RoomPack"), Asset->RoomPackMesh.Get() == LoadNamedAsset<UStaticMesh>(RoomPackFolder, TEXT("SM_RoomPack")));

	const UMaterial* Master = LoadNamedAsset<UMaterial>(RoomPackFolder, TEXT("M_RoomPack_Master"));
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
		const auto* Instance = LoadNamedAsset<UMaterialInstanceConstant>(RoomPackFolder, TEXT("MI_RoomPack_") + Tier);
		if (!TestNotNull(Tier, Instance)) { continue; }
		TestTrue(Tier + TEXT(" 显式绑定对应实例"), Asset->RoomPackMaterials.FindChecked(Entry.Key).Get() == Instance);
		TestTrue(Tier + TEXT(" 使用房间包母材质"), Instance->Parent.Get() == Master);
		const FString ColorName = TEXT("T_RoomPack_") + Tier + TEXT("_BC");
		const FString MaskName = TEXT("T_RoomPack_") + Tier + TEXT("_ORM");
		const auto* BaseColor = LoadNamedAsset<UTexture2D>(RoomPackFolder, ColorName);
		const auto* Masks = LoadNamedAsset<UTexture2D>(RoomPackFolder, MaskName);
		VerifyTexture(BaseColor, ColorName, true, TC_Default, false);
		VerifyTexture(Masks, MaskName, false, TC_Masks, false);
		UTexture* Parameter = nullptr;
		TestTrue(Tier + TEXT(" 使用该档基础色"), Instance->GetTextureParameterValue(FName(TEXT("PackBaseColor")), Parameter)
			&& Parameter == BaseColor);
		TestTrue(Tier + TEXT(" 使用该档 ORM"), Instance->GetTextureParameterValue(FName(TEXT("PackORM")), Parameter)
			&& Parameter == Masks);
	}
	VerifyTexture(LoadNamedAsset<UTexture2D>(RoomPackFolder, TEXT("T_RoomPack_Foil_N")), TEXT("T_RoomPack_Foil_N"), false, TC_Normalmap, true);

	// 非法变体只改临时副本，编码预期错误并断言原配置不会被修正。
	auto* Invalid = DuplicateObject(Asset, GetTransientPackage());
	AddExpectedError(TEXT("商店空岛配置"), EAutomationExpectedErrorFlags::Contains, 14);
	Invalid->OfferCount = 3;
	TestFalse(TEXT("拒绝少于四个货位"), Invalid->Validate(Reason));
	Invalid->OfferCount = 4;
	Invalid->RarityWeightsByTowerLevel.Pop();
	TestFalse(TEXT("拒绝概率表缺少等级行"), Invalid->Validate(Reason));
	Invalid->RarityWeightsByTowerLevel = Asset->RarityWeightsByTowerLevel;
	Invalid->RarityWeightsByTowerLevel[0].Rare = -1;
	TestFalse(TEXT("拒绝负概率"), Invalid->Validate(Reason));
	Invalid->RarityWeightsByTowerLevel[0].Rare = 0;
	Invalid->RarityWeightsByTowerLevel[0].Common++;
	TestFalse(TEXT("拒绝行总和不是一百"), Invalid->Validate(Reason));
	Invalid->RarityWeightsByTowerLevel = Asset->RarityWeightsByTowerLevel;
	Invalid->RoomGroupPool[0] = nullptr;
	TestFalse(TEXT("拒绝无效商品指针"), Invalid->Validate(Reason));
	Invalid->RoomGroupPool = Asset->RoomGroupPool;
	Invalid->BadgePool.Reset();
	TestFalse(TEXT("拒绝空商品池"), Invalid->Validate(Reason));
	Invalid->BadgePool = Asset->BadgePool;
	Invalid->BadgePool.RemoveAll([](const UCastleBadgeDataAsset* Badge) { return Badge->Rarity == ECastleItemRarity::Legendary; });
	TestFalse(TEXT("拒绝已开启档位缺少徽章"), Invalid->Validate(Reason));
	Invalid->BadgePool = Asset->BadgePool;
	Invalid->RoomPackMesh = nullptr;
	TestFalse(TEXT("拒绝缺失房间包网格"), Invalid->Validate(Reason));
	Invalid->RoomPackMesh = Asset->RoomPackMesh;
	Invalid->RoomPackMaterials[ECastleItemRarity::Epic] = nullptr;
	TestFalse(TEXT("拒绝任一档材质缺失"), Invalid->Validate(Reason));
	Invalid->RoomPackMaterials[ECastleItemRarity::Epic] = UMaterial::GetDefaultMaterial(MD_Surface);
	TestFalse(TEXT("拒绝材质没有售罄控制参数"), Invalid->Validate(Reason));
	Invalid->RoomPackMaterials = Asset->RoomPackMaterials;
	auto* InvalidBadge = DuplicateObject(Asset->BadgePool[0].Get(), GetTransientPackage());
	Invalid->BadgePool[0] = InvalidBadge;
	InvalidBadge->ShopPrice = -1;
	TestFalse(TEXT("拒绝徽章负售价"), Invalid->Validate(Reason));
	InvalidBadge->ShopPrice = Asset->BadgePool[0]->ShopPrice;
	InvalidBadge->Badge.Icon = nullptr;
	TestFalse(TEXT("拒绝缺失徽章图标"), Invalid->Validate(Reason));
	InvalidBadge->Badge.Icon = Asset->BadgePool[0]->Badge.Icon;
	InvalidBadge->DisplayMesh = nullptr;
	TestFalse(TEXT("拒绝缺失徽章网格"), Invalid->Validate(Reason));
	Invalid->BadgePool = Asset->BadgePool;
	auto* InvalidGroup = DuplicateObject(Asset->RoomGroupPool[0].Get(), GetTransientPackage());
	Invalid->RoomGroupPool[0] = InvalidGroup;
	InvalidGroup->ShopPrice = -1;
	TestFalse(TEXT("拒绝房间组负售价"), Invalid->Validate(Reason));
	InvalidGroup->ShopPrice = Asset->RoomGroupPool[0]->ShopPrice;
	AddExpectedError(TEXT("的卡包稀有度无效"), EAutomationExpectedErrorFlags::Contains, 2);
	InvalidGroup->Rarity = ECastleItemRarity::None;
	TestFalse(TEXT("拒绝房间组漏配稀有度"), Invalid->Validate(Reason));
	InvalidGroup->Rarity = static_cast<ECastleItemRarity>(255);
	TestFalse(TEXT("拒绝房间组稀有度越界"), Invalid->Validate(Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShopSkyIslandCatalogTest, "MovingCastle.SkyIslands.Shop.OfficialCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShopSkyIslandCatalogTest::RunTest(const FString& Parameters)
{
	const auto* Config = LoadObject<UCastleShopSkyIslandDataAsset>(nullptr, ShopAssetPath);
	if (!TestNotNull(TEXT("正式商店配置"), Config)) { return false; }
	FText Reason;
	if (!TestTrue(TEXT("完整目录通过商店校验"), Config->Validate(Reason))) { return false; }

	// 使用指南中的具名资产和各自售价，避免两个错误相同的奖池互相比对后仍通过。
	const struct
	{
		const TCHAR* AssetName;
		const TCHAR* DisplayName;
		ECastleItemRarity Rarity;
		int32 Price;
	} RoomGroups[] = {
		{TEXT("DA_RoomGroup_01_RandomSupply"), TEXT("随机补给"), ECastleItemRarity::Common, 48},
		{TEXT("DA_RoomGroup_02_WangcaiPatchBag"), TEXT("旺财的补缝袋"), ECastleItemRarity::Common, 40},
		{TEXT("DA_RoomGroup_03_ShieldBehindField"), TEXT("盾后小田"), ECastleItemRarity::Common, 55},
		{TEXT("DA_RoomGroup_04_CustomNormalRoom"), TEXT("自定义普通房间"), ECastleItemRarity::Common, 36},
		{TEXT("DA_RoomGroup_05_TwinRooms"), TEXT("双胞房间"), ECastleItemRarity::Common, 44},
		{TEXT("DA_RoomGroup_06_TopSupply"), TEXT("顶格补给"), ECastleItemRarity::Uncommon, 86},
		{TEXT("DA_RoomGroup_07_TripleThreeArchers"), TEXT("三三箭塔"), ECastleItemRarity::Uncommon, 105},
		{TEXT("DA_RoomGroup_08_LuckyCannons"), TEXT("幸运炮塔"), ECastleItemRarity::Uncommon, 96},
		{TEXT("DA_RoomGroup_09_SequentialFarms"), TEXT("连号农田"), ECastleItemRarity::Uncommon, 105},
		{TEXT("DA_RoomGroup_10_Highway"), TEXT("高速公路"), ECastleItemRarity::Uncommon, 115},
		{TEXT("DA_RoomGroup_11_RedStorm"), TEXT("红色风暴"), ECastleItemRarity::Rare, 185},
		{TEXT("DA_RoomGroup_12_DoubleStairPuzzle"), TEXT("双楼梯拼图"), ECastleItemRarity::Rare, 168},
		{TEXT("DA_RoomGroup_13_TricolorAmmoSquad"), TEXT("三色弹药班"), ECastleItemRarity::Rare, 178},
		{TEXT("DA_RoomGroup_14_ThreeWayToolbox"), TEXT("三岔分工箱"), ECastleItemRarity::Rare, 172},
		{TEXT("DA_RoomGroup_15_BigNumberRoom"), TEXT("大数字房间"), ECastleItemRarity::Rare, 148},
		{TEXT("DA_RoomGroup_16_RandomFuncCustomRoom"), TEXT("随机功能自定义房间"), ECastleItemRarity::Rare, 156},
		{TEXT("DA_RoomGroup_17_ThreeBlueEights"), TEXT("加入 3 个蓝 8"), ECastleItemRarity::Epic, 275},
		{TEXT("DA_RoomGroup_18_HiveChainBox"), TEXT("蜂房接龙匣"), ECastleItemRarity::Epic, 340},
		{TEXT("DA_RoomGroup_19_FullyCustomRoom"), TEXT("完全自定义房间"), ECastleItemRarity::Epic, 245},
		{TEXT("DA_RoomGroup_20_Normalize"), TEXT("归一化"), ECastleItemRarity::Epic, 300},
		{TEXT("DA_RoomGroup_21_PortalRoom"), TEXT("传送门房间"), ECastleItemRarity::Epic, 315},
		{TEXT("DA_RoomGroup_22_GreenLushRedWither"), TEXT("绿肥红瘦"), ECastleItemRarity::Legendary, 520},
		{TEXT("DA_RoomGroup_23_SelfOrbitCannon"), TEXT("自环绕炮塔"), ECastleItemRarity::Legendary, 490},
		{TEXT("DA_RoomGroup_24_TowerBuyout"), TEXT("功能塔买断"), ECastleItemRarity::Legendary, 650},
		{TEXT("DA_RoomGroup_25_DirectDeploy"), TEXT("直接部署"), ECastleItemRarity::Legendary, 620},
		{TEXT("DA_RoomGroup_26_ColorShift"), TEXT("颜色转变"), ECastleItemRarity::Legendary, 500},
	};
	for (const auto& Row : RoomGroups)
	{
		auto* Group = LoadNamedAsset<UCastleRoomGroupDataAsset>(TEXT("/Game/MovingCastle/Data/Items/RoomGroups"), Row.AssetName);
		if (!TestNotNull(Row.AssetName, Group)) { return false; }
		TestTrue(FString(Row.AssetName) + TEXT(" 进入正式商店池"), Config->RoomGroupPool.Contains(Group));
		TestEqual(FString(Row.AssetName) + TEXT(" 名称"), Group->DisplayName.ToString(), FString(Row.DisplayName));
		TestEqual(FString(Row.AssetName) + TEXT(" 稀有度"), Group->Rarity, Row.Rarity);
		TestEqual(FString(Row.AssetName) + TEXT(" 售价"), Group->ShopPrice, Row.Price);
		TestTrue(FString(Row.AssetName) + TEXT(" 有显式发牌、选择或主栏动作"), !Group->CardRules.IsEmpty()
			|| Group->bChooseInPicker || Group->MainBarAction != ECastleRoomGroupMainBarAction::None);
	}

	const struct
	{
		ECastleBadgeType Type;
		const TCHAR* AssetSuffix;
		const TCHAR* ArtSuffix;
		ECastleItemRarity Rarity;
		int32 Price;
		float Magnitude;
	} Badges[] = {
		{ECastleBadgeType::FirepowerCard, TEXT("FirepowerCard"), TEXT("01_firepower_card"), ECastleItemRarity::Common, 90, 0.10f},
		{ECastleBadgeType::CoinPurse, TEXT("CoinPurse"), TEXT("02_coin_purse"), ECastleItemRarity::Common, 90, 0.10f},
		{ECastleBadgeType::StartingFunds, TEXT("StartingFunds"), TEXT("03_starting_funds"), ECastleItemRarity::Common, 140, 100.00f},
		{ECastleBadgeType::DoubleDoorPass, TEXT("DoubleDoorPass"), TEXT("04_double_door_pass"), ECastleItemRarity::Common, 130, 10.00f},
		{ECastleBadgeType::SpareBowstring, TEXT("SpareBowstring"), TEXT("05_spare_bowstring"), ECastleItemRarity::Common, 85, 1.00f},
		{ECastleBadgeType::BalloonRack, TEXT("BalloonRack"), TEXT("06_balloon_rack"), ECastleItemRarity::Common, 120, 0.20f},
		{ECastleBadgeType::ShopDiscount, TEXT("ShopDiscount"), TEXT("07_shop_discount"), ECastleItemRarity::Uncommon, 230, 0.85f},
		{ECastleBadgeType::ShopRebate, TEXT("ShopRebate"), TEXT("08_shop_rebate"), ECastleItemRarity::Uncommon, 220, 1.00f},
		{ECastleBadgeType::ShiftWhistle, TEXT("ShiftWhistle"), TEXT("09_shift_whistle"), ECastleItemRarity::Uncommon, 170, 0.20f},
		{ECastleBadgeType::StringGlove, TEXT("StringGlove"), TEXT("10_string_glove"), ECastleItemRarity::Uncommon, 180, 1.00f},
		{ECastleBadgeType::QuickstringPass, TEXT("QuickstringPass"), TEXT("11_quickstring_pass"), ECastleItemRarity::Uncommon, 200, 0.20f},
		{ECastleBadgeType::FreeRefresh, TEXT("FreeRefresh"), TEXT("12_free_refresh"), ECastleItemRarity::Rare, 360, 3.00f},
		{ECastleBadgeType::QuickLadder, TEXT("QuickLadder"), TEXT("13_quick_ladder"), ECastleItemRarity::Rare, 270, 0.50f},
		{ECastleBadgeType::LoopPurchase, TEXT("LoopPurchase"), TEXT("14_loop_purchase"), ECastleItemRarity::Rare, 320, 10.00f},
		{ECastleBadgeType::LateAdvantage, TEXT("LateAdvantage"), TEXT("15_late_advantage"), ECastleItemRarity::Rare, 380, 0.20f},
		{ECastleBadgeType::SiphonArrow, TEXT("SiphonArrow"), TEXT("16_siphon_arrow"), ECastleItemRarity::Rare, 350, 0.10f},
		{ECastleBadgeType::BlueAbacus, TEXT("BlueAbacus"), TEXT("17_blue_abacus"), ECastleItemRarity::Epic, 450, 1.00f},
		{ECastleBadgeType::WorkshopRefund, TEXT("WorkshopRefund"), TEXT("18_workshop_refund"), ECastleItemRarity::Epic, 520, 2.00f},
		{ECastleBadgeType::EmergencyBallast, TEXT("EmergencyBallast"), TEXT("19_emergency_ballast"), ECastleItemRarity::Epic, 500, 0.50f},
		{ECastleBadgeType::RapidMarch, TEXT("RapidMarch"), TEXT("20_rapid_march"), ECastleItemRarity::Epic, 480, 0.20f},
		{ECastleBadgeType::BerserkBee, TEXT("BerserkBee"), TEXT("21_berserk_bee"), ECastleItemRarity::Epic, 550, 5.00f},
		{ECastleBadgeType::ChainReaction, TEXT("ChainReaction"), TEXT("22_chain_reaction"), ECastleItemRarity::Legendary, 1000, 1.00f},
		{ECastleBadgeType::BurstAlarm, TEXT("BurstAlarm"), TEXT("23_burst_alarm"), ECastleItemRarity::Legendary, 1000, 2.00f},
		{ECastleBadgeType::LapReassignment, TEXT("LapReassignment"), TEXT("24_lap_reassignment"), ECastleItemRarity::Legendary, 900, 10.00f},
		{ECastleBadgeType::LastBalloonClasp, TEXT("LastBalloonClasp"), TEXT("25_last_balloon_clasp"), ECastleItemRarity::Legendary, 950, 1.00f},
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Badges); ++Index)
	{
		const auto& Row = Badges[Index];
		const FString Name = FString::Printf(TEXT("DA_Badge_%02d_%s"), Index + 1, Row.AssetSuffix);
		auto* Asset = LoadNamedAsset<UCastleBadgeDataAsset>(TEXT("/Game/MovingCastle/Data/Items/Badges"), Name);
		auto* Mesh = LoadNamedAsset<UStaticMesh>(TEXT("/Game/MovingCastle/Art/Badges/Meshes"), FString(TEXT("SM_Badge_")) + Row.ArtSuffix);
		auto* Icon = LoadNamedAsset<UTexture2D>(TEXT("/Game/MovingCastle/UI/Badges/Icons"), FString(TEXT("Badge_")) + Row.ArtSuffix);
		if (!TestNotNull(Name, Asset) || !TestNotNull(Name + TEXT(" 的 GLB 网格"), Mesh)
			|| !TestNotNull(Name + TEXT(" 的 PNG 贴图"), Icon))
		{
			return false;
		}
		TestTrue(Name + TEXT(" 进入正式商店池"), Config->BadgePool.Contains(Asset));
		TestTrue(Name + TEXT(" 徽章定义有效"), Asset->Badge.Validate(Reason));
		TestEqual(Name + TEXT(" 类型"), Asset->Badge.Type, Row.Type);
		TestEqual(Name + TEXT(" 数值"), Asset->Badge.Magnitude, Row.Magnitude);
		TestEqual(Name + TEXT(" 稀有度"), Asset->Rarity, Row.Rarity);
		TestEqual(Name + TEXT(" 售价"), Asset->ShopPrice, Row.Price);
		TestTrue(Name + TEXT(" 绑定同编号的 GLB 与 PNG"), Asset->DisplayMesh == Mesh && Asset->Badge.Icon == Icon);
		TestEqual(Name + TEXT(" 保留源网格三角面"), Mesh->GetNumTriangles(0), 1020);
		const UBodySetup* Collision = Mesh->GetBodySetup();
		TestTrue(Name + TEXT(" 使用显示网格查询且禁用导航"),
			Collision && Collision->CollisionTraceFlag == CTF_UseComplexAsSimple && !Mesh->bHasNavigationData);
		TestEqual(Name + TEXT(" PNG 源图宽"), Icon->Source.GetSizeX(), int64{512});
		TestEqual(Name + TEXT(" PNG 源图高"), Icon->Source.GetSizeY(), int64{512});
		TestTrue(Name + TEXT(" PNG 保留 Alpha 和 sRGB"), Icon->HasAlphaChannel() && Icon->SRGB && !Icon->CompressionNoAlpha);
		TestTrue(Name + TEXT(" 使用 RGBA UI 压缩、UI 组且不生成 mip"),
			Icon->CompressionSettings == TC_EditorIcon && Icon->LODGroup == TEXTUREGROUP_UI && Icon->MipGenSettings == TMGS_NoMipmaps);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShopSkyIslandRarityTest, "MovingCastle.SkyIslands.Shop.RaritySampling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShopSkyIslandRarityTest::RunTest(const FString& Parameters)
{
	const auto* Config = LoadObject<UCastleShopSkyIslandDataAsset>(nullptr, ShopAssetPath);
	if (!TestNotNull(TEXT("正式商店配置"), Config)) { return false; }
	FText Reason;
	if (!TestTrue(TEXT("采样前配置通过校验"), Config->Validate(Reason))) { return false; }
	static constexpr int32 Expected[4][5] = {
		{80, 20, 0, 0, 0}, {60, 25, 15, 0, 0}, {45, 25, 20, 10, 0}, {35, 23, 20, 15, 7}
	};
	using FProduct = ACastleShopSkyIsland::FProduct;
	TArray<FProduct> Products;
	FRandomStream Seeds(489306);
	constexpr int32 SampleCount = 12000;
	for (int32 Level = 1; Level <= 4; ++Level)
	{
		const auto Percentages = Config->RarityWeightsByTowerLevel[Level - 1].GetPercentages();
		int32 Counts[4][5] = {};
		for (int32 Sample = 0; Sample < SampleCount; ++Sample)
		{
			if (!ACastleShopSkyIsland::TryRollProducts(*Config, Level, static_cast<int32>(Seeds.GetUnsignedInt()), Products, Reason))
			{
				AddError(Reason.ToString());
				return false;
			}
			if (Products.Num() != 4 || !Products[0].RoomGroup || !Products[1].Badge)
			{
				AddError(TEXT("四个货位必须至少包含一件房间组与一枚徽章。"));
				return false;
			}
			for (int32 Slot = 0; Slot < Products.Num(); ++Slot)
			{
				const auto& Product = Products[Slot];
				if (MakeArrayView(Products.GetData(), Slot).Contains(Product))
				{
					AddError(TEXT("正式货池在仍有候选时抽出了重复商品。"));
					return false;
				}
				const auto Rarity = Product.RoomGroup ? Product.RoomGroup->Rarity : Product.Badge->Rarity;
				++Counts[Slot][static_cast<int32>(Rarity) - static_cast<int32>(ECastleItemRarity::Common)];
			}
		}
		// 四个货位各自对比配置与样本，保底货位也必须服从同一张概率表。
		for (int32 Rarity = 0; Rarity < 5; ++Rarity)
		{
			const int32 ExpectedPercent = Expected[Level - 1][Rarity];
			TestEqual(FString::Printf(TEXT("Lv%d 档 %d 的配置百分比"), Level, Rarity + 1), Percentages[Rarity], ExpectedPercent);
			for (int32 Slot = 0; Slot < 4; ++Slot)
			{
				const double Observed = 100.0 * Counts[Slot][Rarity] / SampleCount;
				TestTrue(FString::Printf(TEXT("Lv%d 货位 %d 档 %d 实测 %.2f%%，期望 %d%%"), Level, Slot + 1, Rarity + 1, Observed, ExpectedPercent),
					ExpectedPercent == 0 ? Counts[Slot][Rarity] == 0 : FMath::Abs(Observed - ExpectedPercent) < 2.0);
			}
		}
	}

	auto* Scarce = DuplicateObject(Config, GetTransientPackage());
	Scarce->RoomGroupPool.SetNum(1);
	Scarce->BadgePool.SetNum(1);
	for (FCastleShopRarityWeights& Row : Scarce->RarityWeightsByTowerLevel) { Row = {100, 0, 0, 0, 0}; }
	if (!TestTrue(TEXT("普通档只有一房间组一徽章的显式配置可用"), Scarce->Validate(Reason))) { return false; }
	AddExpectedMessage(TEXT("档已无未展示商品"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestTrue(TEXT("抽空普通档后可重复上架该档商品"), ACastleShopSkyIsland::TryRollProducts(*Scarce, 1, 47, Products, Reason));
	TestEqual(TEXT("补齐四个货位"), Products.Num(), 4);
	for (const FProduct& Product : Products)
	{
		TestTrue(TEXT("售卖货位始终留在已抽中的档位"),
			Product.RoomGroup == Scarce->RoomGroupPool[0] || Product.Badge == Scarce->BadgePool[0]);
	}
	AddExpectedError(TEXT("没有对应的稀有度概率行"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("拒绝无效主塔 Lv0"), ACastleShopSkyIsland::TryRollProducts(*Config, 0, 47, Products, Reason));
	TestFalse(TEXT("拒绝配置表之外的主塔 Lv5"), ACastleShopSkyIsland::TryRollProducts(*Config, 5, 47, Products, Reason));
	return true;
}


#endif
