// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleShopComponent.h"

#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildings.h"
#include "CastleCardPricing.h"
#include "CastleEconomyComponent.h"
#include "Moving_Castle.h"

UCastleShopComponent::UCastleShopComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PricingStrategyClass = UCastleSimpleCardPricing::StaticClass();
	OfferBuildingClasses = { ACastleRoadBuilding::StaticClass(), ACastleArcherTowerLevel1::StaticClass(),
		ACastleCannonTowerLevel1::StaticClass(), ACastleFarmLevel1::StaticClass() };
}

bool UCastleShopComponent::InitializeShop(UCastleEconomyComponent* InEconomy, ACastleBoard* InBoard, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (bInitialized || !IsValid(InEconomy) || !IsValid(InBoard) || !IsValid(InBoard->PrototypeBackpack))
	{
		OutFailureReason = FText::FromString(TEXT("商店初始化失败：已经初始化，或缺少账户、棋盘、背包。"));
		return false;
	}
	if (PricingStrategyClass == nullptr || PricingStrategyClass->HasAnyClassFlags(CLASS_Abstract)
		|| !PricingStrategyClass->ImplementsInterface(UCastleCardPricing::StaticClass()))
	{
		OutFailureReason = FText::FromString(TEXT("商店初始化失败：定价类必须实现 CastleCardPricing 接口且可实例化。"));
		return false;
	}
	if (OfferBuildingClasses.IsEmpty() || RefreshCost < 0 || RoomLimit < 1)
	{
		OutFailureReason = FText::FromString(TEXT("商店初始化失败：商品池不能为空，刷新费用不能为负，总房间上限必须为正。"));
		return false;
	}
	for (const TSubclassOf<ACastleBuildingBase>& BuildingClass : OfferBuildingClasses)
	{
		if (BuildingClass == nullptr || BuildingClass->HasAnyClassFlags(CLASS_Abstract)
			|| BuildingClass->GetDefaultObject<ACastleBuildingBase>()->GetBuildingLevel() != 1)
		{
			OutFailureReason = FText::FromString(TEXT("商店初始化失败：商品池必须全部使用可生成的一级建筑类。"));
			return false;
		}
	}
	PricingStrategy = NewObject<UObject>(this, PricingStrategyClass);
	if (PricingStrategy == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("商店初始化失败：无法创建定价策略对象。"));
		return false;
	}
	Economy = InEconomy;
	Board = InBoard;
	Backpack = InBoard->PrototypeBackpack;
	RandomStream.Initialize(RandomSeed);
	bInitialized = true;
	return true;
}

bool UCastleShopComponent::CalculatePrice(const FCastleBuildingDescriptor& Descriptor, bool bSelling, int32& OutPrice, FText& OutFailureReason) const
{
	OutPrice = 0;
	OutFailureReason = FText::GetEmpty();
	if (!IsValid(PricingStrategy))
	{
		OutFailureReason = FText::FromString(TEXT("商店没有有效的定价策略。"));
		return false;
	}
	if (!ICastleCardPricing::Execute_TryCalculatePrice(PricingStrategy, Descriptor,
		bSelling ? ECastleCardPriceKind::Sell : ECastleCardPriceKind::Buy, OutPrice, OutFailureReason))
	{
		if (OutFailureReason.IsEmpty())
		{
			OutFailureReason = FText::FromString(TEXT("定价策略拒绝报价，且未提供失败原因。"));
		}
		return false;
	}
	if (OutPrice < 0)
	{
		OutFailureReason = FText::FromString(TEXT("定价策略返回了负数价格，交易已拒绝。"));
		return false;
	}
	return true;
}

