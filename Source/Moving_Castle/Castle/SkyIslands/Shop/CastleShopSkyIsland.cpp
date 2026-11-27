#include "CastleShopSkyIsland.h"

#include "CastleShopSkyIslandDataAsset.h"
#include "Castle/Components/CastleEconomyComponent.h"
#include "Castle/Components/CastleMainTowerBalloonComponent.h"
#include "Castle/Items/CastleBadgeDataAsset.h"
#include "Castle/Items/CastleItemGrantLibrary.h"
#include "Castle/Items/CastlePendingRoomReward.h"
#include "Castle/Items/CastleRoomGroupDataAsset.h"
#include "Castle/SkyIslands/Rest/CastleSkyIslandAcceptance.h"
#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastlePlayerController.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "PhysicsEngine/BodySetup.h"

namespace
{
	constexpr double RoomPackUniformScale = 14.5;
	// 徽章底边原点保留导入尺寸。放大后与房间包高度相近，价格牌位于两者上方。
	constexpr double BadgeUniformScale = 25.0;
	constexpr int32 ShopSlotCount = 4;
	constexpr int32 TowerLevelCount = 4;
	constexpr int32 RarityCount = 5;
	constexpr int32 FullPercent = 100;
	const FName ShopOfferSoldAmountParameter(TEXT("SoldAmount"));

	int32 GetRarityIndex(const ECastleItemRarity Rarity)
	{
		return static_cast<int32>(Rarity) - static_cast<int32>(ECastleItemRarity::Common);
	}

	ECastleItemRarity GetRarity(const int32 Index)
	{
		return static_cast<ECastleItemRarity>(static_cast<int32>(ECastleItemRarity::Common) + Index);
	}

	FText GetRarityName(const int32 Index)
	{
		return StaticEnum<ECastleItemRarity>()->GetDisplayNameTextByValue(static_cast<int64>(GetRarity(Index)));
	}

	/** 货位使用网格自身的查询几何，显示、导航和准星命中的配置必须一致。 */
	bool HasDisplayGeometry(const UStaticMesh* Mesh, const double Scale)
	{
		if (!IsValid(Mesh) || Mesh->bHasNavigationData || Mesh->GetNumTriangles(0) <= 0)
		{
			return false;
		}
		const UBodySetup* Body = Mesh->GetBodySetup();
		const FBox Bounds = Mesh->GetBoundingBox();
		const double Height = Bounds.GetSize().Z * Scale;
		const double Top = Bounds.Max.Z * Scale;
		return Body && (Body->CollisionTraceFlag == CTF_UseComplexAsSimple || !Body->AggGeom.BoxElems.IsEmpty())
			&& FMath::Abs(Bounds.Min.Z * Scale) <= 1.0 && Height >= 180.0 && Height <= 200.0
			&& Top >= 180.0 && Top <= 200.0;
	}

	/** 默认售卖态须保留原画面，售罄由本货位的实例将参数改为 1。 */
	bool HasSoldParameter(const UMaterialInterface* Material)
	{
		float InitialSoldAmount;
		return IsValid(Material) && Material->GetScalarParameterValue(ShopOfferSoldAmountParameter, InitialSoldAmount)
			&& InitialSoldAmount == 0.0f;
	}
}

