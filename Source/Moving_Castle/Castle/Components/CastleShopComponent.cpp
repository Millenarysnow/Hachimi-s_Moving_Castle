// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleShopComponent.h"
#include "Audio/CastleAudioSubsystem.h"

#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleCardPricing.h"
#include "CastleEconomyComponent.h"
#include "Engine/World.h"
#include "Items/CastleBadgeRuntimeSubsystem.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Moving_Castle.h"
#include "UObject/StrongObjectPtr.h"

UCastleShopComponent::UCastleShopComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PricingStrategyClass = UCastleSimpleCardPricing::StaticClass();
}

bool UCastleShopComponent::InitializeShop(UCastleEconomyComponent* InEconomy, ACastleBoard* InBoard, FText& OutFailureReason)
{
	if (!InitializeShopState(InEconomy, InBoard, OutFailureReason)) { return false; }
	// 成功绑定的正式商店即使关闭，棋盘仍执行该房间容量。
	InBoard->BoundShops.Add(this);
	return true;
}

bool UCastleShopComponent::InitializeShopState(UCastleEconomyComponent* InEconomy, ACastleBoard* InBoard, FText& OutFailureReason)
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
	if (RefreshCost < 0 || RoomLimit < 1)
	{
		OutFailureReason = FText::FromString(TEXT("商店初始化失败：刷新费用不能为负，总房间上限必须为正。"));
		return false;
	}
	if (!InBoard->ValidateBuildingDefinitions(OutFailureReason))
	{
		return false;
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

int32 UCastleShopComponent::GetBuyPrice(const FCastleShopOffer& Offer) const
{
	// 重新开店也使用当前已入槽的徽章；商品、售罄和定价策略的原始报价按原规则保留。
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(IsValid(Board) ? Board->GetWorld() : nullptr);
	return !bScriptedStock && Badges ? Badges->GetDiscountedShopBuyPrice(Offer.Price) : Offer.Price;
}

TArray<FCastleShopOffer> UCastleShopComponent::GetOffers() const
{
	TArray<FCastleShopOffer> QuotedOffers = Offers;
	for (FCastleShopOffer& Offer : QuotedOffers) { Offer.Price = GetBuyPrice(Offer); }
	return QuotedOffers;
}

bool UCastleShopComponent::GenerateRandomRoom(FRandomStream& Stream, const int32 MinNumber, const int32 MaxNumber,
	const TOptional<int32> ForcedNumber, FCastleBuildingDescriptor& OutCard, FText& OutFailureReason) const
{
	const TConstArrayView<ECastleTileColor> Colors = CastleTileColors::GetPlayable();
	const auto* Definition = Board->BuildingDefinitions[Stream.RandRange(0, Board->BuildingDefinitions.Num() - 1)].Get();
	const ECastleTileColor Color = Colors[Stream.RandRange(0, Colors.Num() - 1)];
	// 首格数字由采购签指定时，仍按正常顺序推进当次随机流，后五格继续使用独立抽样。
	const int32 RandomNumber = Stream.RandRange(MinNumber, MaxNumber);
	if (!Definition->CreateDescriptor(1, Color, ForcedNumber.Get(RandomNumber), OutCard, OutFailureReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店 %s 无法生成房间卡 %s：%s。"),
			*GetPathName(), *Definition->GetPathName(), *OutFailureReason.ToString());
		return false;
	}
	return true;
}

bool UCastleShopComponent::GenerateOffers(FRandomStream& Stream, TArray<FCastleShopOffer>& OutOffers, FText& OutFailureReason,
	const TOptional<int32> FirstNumber) const
{
	OutOffers.Reset();
	OutFailureReason = FText::GetEmpty();
	if (!IsValid(Board) || !Board->ValidateBuildingDefinitions(OutFailureReason))
	{
		if (OutFailureReason.IsEmpty())
		{
			OutFailureReason = FText::FromString(TEXT("商店生成失败：没有绑定有效棋盘。"));
		}
		return false;
	}
	for (int32 Index = 0; Index < 6; ++Index)
	{
		FCastleShopOffer Offer;
		if (!GenerateRandomRoom(Stream, MinOfferNumber, MaxOfferNumber, Index == 0 ? FirstNumber : TOptional<int32>(),
			Offer.Descriptor, OutFailureReason))
		{
			return false;
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
	if (!bScriptedStock && Offers.IsEmpty())
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
		UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::ClosePanel);
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

bool UCastleShopComponent::TryResolveTradeBadgeRuntime(UCastleBadgeRuntimeSubsystem*& OutRuntime, FText& OutFailureReason) const
{
	OutRuntime = nullptr;
	const UWorld* World = Board->GetWorld();
	// 固定教学货架和独立数据商店的既有入口可以在单局之外运行，自身不拥有徽章计数。
	if (bScriptedStock || !World || !World->IsGameWorld()) { return true; }
	OutRuntime = World->GetSubsystem<UCastleBadgeRuntimeSubsystem>();
	if (OutRuntime) { return true; }
	OutFailureReason = INVTEXT("普通商店交易需要本局徽章子系统。");
	UE_LOG(LogMoving_Castle, Error, TEXT("商店 %s 交易失败：%s"), *GetPathName(), *OutFailureReason.ToString());
	return false;
}

bool UCastleShopComponent::TryRefreshOffers(FText& OutFailureReason)
{
	return RefreshOffersInternal(false, OutFailureReason);
}

bool UCastleShopComponent::TryRefreshOffersForReward(FText& OutFailureReason)
{
	return RefreshOffersInternal(true, OutFailureReason);
}

bool UCastleShopComponent::SetScriptedOffers(TConstArrayView<FCastleBuildingDescriptor> Current,
	TConstArrayView<FCastleBuildingDescriptor> OnRefresh, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const auto Reject = [&]()
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店 %s 教学货架配置失败：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	};
	if (!bInitialized || bOpen || bTrading || Current.Num() > 6 || OnRefresh.Num() > 6)
	{
		OutReason = INVTEXT("商店须在初始化后、闭店且无交易时更换教学货架，每批最多六张牌。");
		return Reject();
	}
	const auto Prepare = [&](TConstArrayView<FCastleBuildingDescriptor> Source, TArray<FCastleShopOffer>& Target)
	{
		for (const FCastleBuildingDescriptor& Card : Source)
		{
			if (!Card.Validate(OutReason)) { return false; }
			if (Card.Level != 1 || !Board->BuildingDefinitions.Contains(Card.Definition))
			{
				OutReason = INVTEXT("教学商品必须是棋盘已登记的一星建筑。");
				return false;
			}
			FCastleShopOffer& Offer = Target.AddDefaulted_GetRef();
			Offer.Descriptor = Card;
			if (!CalculatePrice(Card, false, Offer.Price, OutReason)) { return false; }
		}
		return true;
	};
	// 两批都完成验证和报价后才替换现有货架。
	TArray<FCastleShopOffer> CurrentOffers, RefreshOffers;
	if (!Prepare(Current, CurrentOffers) || !Prepare(OnRefresh, RefreshOffers)) { return Reject(); }
	Offers = MoveTemp(CurrentOffers);
	ScriptedRefreshOffers = MoveTemp(RefreshOffers);
	bScriptedStock = true;
	bScriptedRefreshPending = !OnRefresh.IsEmpty();
	OnShopChanged.Broadcast();
	return true;
}

bool UCastleShopComponent::TryOpenForReward(UCastleEconomyComponent* InEconomy, ACastleBoard* InBoard, FText& OutFailureReason)
{
	TStrongObjectPtr<UCastleShopComponent> Candidate(NewObject<UCastleShopComponent>());
	if (!PrepareRewardShop(InEconomy, InBoard, MaxOfferNumber, *Candidate, OutFailureReason)) { return false; }
	CommitRewardShop(*Candidate);
	return true;
}

bool UCastleShopComponent::PrepareRewardShop(UCastleEconomyComponent* InEconomy, ACastleBoard* InBoard,
	const int32 CandidateMaxNumber, UCastleShopComponent& Candidate, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (bTrading || bOpen)
	{
		OutFailureReason = FText::FromString(TEXT("商店奖励失败：商店正在交易或已经打开。"));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s：%s"), *GetPathName(), *OutFailureReason.ToString());
		return false;
	}
	if (bInitialized && (Economy != InEconomy || Board != InBoard))
	{
		OutFailureReason = FText::FromString(TEXT("商店奖励失败：已初始化商店不能更换账户或棋盘。"));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s：%s"), *GetPathName(), *OutFailureReason.ToString());
		return false;
	}
	// 候选组件没有表现层监听者；初始化或出牌失败不会污染正式商店的任何状态。
	TGuardValue<bool> Guard(bTrading, true);
	if (!Candidate.TrySetOfferNumberRange(MinOfferNumber, CandidateMaxNumber, OutFailureReason)) { return false; }
	Candidate.PricingStrategyClass = PricingStrategyClass;
	Candidate.RandomSeed = RandomSeed;
	Candidate.RefreshCost = RefreshCost;
	Candidate.RoomLimit = RoomLimit;
	if (bInitialized)
	{
		Candidate.Economy = Economy;
		Candidate.Board = Board;
		Candidate.Backpack = Backpack;
		Candidate.PricingStrategy = PricingStrategy;
		Candidate.RandomStream = RandomStream;
		Candidate.bInitialized = true;
	}
	else if (!Candidate.InitializeShopState(InEconomy, InBoard, OutFailureReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("%s 商店奖励初始化失败：%s"), *GetPathName(), *OutFailureReason.ToString());
		return false;
	}
	return Candidate.TryRefreshOffersForReward(OutFailureReason);
}

void UCastleShopComponent::CommitRewardShop(UCastleShopComponent& Candidate)
{
	// 广播前写完所有在免费换批中共同提交的商店字段。
	Economy = Candidate.Economy;
	Board = Candidate.Board;
	// 只有正式提交才能让新商店约束棋盘，临时换批对象从不登记。
	Board->BoundShops.AddUnique(this);
	Backpack = Candidate.Backpack;
	PricingStrategy = Candidate.PricingStrategy;
	Offers = MoveTemp(Candidate.Offers);
	RandomStream = Candidate.RandomStream;
	bInitialized = true;
	bOpen = true;
	OnShopChanged.Broadcast();
}

bool UCastleShopComponent::TrySetRoomLimit(const int32 NewRoomLimit, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (NewRoomLimit < 1)
	{
		OutFailureReason = FText::FromString(TEXT("房间上限设置失败：总房间上限必须为正数。"));
		UE_LOG(LogMoving_Castle, Error, TEXT("房间上限设置失败：收到无效值 %d，组件 %s。"), NewRoomLimit, *GetPathName());
		return false;
	}
	RoomLimit = NewRoomLimit;
	return true;
}

bool UCastleShopComponent::TrySetOfferNumberRange(const int32 MinNumber, const int32 MaxNumber, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (MinNumber < 1 || MinNumber > MaxNumber || MaxNumber > 9)
	{
		OutReason = INVTEXT("商品数字区间必须满足 1 ≤ 最小值 ≤ 最大值 ≤ 9。");
		UE_LOG(LogMoving_Castle, Error, TEXT("商店 %s 拒绝商品数字区间 [%d, %d]。"), *GetPathName(), MinNumber, MaxNumber);
		return false;
	}
	MinOfferNumber = MinNumber;
	MaxOfferNumber = MaxNumber;
	return true;
}

void UCastleShopComponent::CommitValidatedTowerGrowth(const int32 NewRoomLimit, const int32 NewMaxNumber)
{
	// 成长表的校验条件覆盖两个公开设置入口。这里只提交上限，已生成的商品仍保留原样。
	check(NewRoomLimit > 0 && NewMaxNumber >= 1 && NewMaxNumber <= 9);
	RoomLimit = NewRoomLimit;
	MinOfferNumber = 1;
	MaxOfferNumber = NewMaxNumber;
}

bool UCastleShopComponent::RefreshOffersInternal(const bool bRewardRefresh, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	// 奖励在界面打开前准备商品；付费刷新仍严格要求商店已打开。
	if (bRewardRefresh && (!bInitialized || bTrading || !IsValid(Economy) || !IsValid(Board) || !IsValid(Backpack)))
	{
		OutFailureReason = FText::FromString(TEXT("奖励换批失败：商店未初始化、依赖无效或正在交易。"));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s：%s"), *GetPathName(), *OutFailureReason.ToString());
		return false;
	}
	if (!bRewardRefresh && !ValidateTrade(OutFailureReason))
	{
		return false;
	}
	UCastleBadgeRuntimeSubsystem* Runtime = nullptr;
	if (!bRewardRefresh && !TryResolveTradeBadgeRuntime(Runtime, OutFailureReason)) { return false; }
	const bool bUseBadgeRefresh = Runtime && RefreshCost > 0 && Runtime->GetRemainingFreeShopRefreshes() > 0;
	const int32 Price = (bRewardRefresh || bUseBadgeRefresh) ? 0 : RefreshCost;
	if (!Economy->CanSpendGold(Price, OutFailureReason)) { return false; }

	TGuardValue<bool> TradeGuard(bTrading, true);
	FRandomStream CandidateStream = RandomStream;
	TArray<FCastleShopOffer> Candidates;
	TOptional<int32> FirstNumber;
	const bool bMatchFirst = Runtime && Runtime->IsMatchingShopRefreshDue();
	if (bMatchFirst && Backpack->GetItemCount() > 0)
	{
		FCastleBuildingDescriptor FirstCard;
		if (!Backpack->TryGetItemAt(0, FirstCard) || !FirstCard.Validate(OutFailureReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店 %s 无法匹配背包 %s 首张牌 %s：%s。"),
				*GetPathName(), *Backpack->GetPathName(), *GetPathNameSafe(FirstCard.Definition.Get()), *OutFailureReason.ToString());
			return false;
		}
		FirstNumber = FirstCard.Number;
	}
	if (bScriptedStock)
	{
		if (!bScriptedRefreshPending)
		{
			OutFailureReason = INVTEXT("当前教学货架没有安排下一批商品。");
			return false;
		}
		Candidates = ScriptedRefreshOffers;
	}
	else if (!GenerateOffers(CandidateStream, Candidates, OutFailureReason, FirstNumber))
	{
		// 奖励路径仍沿用调用方可识别的原日志；普通刷新只额外记录徽章结算相关错误。
		if (bRewardRefresh)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("%s 奖励换批失败：%s"), *GetPathName(), *OutFailureReason.ToString());
		}
		else if (bUseBadgeRefresh || bMatchFirst)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("商店 %s 换批失败：%s"), *GetPathName(), *OutFailureReason.ToString());
		}
		return false;
	}
	// 商店状态及徽章计数一起提交，余额通知观察者随后才能读取完整结果。
	if (!Economy->TrySpendShopGold(Price, [&]()
	{
		Offers = MoveTemp(Candidates);
		RandomStream = CandidateStream;
		if (bScriptedStock) { bScriptedRefreshPending = false; }
		if (Runtime) { Runtime->RecordShopRefresh(bUseBadgeRefresh); }
		return true;
	}, OutFailureReason))
	{
		return false;
	}
	if (bMatchFirst)
	{
		if (FirstNumber.IsSet())
		{
			UE_LOG(LogMoving_Castle, Display, TEXT("环路采购签：本次商店首格匹配主建筑栏数字 %d。"), FirstNumber.GetValue());
		}
		else
		{
			// D12 对空主栏有明确规则，仍生成正常货架并告知本次为何没有锁定数字。
			UE_LOG(LogMoving_Castle, Warning, TEXT("环路采购签：背包 %s 主建筑栏为空，本次首格按普通规则随机。"),
				*Backpack->GetPathName());
		}
	}
	// 新货架已经原子提交，刷新拒绝或预备奖励商店时不提示翻页。
	if (!bRewardRefresh) { UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::PageTurn); }
	OnShopChanged.Broadcast();
	return true;
}

