#include "CastleShopSkyIsland.h"

#include "CastleShopSkyIslandDataAsset.h"
#include "Castle/Components/CastleEconomyComponent.h"
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
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr double RoomPackUniformScale = 14.5;
	const FName RoomPackSoldParameter(TEXT("SoldAmount"));
	constexpr ECastleItemRarity RoomPackRarities[] = {
		ECastleItemRarity::Common, ECastleItemRarity::Uncommon, ECastleItemRarity::Rare,
		ECastleItemRarity::Epic, ECastleItemRarity::Legendary
	};

	/** 单个抽样条目只指向一种商品，可用同一随机流混合两个池。 */
	struct FCastleShopIslandProduct
	{
		UCastleRoomGroupDataAsset* RoomGroup = nullptr;
		UCastleBadgeDataAsset* Badge = nullptr;

		bool operator==(const FCastleShopIslandProduct& Other) const
		{
			return RoomGroup == Other.RoomGroup && Badge == Other.Badge;
		}
	};
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
	if (OfferCount < 3 || OfferCount > 4 || DefaultRoomGroupPrice < 0 || DefaultBadgePrice < 0)
	{
		return Reject(INVTEXT("商品数量必须为 3 至 4，且房间组和徽章售价必须非负。"));
	}
	if (RoomGroupPool.IsEmpty() || BadgePool.IsEmpty())
	{
		return Reject(INVTEXT("房间组和徽章商品池都必须配置。"));
	}
	// 拒绝没有可查询包体或会参与导航的网格，确保卡包按实物尺寸贴地陈列。
	if (!IsValid(RoomPackMesh) || RoomPackMesh->GetNumTriangles(0) <= 0)
	{
		return Reject(FText::Format(INVTEXT("RoomPackMesh {0} 缺失或没有可用三角面。"),
			FText::FromString(GetPathNameSafe(RoomPackMesh))));
	}
	const UBodySetup* BodySetup = RoomPackMesh->GetBodySetup();
	const FBox MeshBounds = RoomPackMesh->GetBoundingBox();
	const double DisplayHeight = MeshBounds.GetSize().Z * RoomPackUniformScale;
	const double DisplayTop = MeshBounds.Max.Z * RoomPackUniformScale;
	if (!BodySetup || (BodySetup->CollisionTraceFlag != CTF_UseComplexAsSimple && BodySetup->AggGeom.BoxElems.IsEmpty())
		|| RoomPackMesh->bHasNavigationData || FMath::Abs(MeshBounds.Min.Z * RoomPackUniformScale) > 1.0
		|| DisplayHeight < 180.0 || DisplayHeight > 200.0 || DisplayTop < 180.0 || DisplayTop > 200.0)
	{
		return Reject(FText::Format(INVTEXT("RoomPackMesh {0} 必须具备查询碰撞、禁用导航且缩放后贴地并高 180 至 200 cm。"),
			FText::FromString(RoomPackMesh->GetPathName())));
	}
	// 逐档检查引用和售罄参数。多出的键也拒绝，防止错误的稀有度藏在完整配置中。
	for (const ECastleItemRarity Rarity : RoomPackRarities)
	{
		const UMaterialInterface* Material = RoomPackMaterials.FindRef(Rarity).Get();
		float InitialSoldAmount;
		if (!IsValid(Material) || !Material->GetScalarParameterValue(RoomPackSoldParameter, InitialSoldAmount) || InitialSoldAmount != 0.0f)
		{
			return Reject(FText::Format(INVTEXT("RoomPackMaterials 的 {0} 档材质 {1} 缺失，或没有初值为 0 的 SoldAmount 参数。"),
				FText::FromString(StaticEnum<ECastleItemRarity>()->GetNameStringByValue(static_cast<int64>(Rarity))),
				FText::FromString(GetPathNameSafe(Material))));
		}
	}
	if (RoomPackMaterials.Num() != UE_ARRAY_COUNT(RoomPackRarities))
	{
		return Reject(INVTEXT("RoomPackMaterials 只能配置白、蓝、紫、金和炫彩五个稀有度。"));
	}
	for (const UCastleRoomGroupDataAsset* RoomGroup : RoomGroupPool)
	{
		if (!IsValid(RoomGroup) || RoomGroup->DisplayName.IsEmpty())
		{
			return Reject(FText::Format(INVTEXT("房间组商品 {0} 的资产或名称无效。"),
				FText::FromString(GetPathNameSafe(RoomGroup))));
		}
		if (!RoomGroup->ValidateRarity(OutReason)) { return false; }
	}
	for (const UCastleBadgeDataAsset* Badge : BadgePool)
	{
		FText Reason = INVTEXT("徽章商品资产无效。");
		if (!IsValid(Badge) || !Badge->Badge.Validate(Reason))
		{
			return Reject(FText::Format(INVTEXT("{0}：{1}"), FText::FromString(GetPathNameSafe(Badge)), Reason));
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
	// 保留原网格与准星查询碰撞。房间包由母材质压暗置灰，徽章沿用原有着色。
	if (RoomGroup)
	{
		OfferMaterialInstance->SetScalarParameterValue(RoomPackSoldParameter, 1.0f);
	}
	else
	{
		OfferMaterialInstance->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.23f, 0.23f, 0.23f));
	}
	Sign->SetText(FText::FromString(DisplayLabel + TEXT("\nSOLD OUT")));
}