bool UCastleShopSkyIslandDataAsset::Validate(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	const auto Reject = [this, &OutReason](const FText& Reason)
	{
		OutReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("商店空岛配置 %s：%s"), *GetPathName(), *Reason.ToString());
		return false;
	};
	if (OfferCount != ShopSlotCount || RarityWeightsByTowerLevel.Num() != TowerLevelCount)
	{
		return Reject(INVTEXT("必须配置四个货位，以及从主塔 Lv1 到 Lv4 的四行稀有度概率。"));
	}
	if (RoomGroupPool.IsEmpty() || BadgePool.IsEmpty())
	{
		return Reject(INVTEXT("房间组和徽章商品池都必须配置。"));
	}
	// 拒绝没有可查询包体或会参与导航的网格，确保卡包按实物尺寸贴地陈列。
	if (!HasDisplayGeometry(RoomPackMesh, RoomPackUniformScale))
	{
		return Reject(FText::Format(INVTEXT("RoomPackMesh {0} 必须具备有效网格和查询碰撞、禁用导航且缩放后贴地并高 180 至 200 cm。"),
			FText::FromString(GetPathNameSafe(RoomPackMesh))));
	}
	// 逐档检查引用和售罄参数。多出的键也拒绝，防止错误的稀有度藏在完整配置中。
	for (int32 Index = 0; Index < RarityCount; ++Index)
	{
		const UMaterialInterface* Material = RoomPackMaterials.FindRef(GetRarity(Index)).Get();
		if (!HasSoldParameter(Material))
		{
			return Reject(FText::Format(INVTEXT("RoomPackMaterials 的 {0} 档材质 {1} 缺失，或没有初值为 0 的 SoldAmount 参数。"),
				GetRarityName(Index), FText::FromString(GetPathNameSafe(Material))));
		}
	}
	if (RoomPackMaterials.Num() != RarityCount)
	{
		return Reject(INVTEXT("RoomPackMaterials 只能配置白、蓝、紫、金和炫彩五个稀有度。"));
	}
	bool bRoomAvailable[RarityCount] = {};
	bool bBadgeAvailable[RarityCount] = {};
	TSet<const UObject*> SeenProducts;
	for (const UCastleRoomGroupDataAsset* RoomGroup : RoomGroupPool)
	{
		if (!IsValid(RoomGroup) || RoomGroup->DisplayName.IsEmpty() || RoomGroup->ShopPrice < 0 || SeenProducts.Contains(RoomGroup))
		{
			return Reject(FText::Format(INVTEXT("房间组商品 {0} 缺失、名称为空、售价为负或在池中重复。"),
				FText::FromString(GetPathNameSafe(RoomGroup))));
		}
		if (!RoomGroup->ValidateRarity(OutReason)) { return false; }
		bRoomAvailable[GetRarityIndex(RoomGroup->Rarity)] = true;
		SeenProducts.Add(RoomGroup);
	}
	for (const UCastleBadgeDataAsset* Badge : BadgePool)
	{
		const int32 Index = IsValid(Badge) ? GetRarityIndex(Badge->Rarity) : INDEX_NONE;
		const FText Name = FText::FromString(GetPathNameSafe(Badge));
		if (Index < 0 || Index >= RarityCount || Badge->ShopPrice < 0 || SeenProducts.Contains(Badge))
		{
			return Reject(FText::Format(INVTEXT("徽章商品 {0} 缺失、稀有度无效、售价为负或在池中重复。"), Name));
		}
		FText Reason;
		if (!Badge->Badge.Validate(Reason))
		{
			return Reject(FText::Format(INVTEXT("徽章商品 {0}：{1}"), Name, Reason));
		}
		const UTexture2D* Icon = Badge->Badge.Icon;
		// 校验导入尺寸。编辑器首次加载时 GPU 平台数据可能在异步编译，GetSizeX/Y 会返回临时默认纹理尺寸。
		if (!IsValid(Icon) || Icon->GetImportedSize() != FIntPoint(512, 512) || !Icon->SRGB
			|| Icon->LODGroup != TEXTUREGROUP_UI || Icon->CompressionSettings != TC_EditorIcon)
		{
			return Reject(FText::Format(INVTEXT("徽章 {0} 的图标 {1} 必须是 512×512 的 sRGB RGBA UI 纹理。"),
				Name, FText::FromString(GetPathNameSafe(Icon))));
		}
		const UStaticMesh* Mesh = Badge->DisplayMesh;
		if (!HasDisplayGeometry(Mesh, BadgeUniformScale) || Mesh->GetStaticMaterials().Num() != 1
			|| !HasSoldParameter(Mesh->GetMaterial(0)))
		{
			return Reject(FText::Format(INVTEXT("徽章 {0} 的网格 {1} 必须具备贴地的查询几何、禁用导航且具有一个支持 SoldAmount 的材质槽。"),
				Name, FText::FromString(GetPathNameSafe(Mesh))));
		}
		bBadgeAvailable[Index] = true;
		SeenProducts.Add(Badge);
	}
	for (int32 LevelIndex = 0; LevelIndex < TowerLevelCount; ++LevelIndex)
	{
		const auto Percentages = RarityWeightsByTowerLevel[LevelIndex].GetPercentages();
		const FText Level = FText::AsNumber(LevelIndex + 1);
		int64 Total = 0;
		for (int32 Index = 0; Index < RarityCount; ++Index)
		{
			const int32 Percent = Percentages[Index];
			if (Percent < 0)
			{
				return Reject(FText::Format(INVTEXT("主塔 Lv{0} 的 {1} 档概率为负。"), Level, GetRarityName(Index)));
			}
			Total += Percent;
		}
		if (Total != FullPercent)
		{
			return Reject(FText::Format(INVTEXT("主塔 Lv{0} 的五档概率之和必须为 100，实际为 {1}。"),
				Level, FText::AsNumber(Total)));
		}
		for (int32 Index = 0; Index < RarityCount; ++Index)
		{
			// 两个保底品类货位都能抽到任一开启档位，缺档会在玩家传送前拒绝。
			if (Percentages[Index] > 0 && (!bRoomAvailable[Index] || !bBadgeAvailable[Index]))
			{
				return Reject(FText::Format(INVTEXT("主塔 Lv{0} 的 {1} 档必须同时提供房间组和徽章。"),
					Level, GetRarityName(Index)));
			}
		}
	}
	return true;
}