int32 UCastleShopComponent::GetOwnedRoomCount() const
{
	// 未绑定棋盘的商店沿用公开查询原有的空状态；导演开局直接从自己的棋盘读取。
	if (!bInitialized) { return 0; }
	int32 Cells = INDEX_NONE;
	FText Reason;
	TryCountOwnedRoomCells(Board, Cells, Reason);
	return Cells;
}

bool UCastleShopComponent::TryCountOwnedRoomCells(const ACastleBoard* InBoard, int32& OutCells, FText& OutReason)
{
	OutCells = INDEX_NONE;
	OutReason = FText::GetEmpty();
	if (!IsValid(InBoard))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("棋盘 %s 无效，无法统计场内占格。"), *GetPathNameSafe(InBoard)));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	// 占用索引已逐格登记多格房间。主建筑栏与临时栏只影响库存。
	OutCells = InBoard->GetOccupiedRoomCellCount();
	return true;
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
	const int32 OwnedCells = GetOwnedRoomCount();
	if (OwnedCells == INDEX_NONE)
	{
		OutFailureReason = INVTEXT("房间总量读取失败，无法购买，详见错误日志。");
		return false;
	}
	if (RoomLimit < 1 || OwnedCells >= RoomLimit)
	{
		OutFailureReason = FText::FromString(TEXT("房间总量已达到上限，无法继续购买。"));
		return false;
	}
	const int32 Price = GetBuyPrice(Offers[OfferIndex]);
	if (!Economy->CanSpendGold(Price, OutFailureReason))
	{
		return false;
	}
	TGuardValue<bool> TradeGuard(bTrading, true);
	UCastleBadgeRuntimeSubsystem* Runtime = nullptr;
	if (!TryResolveTradeBadgeRuntime(Runtime, OutFailureReason)) { return false; }
	const int64 RebateCount = Runtime ? Runtime->GetNextShopPurchaseRebate() : 0;
	FRandomStream CandidateStream = RandomStream;
	TArray<FCastleBuildingDescriptor> RebateCards;
	if (RebateCount > 0)
	{
		if (!Board->ValidateBuildingDefinitions(OutFailureReason) || RebateCount > MAX_int32)
		{
			if (OutFailureReason.IsEmpty()) { OutFailureReason = INVTEXT("商店返利卡牌数量超出背包数组容量。"); }
			UE_LOG(LogMoving_Castle, Error, TEXT("商店 %s 无法准备返利：%s"), *GetPathName(), *OutFailureReason.ToString());
			return false;
		}
		for (int64 Index = 0; Index < RebateCount; ++Index)
		{
			if (!GenerateRandomRoom(CandidateStream, MinOfferNumber, MaxOfferNumber, {}, RebateCards.AddDefaulted_GetRef(), OutFailureReason))
			{
				return false;
			}
		}
	}
	int32 AddedIndex = INDEX_NONE;
	if (!Backpack->TryAddItem(Offers[OfferIndex].Descriptor, AddedIndex, OutFailureReason))
	{
		return false;
	}
	Offers[OfferIndex].bSold = true;
	if (!Economy->TrySpendShopGold(Price, [&]()
	{
		if (!RebateCards.IsEmpty() && !Backpack->TryAddItemsAllowOverflow(RebateCards, OutFailureReason)) { return false; }
		if (Runtime) { Runtime->RecordShopPurchase(); }
		RandomStream = CandidateStream;
		return true;
	}, OutFailureReason))
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
	if (!RebateCards.IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("商店返利：本次购买额外发放 %d 张随机房间卡。"), RebateCards.Num());
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
	// 固定货架只包含教学所需物品，保留回路所需的普通房卡。
	if (UsesScriptedOffers())
	{
		OutFailureReason = INVTEXT("新手教程期间暂不可出售房间卡");
		return false;
	}
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
	// 金币已入账，暂取的空槽现在可以由临时栏队首补位。
	Backpack->CommitAutoRefillFromTempBar();
	OnShopChanged.Broadcast();
	return true;
}