bool UCastleShopComponent::GenerateOffers(FRandomStream& Stream, TArray<FCastleShopOffer>& OutOffers, FText& OutFailureReason) const
{
	OutOffers.Reset();
	OutFailureReason = FText::GetEmpty();
	if (OfferBuildingClasses.IsEmpty())
	{
		OutFailureReason = FText::FromString(TEXT("商店生成失败：商品池为空。"));
		return false;
	}
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const TSubclassOf<ACastleBuildingBase> BuildingClass = OfferBuildingClasses[Stream.RandRange(0, OfferBuildingClasses.Num() - 1)];
		if (BuildingClass == nullptr || BuildingClass->HasAnyClassFlags(CLASS_Abstract))
		{
			OutFailureReason = FText::FromString(TEXT("商店生成失败：商品池包含无效建筑类。"));
			return false;
		}
		const ACastleBuildingBase* Defaults = BuildingClass->GetDefaultObject<ACastleBuildingBase>();
		FCastleShopOffer Offer;
		Offer.Descriptor.BuildingClass = BuildingClass;
		Offer.Descriptor.BuildingType = Defaults->GetBuildingType();
		Offer.Descriptor.Level = Defaults->GetBuildingLevel();
		if (Offer.Descriptor.BuildingType != ECastleBuildingType::Road)
		{
			// 有放回抽样：允许一轮出现多张同类牌，颜色覆盖五种功能颜色，数字为 1 至 9。
			Offer.Descriptor.Color = static_cast<ECastleTileColor>(Stream.RandRange(
				static_cast<int32>(ECastleTileColor::Red), static_cast<int32>(ECastleTileColor::Purple)));
			Offer.Descriptor.Number = Stream.RandRange(1, 9);
		}
		if (Offer.Descriptor.Level != 1 || !CalculatePrice(Offer.Descriptor, false, Offer.Price, OutFailureReason))
		{
			if (OutFailureReason.IsEmpty())
			{
				OutFailureReason = FText::FromString(TEXT("商店商品池目前只允许一级建筑。"));
			}
			return false;
		}
		OutOffers.Add(Offer);
	}
	return true;
}

bool UCastleShopComponent::TryOpenShop(FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (!bInitialized || bTrading || !IsValid(Economy) || !IsValid(Board) || !IsValid(Backpack))
	{
		OutFailureReason = FText::FromString(TEXT("无法打开商店：尚未初始化、依赖已失效或正在交易。"));
		return false;
	}
	if (bOpen)
	{
		return true;
	}
	TGuardValue<bool> TradeGuard(bTrading, true);
	if (Offers.IsEmpty())
	{
		FRandomStream CandidateStream = RandomStream;
		TArray<FCastleShopOffer> Candidates;
		if (!GenerateOffers(CandidateStream, Candidates, OutFailureReason))
		{
			return false;
		}
		Offers = MoveTemp(Candidates);
		RandomStream = CandidateStream;
	}
	bOpen = true;
	OnShopChanged.Broadcast();
	return true;
}

void UCastleShopComponent::CloseShop()
{
	if (bOpen && !bTrading)
	{
		bOpen = false;
		OnShopChanged.Broadcast();
	}
}

bool UCastleShopComponent::ValidateTrade(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (!bInitialized || !bOpen || bTrading || !IsValid(Economy) || !IsValid(Board) || !IsValid(Backpack))
	{
		OutFailureReason = FText::FromString(TEXT("交易失败：商店未打开、依赖无效或正在处理另一笔交易。"));
		return false;
	}
	return true;
}

bool UCastleShopComponent::TryRefreshOffers(FText& OutFailureReason)
{
	if (!ValidateTrade(OutFailureReason) || !Economy->CanSpendGold(RefreshCost, OutFailureReason))
	{
		return false;
	}
	TGuardValue<bool> TradeGuard(bTrading, true);
	FRandomStream CandidateStream = RandomStream;
	TArray<FCastleShopOffer> Candidates;
	if (!GenerateOffers(CandidateStream, Candidates, OutFailureReason))
	{
		return false;
	}
	// 先暂存完整商品再扣费，使余额通知的观察者看见的是同一笔交易的最终状态。
	TArray<FCastleShopOffer> PreviousOffers = MoveTemp(Offers);
	Offers = MoveTemp(Candidates);
	if (!Economy->TrySpendGold(RefreshCost, OutFailureReason))
	{
		Offers = MoveTemp(PreviousOffers);
		return false;
	}
	RandomStream = CandidateStream;
	OnShopChanged.Broadcast();
	return true;
}