FText UCastleShopSkyIslandOffer::GetInteractPrompt() const
{
	if (bSold)
	{
		return FText::Format(INVTEXT("{0}（已售罄）"), ProductName);
	}
	return FText::Format(INVTEXT("{0}　{1} 金币"), ProductName, FText::AsNumber(Price));
}

bool UCastleShopSkyIslandOffer::CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	const auto* Island = Cast<ACastleShopSkyIsland>(GetOwner());
	if (!Island || Context.Island != Island || !Island->IsIslandController(Context.Controller)
		|| Context.Hit.GetComponent() != this || !OfferMaterialInstance || !Sign)
	{
		OutReason = INVTEXT("请用当前玩家的准星对准本岛已初始化的商品台。");
		return false;
	}
	if (bSold || bSubmitting)
	{
		OutReason = bSold ? GetInteractPrompt() : INVTEXT("正在处理本商品的购买。");
		UE_LOG(LogMoving_Castle, Display, TEXT("商店空岛商品 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	return true;
}

bool UCastleShopSkyIslandOffer::ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason)
{
	if (!CanInteract(Context, OutReason)) { return false; }
	// 金币和道具会同步通知观察者。同一台座的购买必须等当前提交返回。
	TGuardValue<bool> Guard(bSubmitting, true);
	return CastChecked<ACastleShopSkyIsland>(GetOwner())->Purchase(*this, OutReason);
}

void UCastleShopSkyIslandOffer::MarkSold()
{
	bSold = true;
	// 保留原网格与准星查询碰撞。房间包和徽章的动态材质都使用同一售罄参数。
	OfferMaterialInstance->SetScalarParameterValue(ShopOfferSoldAmountParameter, 1.0f);
	Sign->SetText(FText::FromString(DisplayLabel + TEXT("\nSOLD OUT")));
}

ACastleShopSkyIsland::ACastleShopSkyIsland()
{
	// 绑定该玩法的岛体，位置沿用作者对齐的岛面中心。
	SetIslandArtMesh(TEXT("SM_SkyIsland_Shop"));
	SetDepartureArtMesh(TEXT("SM_SkyIsland_Departure_Shop"));
	ConfigAsset = TSoftObjectPtr<UCastleShopSkyIslandDataAsset>(FSoftObjectPath(
		TEXT("/Game/MovingCastle/Data/SkyIslands/DA_ShopSkyIsland.DA_ShopSkyIsland")));
}

bool ACastleShopSkyIsland::IsIslandController(const ACastlePlayerController* Controller) const
{
	return IsValid(Controller) && GetIslandContext().Controller.Get() == Controller;
}

bool ACastleShopSkyIsland::OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutReason)
{
	Config = ConfigAsset.LoadSynchronous();
	if (Context.Type != ECastleSkyIslandType::Shop || !IsValid(Config))
	{
		return RejectOffer(INVTEXT("商店空岛需要正确类型和有效配置资产。"), OutReason);
	}
	if (!Config->Validate(OutReason)) { return false; }

	// 与导演使用同一世界唯一棋盘，发牌定义和存储从这个棋盘成对取得。
	TActorIterator<ACastleBoard> BoardIt(GetWorld());
	ACastleBoard* MatchedBoard = BoardIt ? *BoardIt : nullptr;
	Economy = Context.Controller->GetEconomy();
	if (!IsValid(MatchedBoard) || !IsValid(MatchedBoard->PrototypeBackpack) || !Economy.IsValid())
	{
		return RejectOffer(INVTEXT("商店空岛需要本局棋盘、背包和金币账户。"), OutReason);
	}
	++BoardIt;
	if (BoardIt) { return RejectOffer(INVTEXT("本局存在多座棋盘，无法确定商店背包。"), OutReason); }
	Board = MatchedBoard;

	// 棋盘已提交的开放层数是主塔等级的来源，受击和徽章改变血量时不会影响稀有度。
	const int32 TowerLevel = UCastleMainTowerBalloonComponent::FindTierIndex(MatchedBoard->GetUnlockedLayerCount()) + 1;
	TArray<FProduct> Products;
	if (!TryRollProducts(*Config, TowerLevel, Context.Seed, Products, OutReason)) { return false; }
	for (const FProduct& Product : Products)
	{
		if (!CreateOffer(Product, OutReason)) { return false; }
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("商店空岛 %s 按主塔 Lv%d 生成 %d 个商品台，种子 %d。"),
		*GetPathName(), TowerLevel, Offers.Num(), Context.Seed);
	return true;
}