ACastleShopSkyIsland::ACastleShopSkyIsland()
{
	ConfigAsset = TSoftObjectPtr<UCastleShopSkyIslandDataAsset>(FSoftObjectPath(
		TEXT("/Game/MovingCastle/Data/SkyIslands/DA_ShopSkyIsland.DA_ShopSkyIsland")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BadgeMesh = Sphere.Object;
	BadgeMaterial = Material.Object;
}

bool ACastleShopSkyIsland::IsIslandController(const ACastlePlayerController* Controller) const
{
	return IsValid(Controller) && GetIslandContext().Controller.Get() == Controller;
}

bool ACastleShopSkyIsland::OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutReason)
{
	Config = ConfigAsset.LoadSynchronous();
	FLinearColor MaterialTint;
	if (Context.Type != ECastleSkyIslandType::Shop || !Config || !BadgeMesh || !BadgeMaterial
		|| !BadgeMaterial->GetVectorParameterValue(FName(TEXT("Color")), MaterialTint))
	{
		return RejectOffer(INVTEXT("商店空岛需要正确类型、配置资产、徽章网格及带 Color 参数的徽章材质。"), OutReason);
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

	FRandomStream Random(Context.Seed);
	TArray<FCastleShopIslandProduct> Pool;
	for (UCastleRoomGroupDataAsset* Group : Config->RoomGroupPool) { Pool.Add({Group, nullptr}); }
	for (UCastleBadgeDataAsset* Badge : Config->BadgePool) { Pool.Add({nullptr, Badge}); }
	TArray<FCastleShopIslandProduct> Remaining = Pool;
	const auto AddProduct = [this, &Random, &Remaining, &OutReason](const FCastleShopIslandProduct Product)
	{
		Remaining.Remove(Product);
		return CreateOffer(Product.RoomGroup, Product.Badge, static_cast<int32>(Random.GetUnsignedInt()), OutReason);
	};
	const int32 RoomCount = Config->RoomGroupPool.Num();
	if (!AddProduct(Pool[Random.RandRange(0, RoomCount - 1)]) || !AddProduct(Pool[Random.RandRange(RoomCount, Pool.Num() - 1)]))
	{
		return false;
	}
	// 剩余货位按池条目等权抽取，优先不同商品；已展示所有不同商品后仍按原池配置填满货位。
	while (Offers.Num() < Config->OfferCount)
	{
		const auto& Candidates = Remaining.IsEmpty() ? Pool : Remaining;
		if (!AddProduct(Candidates[Random.RandRange(0, Candidates.Num() - 1)])) { return false; }
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("商店空岛 %s 已生成 %d 个商品台，种子 %d。"), *GetPathName(), Offers.Num(), Context.Seed);
	return true;
}

bool ACastleShopSkyIsland::RejectOffer(const FText& Reason, FText& OutReason) const
{
	OutReason = Reason;
	UE_LOG(LogMoving_Castle, Error, TEXT("商店空岛 %s，配置 %s：%s"),
		*GetPathName(), *ConfigAsset.ToString(), *Reason.ToString());
	return false;
}

bool ACastleShopSkyIsland::CreateOffer(UCastleRoomGroupDataAsset* RoomGroup, UCastleBadgeDataAsset* Badge,
	const int32 RewardSeed, FText& OutReason)
{
	// 台座排在入口两旁，中心始终留给去离开门的飞行路径。文本独立挂在根上以免随网格缩放。
	const int32 Index = Offers.Num();
	const bool bLeftSide = Index % 2 == 0;
	const FVector Location(-330.0 + 320.0 * (Index / 2), bLeftSide ? -570.0 : 570.0, RoomGroup ? 0.0 : 100.0);
	const FName OfferName(*FString::Printf(TEXT("Offer%d"), Index + 1));
	auto* Offer = NewObject<UCastleShopSkyIslandOffer>(this, OfferName);
	AddInstanceComponent(Offer);
	Offer->SetupAttachment(GetRootComponent());
	Offer->SetRelativeLocation(Location);
	if (RoomGroup)
	{
		// 导入后印字面朝局部 +Y。左侧保持原向，右侧绕 Z 转半圈，包底轴心贴地。
		Offer->SetStaticMesh(Config->RoomPackMesh);
		Offer->SetRelativeRotation(FRotator(0.0, bLeftSide ? 0.0 : 180.0, 0.0));
		Offer->SetRelativeScale3D(FVector(RoomPackUniformScale));
		Offer->SetCanEverAffectNavigation(false);
	}
	else
	{
		Offer->SetStaticMesh(BadgeMesh);
		Offer->SetRelativeScale3D(FVector(1.6, 1.6, 2.0));
	}
	Offer->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Offer->SetCollisionResponseToAllChannels(ECR_Ignore);
	Offer->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Offer->RoomGroup = RoomGroup;
	Offer->Badge = Badge;
	Offer->RewardSeed = RewardSeed;
	Offer->Price = RoomGroup ? Config->DefaultRoomGroupPrice : Config->DefaultBadgePrice;
	Offer->ProductName = RoomGroup ? RoomGroup->DisplayName : Badge->Badge.GetDisplayName();
	// 包装名用资产身份，中文交互标题仍使用配置好的 DisplayName。
	const FString TypeName = RoomGroup ? RoomGroup->GetName()
		: StaticEnum<ECastleBadgeType>()->GetNameStringByValue(static_cast<int64>(Badge->Badge.Type));
	Offer->DisplayLabel = FName::NameToDisplayString(TypeName, false);
	UMaterialInterface* Material = RoomGroup ? Config->RoomPackMaterials.FindChecked(RoomGroup->Rarity).Get() : BadgeMaterial.Get();
	Offer->OfferMaterialInstance = Offer->CreateDynamicMaterialInstance(0, Material);
	if (!Offer->OfferMaterialInstance)
	{
		return RejectOffer(FText::Format(INVTEXT("商品 {0} 无法从材质 {1} 创建动态材质。"),
			FText::FromString(GetPathNameSafe(RoomGroup ? static_cast<UObject*>(RoomGroup) : Badge)),
			FText::FromString(GetPathNameSafe(Material))), OutReason);
	}
	if (Badge)
	{
		Offer->OfferMaterialInstance->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.98f, 0.53f, 0.13f));
	}
	Offer->RegisterComponent();

	const FName SignName(*FString::Printf(TEXT("OfferSign%d"), Index + 1));
	Offer->Sign = NewObject<UTextRenderComponent>(this, SignName);
	AddInstanceComponent(Offer->Sign);
	Offer->Sign->SetupAttachment(GetRootComponent());
	Offer->Sign->SetRelativeLocation(FVector(Location.X - 86.0, Location.Y, 260.0));
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
		if (!Economy->TrySpendGold(Offer.Price, OutReason)) { return false; }
		if (!GrantBadgeWithReplaceUI(Offer.Badge->Badge, OutReason))
		{
			RefundFailedOffer(Offer, OutReason);
			return false;
		}
		// 满六枚时，待替换状态同样表示发放成功。替换或舍弃由徽章界面完成。
		CompleteSale(Offer);
		return true;
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

	if (!Economy->TrySpendGold(Offer.Price, OutReason)) { return false; }
	if (!UCastleItemGrantLibrary::TryCommitPreparedRoomGroup(Prepared, Backpack, OutReason))
	{
		RefundFailedOffer(Offer, OutReason);
		return false;
	}
	CompleteSale(Offer);
	return true;
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