int32 UCastleShopComponent::GetOwnedRoomCount() const
{
	if (!IsValid(Board) || !IsValid(Backpack))
	{
		return 0;
	}
	int32 Count = Board->GetOccupiedRoomCellCount();
	for (const FCastleBuildingDescriptor& Item : Backpack->GetItems())
	{
		Count += ACastleBuildingBase::GetExpectedFootprintCellCount(Item.Level);
	}
	return Count;
}

TArray<FCastleBuildingDescriptor> UCastleShopComponent::GetBackpackItems() const
{
	return IsValid(Backpack) ? Backpack->GetItems() : TArray<FCastleBuildingDescriptor>();
}

bool UCastleShopComponent::TryBuyOffer(int32 OfferIndex, FText& OutFailureReason)
{
	if (!ValidateTrade(OutFailureReason))
	{
		return false;
	}
	if (!Offers.IsValidIndex(OfferIndex) || Offers[OfferIndex].bSold)
	{
		OutFailureReason = FText::FromString(TEXT("该商品槽位无效或已经售罄，请刷新商店。"));
		return false;
	}
	if (RoomLimit < 1 || GetOwnedRoomCount() + 1 > RoomLimit)
	{
		OutFailureReason = FText::FromString(TEXT("房间总量已达到上限，无法继续购买。"));
		return false;
	}
	if (!Economy->CanSpendGold(Offers[OfferIndex].Price, OutFailureReason))
	{
		return false;
	}
	TGuardValue<bool> TradeGuard(bTrading, true);
	int32 AddedIndex = INDEX_NONE;
	if (!Backpack->TryAddItem(Offers[OfferIndex].Descriptor, AddedIndex, OutFailureReason))
	{
		return false;
	}
	Offers[OfferIndex].bSold = true;
	if (!Economy->TrySpendGold(Offers[OfferIndex].Price, OutFailureReason))
	{
		// 此处只处理不可预期的提交失败；回滚必须显式校验，绝不能留下免费卡牌。
		Offers[OfferIndex].bSold = false;
		FCastleBuildingDescriptor Removed;
		FText RollbackReason;
		if (!Backpack->TryRemoveItemAt(AddedIndex, Removed, RollbackReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店购买回滚失败：%s"), *RollbackReason.ToString());
		}
		return false;
	}
	OnShopChanged.Broadcast();
	return true;
}

bool UCastleShopComponent::TryGetSellPrice(int32 BackpackIndex, int32& OutPrice, FText& OutFailureReason) const
{
	OutPrice = 0;
	FCastleBuildingDescriptor Descriptor;
	if (!IsValid(Backpack) || !Backpack->TryGetItemAt(BackpackIndex, Descriptor))
	{
		OutFailureReason = FText::FromString(TEXT("出售报价失败：背包槽位无效。"));
		return false;
	}
	return CalculatePrice(Descriptor, true, OutPrice, OutFailureReason);
}

bool UCastleShopComponent::TrySellBackpackItem(int32 BackpackIndex, FText& OutFailureReason)
{
	int32 Price = 0;
	if (!ValidateTrade(OutFailureReason) || !TryGetSellPrice(BackpackIndex, Price, OutFailureReason)
		|| !Economy->CanAddGold(Price, OutFailureReason))
	{
		return false;
	}
	TGuardValue<bool> TradeGuard(bTrading, true);
	FCastleBuildingDescriptor Removed;
	if (!Backpack->TryRemoveItemAt(BackpackIndex, Removed, OutFailureReason))
	{
		return false;
	}
	if (!Economy->TryAddGold(Price, OutFailureReason))
	{
		FText RollbackReason;
		if (!Backpack->TryInsertItemAt(BackpackIndex, Removed, RollbackReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店出售回滚失败：%s"), *RollbackReason.ToString());
		}
		return false;
	}
	OnShopChanged.Broadcast();
	return true;
}