bool ACastleShopSkyIsland::TryRollProducts(const UCastleShopSkyIslandDataAsset& Config, const int32 TowerLevel,
	const int32 Seed, TArray<FProduct>& OutProducts, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const auto Reject = [&Config, &OutReason](const FText& Reason)
	{
		OutReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("商店空岛配置 %s：%s"), *Config.GetPathName(), *Reason.ToString());
		return false;
	};
	if (TowerLevel < 1 || TowerLevel > TowerLevelCount || !Config.RarityWeightsByTowerLevel.IsValidIndex(TowerLevel - 1))
	{
		return Reject(FText::Format(INVTEXT("主塔 Lv{0} 没有对应的稀有度概率行。"), FText::AsNumber(TowerLevel)));
	}

	// 建立五档独立池。池序只决定种子到单品的映射，稀有度概率与档位内商品数量无关。
	TArray<FProduct, TInlineAllocator<12>> Pools[RarityCount];
	for (UCastleRoomGroupDataAsset* RoomGroup : Config.RoomGroupPool)
	{
		Pools[GetRarityIndex(RoomGroup->Rarity)].Add({RoomGroup, nullptr, 0});
	}
	for (UCastleBadgeDataAsset* Badge : Config.BadgePool)
	{
		Pools[GetRarityIndex(Badge->Rarity)].Add({nullptr, Badge, 0});
	}
	const auto Percentages = Config.RarityWeightsByTowerLevel[TowerLevel - 1].GetPercentages();
	FRandomStream Random(Seed);
	TArray<FProduct> Products;
	Products.Reserve(ShopSlotCount);
	for (int32 Slot = 0; Slot < ShopSlotCount; ++Slot)
	{
		int32 Roll = Random.RandRange(0, FullPercent - 1);
		int32 RarityIndex = 0;
		for (; RarityIndex < RarityCount && Roll >= Percentages[RarityIndex]; ++RarityIndex)
		{
			Roll -= Percentages[RarityIndex];
		}
		if (RarityIndex == RarityCount)
		{
			return Reject(FText::Format(INVTEXT("主塔 Lv{0} 的概率表未覆盖本次抽样。"), FText::AsNumber(TowerLevel)));
		}
		// 第一个货位只抽房间组，第二个只抽徽章；其余两个在该档合并池中等权抽取。
		TArray<FProduct, TInlineAllocator<12>> Eligible;
		TArray<FProduct, TInlineAllocator<12>> Unused;
		for (const FProduct& Product : Pools[RarityIndex])
		{
			if ((Slot == 0 && !Product.RoomGroup) || (Slot == 1 && !Product.Badge)) { continue; }
			Eligible.Add(Product);
			if (!Products.Contains(Product)) { Unused.Add(Product); }
		}
		if (Eligible.IsEmpty())
		{
			return Reject(FText::Format(INVTEXT("主塔 Lv{0} 的第 {1} 个货位在 {2} 档没有符合品类的商品。"),
				FText::AsNumber(TowerLevel), FText::AsNumber(Slot + 1), GetRarityName(RarityIndex)));
		}
		if (Unused.IsEmpty())
		{
			// 指南 D14 允许在该档的同品类库存用尽后重复上架，且抽样概率不变。
			UE_LOG(LogMoving_Castle, Warning, TEXT("商店空岛配置 %s 主塔 Lv%d 第 %d 个货位的 %s 档已无未展示商品，从该档符合品类的全池抽取。"),
				*Config.GetPathName(), TowerLevel, Slot + 1, *GetRarityName(RarityIndex).ToString());
			Unused = MoveTemp(Eligible);
		}
		FProduct& Product = Products.Add_GetRef(Unused[Random.RandRange(0, Unused.Num() - 1)]);
		Product.RewardSeed = static_cast<int32>(Random.GetUnsignedInt());
	}
	OutProducts = MoveTemp(Products);
	return true;
}

bool ACastleShopSkyIsland::RejectOffer(const FText& Reason, FText& OutReason) const
{
	OutReason = Reason;
	UE_LOG(LogMoving_Castle, Error, TEXT("商店空岛 %s，配置 %s：%s"),
		*GetPathName(), *ConfigAsset.ToString(), *Reason.ToString());
	return false;
}

bool ACastleShopSkyIsland::CreateOffer(const FProduct& Product, FText& OutReason)
{
	UCastleRoomGroupDataAsset* RoomGroup = Product.RoomGroup;
	UCastleBadgeDataAsset* Badge = Product.Badge;
	// 台座排在入口两旁，中心始终留给去离开门的飞行路径。文本独立挂在根上以免随网格缩放。
	const int32 Index = Offers.Num();
	const bool bLeftSide = Index % 2 == 0;
	// 房间包底面落在岛面 101 cm 高的展示台上，徽章网格则直接贴岛面。
	const double DisplayBaseHeight = RoomGroup ? 101.0 : 0.0;
	const FVector Location(-330.0 + 320.0 * (Index / 2), bLeftSide ? -570.0 : 570.0, DisplayBaseHeight);
	const FName OfferName(*FString::Printf(TEXT("Offer%d"), Index + 1));
	auto* Offer = NewObject<UCastleShopSkyIslandOffer>(this, OfferName);
	AddInstanceComponent(Offer);
	Offer->SetupAttachment(GetRootComponent());
	Offer->SetRelativeLocation(Location);
	// 卡包印字面和徽章珐琅面导入后都朝局部 +Y。两侧货位旋转成正对中央通道。
	Offer->SetRelativeRotation(FRotator(0.0, bLeftSide ? 0.0 : 180.0, 0.0));
	if (RoomGroup)
	{
		// 台座饰圈顶面距地约 101 厘米，卡包底面贴台；印字面朝向中央通道。
		Offer->SetStaticMesh(Config->RoomPackMesh);
		Offer->SetRelativeScale3D(FVector(RoomPackUniformScale));
	}
	else
	{
		Offer->SetStaticMesh(Badge->DisplayMesh);
		Offer->SetRelativeScale3D(FVector(BadgeUniformScale));
	}
	Offer->SetCanEverAffectNavigation(false);
	Offer->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Offer->SetCollisionResponseToAllChannels(ECR_Ignore);
	Offer->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Offer->RoomGroup = RoomGroup;
	Offer->Badge = Badge;
	Offer->RewardSeed = Product.RewardSeed;
	Offer->Price = RoomGroup ? RoomGroup->ShopPrice : Badge->ShopPrice;
	Offer->ProductName = RoomGroup ? RoomGroup->DisplayName : Badge->Badge.GetDisplayName();
	// 包装名用资产身份，中文交互标题仍使用配置好的 DisplayName。
	const FString TypeName = RoomGroup ? RoomGroup->GetName()
		: StaticEnum<ECastleBadgeType>()->GetNameStringByValue(static_cast<int64>(Badge->Badge.Type));
	Offer->DisplayLabel = FName::NameToDisplayString(TypeName, false);
	UMaterialInterface* Material = RoomGroup ? Config->RoomPackMaterials.FindChecked(RoomGroup->Rarity).Get() : Badge->DisplayMesh->GetMaterial(0);
	Offer->OfferMaterialInstance = Offer->CreateDynamicMaterialInstance(0, Material);
	if (!Offer->OfferMaterialInstance)
	{
		return RejectOffer(FText::Format(INVTEXT("商品 {0} 无法从材质 {1} 创建动态材质。"),
			FText::FromString(GetPathNameSafe(RoomGroup ? static_cast<UObject*>(RoomGroup) : Badge)),
			FText::FromString(GetPathNameSafe(Material))), OutReason);
	}
	Offer->RegisterComponent();

	const FName SignName(*FString::Printf(TEXT("OfferSign%d"), Index + 1));
	Offer->Sign = NewObject<UTextRenderComponent>(this, SignName);
	AddInstanceComponent(Offer->Sign);
	Offer->Sign->SetupAttachment(GetRootComponent());
	Offer->Sign->SetRelativeLocation(FVector(Location.X - 86.0, Location.Y + (bLeftSide ? 100.0 : -100.0), Location.Z + 230.0));
	Offer->Sign->SetRelativeRotation(FRotator(0, 180, 0));
	Offer->Sign->SetHorizontalAlignment(EHTA_Center);
	Offer->Sign->SetWorldSize(30.0f);
	Offer->Sign->SetText(FText::FromString(FString::Printf(TEXT("%s\n%d GOLD"), *Offer->DisplayLabel, Offer->Price)));
	Offer->Sign->RegisterComponent();
	Offers.Add(Offer);
	return true;
}

bool ACastleShopSkyIsland::CheckFunds(const UCastleShopSkyIslandOffer& Offer, FText& OutReason) const
{
	if (!Economy.IsValid()) { return RejectOffer(INVTEXT("本局金币账户已经失效。"), OutReason); }
	if (Economy->CanSpendGold(Offer.Price, OutReason)) { return true; }
	UE_LOG(LogMoving_Castle, Warning, TEXT("商店空岛 %s 的商品 %s 无法付款：%s"),
		*GetPathName(), *Offer.ProductName.ToString(), *OutReason.ToString());
	return false;
}

bool ACastleShopSkyIsland::Purchase(UCastleShopSkyIslandOffer& Offer, FText& OutReason)
{
	if (OpenOffer || bResolvingSale)
	{
		OutReason = INVTEXT("请先结束正在处理的空岛商品购买。");
		return false;
	}
	if (!CheckFunds(Offer, OutReason)) { return false; }
	TGuardValue<bool> Guard(bResolvingSale, true);

	if (Offer.Badge)
	{
		return Economy->TrySpendShopGold(Offer.Price, [&]()
		{
			if (!GrantBadgeWithReplaceUI(*Offer.Badge, OutReason)) { return false; }
			// 满六枚时，待替换状态同样表示发放成功。替换或舍弃由徽章界面完成。
			CompleteSale(Offer);
			return true;
		}, OutReason);
	}
	auto* Backpack = Board.IsValid() ? Board->PrototypeBackpack.Get() : nullptr;
	if (!IsValid(Backpack) || Backpack->Capacity < 1)
	{
		return RejectOffer(INVTEXT("本局背包已失效或容量配置无效。"), OutReason);
	}
	if (Offer.PendingReward) { return OpenPendingOffer(Offer, OutReason); }

	// 先准备整批房间与牌面，确定卡提交前才扣款。万能卡把扣款延迟到实际确认。
	FRandomStream Random(Offer.RewardSeed);
	FCastlePreparedRoomGroup Prepared;
	if (!UCastleItemGrantLibrary::TryPrepareRoomGroup(Offer.RoomGroup, Backpack, Board.Get(), Board->BuildingDefinitions,
		Random, Prepared, OutReason))
	{
		return false;
	}
	if (UCastlePendingRoomReward* Pending = Prepared.PendingReward)
	{
		const TWeakObjectPtr<ACastleShopSkyIsland> WeakIsland(this);
		const TWeakObjectPtr<UCastleShopSkyIslandOffer> WeakOffer(&Offer);
		if (!Pending->SetClaimTransaction(
			[WeakIsland, WeakOffer](FText& Reason)
			{
				auto* Island = WeakIsland.Get();
				auto* Target = WeakOffer.Get();
				if (!Island || !Target || Island->OpenOffer != Target || Target->bSold || Island->bResolvingSale)
				{
					Reason = INVTEXT("万能卡商品不再有有效的购买上下文。");
					UE_LOG(LogMoving_Castle, Error, TEXT("商店空岛 %s 的万能卡确认失败：%s"),
						*GetPathNameSafe(Target), *Reason.ToString());
					return false;
				}
				if (!Island->CheckFunds(*Target, Reason)) { return false; }
				Island->bResolvingSale = true;
				if (Island->Economy->TrySpendGold(Target->Price, Reason)) { return true; }
				Island->bResolvingSale = false;
				return false;
			},
			[WeakIsland, WeakOffer](const UCastlePendingRoomReward::EClaimResult Result)
			{
				auto* Island = WeakIsland.Get();
				auto* Target = WeakOffer.Get();
				if (!Island || !Target) { return; }
				switch (Result)
				{
				case UCastlePendingRoomReward::EClaimResult::Cancelled:
					Island->OpenOffer = nullptr;
					UE_LOG(LogMoving_Castle, Display, TEXT("商店空岛 %s 已取消万能卡购买，商品保留。"),
						*Target->GetPathName());
					return;
				case UCastlePendingRoomReward::EClaimResult::WriteFailed:
					Island->RefundFailedOffer(*Target, INVTEXT("背包拒绝写入万能卡。"));
					break;
				case UCastlePendingRoomReward::EClaimResult::Granted:
					Island->CompleteSale(*Target);
					Island->OpenOffer = nullptr;
					break;
				}
				Island->bResolvingSale = false;
			}, OutReason))
		{
			return false;
		}
		Offer.PendingReward = Pending;
		return OpenPendingOffer(Offer, OutReason);
	}

	// 确定房间组只有整批写入成功才更新售罄和余额，准备后的快照失效会拒绝整笔交易。
	return Economy->TrySpendShopGold(Offer.Price, [&]()
	{
		if (!UCastleItemGrantLibrary::TryCommitPreparedRoomGroup(Prepared, Backpack, OutReason)) { return false; }
		CompleteSale(Offer);
		return true;
	}, OutReason);
}

bool ACastleShopSkyIsland::OpenPendingOffer(UCastleShopSkyIslandOffer& Offer, FText& OutReason)
{
	OpenOffer = &Offer;
	if (GrantPendingRoomRewardUI(Offer.PendingReward, OutReason)) { return true; }
	OpenOffer = nullptr;
	const FText Failure = OutReason;
	return RejectOffer(Failure, OutReason);
}

void ACastleShopSkyIsland::RefundFailedOffer(const UCastleShopSkyIslandOffer& Offer, const FText& Failure)
{
	FText RefundFailure;
	if (!Economy.IsValid() || !Economy->TryAddGold(Offer.Price, RefundFailure))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店空岛 %s 的商品 %s 发放与退款均失败：%s / %s"),
			*GetPathName(), *Offer.ProductName.ToString(), *Failure.ToString(), *RefundFailure.ToString());
		return;
	}
	UE_LOG(LogMoving_Castle, Error, TEXT("商店空岛 %s 的商品 %s 发放失败，已退还 %d 金币：%s"),
		*GetPathName(), *Offer.ProductName.ToString(), Offer.Price, *Failure.ToString());
}

void ACastleShopSkyIsland::CompleteSale(UCastleShopSkyIslandOffer& Offer)
{
	Offer.MarkSold();
	const FText Message = FText::Format(INVTEXT("已购买 {0}，支付 {1} 金币。"), Offer.ProductName, FText::AsNumber(Offer.Price));
	UE_LOG(LogMoving_Castle, Display, TEXT("商店空岛 %s：%s"), *GetPathName(), *Message.ToString());
	if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 4.0f, FColor::Green, Message.ToString()); }
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld CastleTestShopSkyIslandCommand(
	TEXT("CastleTestShopSkyIsland"), TEXT("Enter the current shop sky island reward node for acceptance."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		CastleQueueSkyIslandAcceptance(World, ECastleSkyIslandType::Shop, ACastleShopSkyIsland::StaticClass());
	}));
#endif
