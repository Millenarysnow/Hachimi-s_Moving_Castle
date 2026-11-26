#include "Items/CastleBadgeRuntimeSubsystem.h"

#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastleEconomyComponent.h"
#include "Engine/World.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Moving_Castle.h"

namespace
{
	/** 采购签和返利按两种独立的交易计数结算，使用相同的十次周期。 */
	constexpr int32 ShopCycle = 10;
	constexpr int32 FreeRefreshesPerEditingPhase = 3;
}

bool UCastleBadgeRuntimeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// 世界切换时随单局销毁，跨地图持有的徽章继续归 GameInstance 子系统。
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCastleBadgeRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UCastleDirectorSubsystem* Director = Collection.InitializeDependency<UCastleDirectorSubsystem>();
	RunStartedHandle = Director->OnRunStarted.AddUObject(this, &UCastleBadgeRuntimeSubsystem::HandleRunStarted);
	EditingEnteredHandle = Director->OnEditingPhaseEntered.AddUObject(this, &UCastleBadgeRuntimeSubsystem::HandleEditingPhaseEntered);
}

void UCastleBadgeRuntimeSubsystem::Deinitialize()
{
	if (UCastleDirectorSubsystem* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>())
	{
		Director->OnRunStarted.Remove(RunStartedHandle);
		Director->OnEditingPhaseEntered.Remove(EditingEnteredHandle);
	}
	Super::Deinitialize();
}

void UCastleBadgeRuntimeSubsystem::HandleRunStarted()
{
	ShopPurchases = ShopRefreshes = FreeShopRefreshes = 0;
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	const int64 StartingGold = Badges ? Badges->GetStartingFundsGold() : 0;
	if (StartingGold == 0) { return; }
	const auto* Controller = Cast<ACastlePlayerController>(GetWorld()->GetFirstPlayerController());
	UCastleEconomyComponent* Economy = Controller ? Controller->GetEconomy() : nullptr;
	FText Reason;
	if (!Economy || StartingGold > MAX_int32)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("开局 %s 无法发放充足的启动资金：金币账户 %s 无效或奖励 %lld 超出整数范围。"),
			*GetPathNameSafe(GetWorld()), *GetPathNameSafe(Economy), StartingGold);
		return;
	}
	// 既有开局金币由控制器发放，启动资金是同一次新开局的独立入账，不经过玩法奖励倍率。
	if (!Economy->TryAddGold(static_cast<int32>(StartingGold), Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("金币账户 %s 发放充足的启动资金失败：%s。"),
			*Economy->GetPathName(), *Reason.ToString());
		return;
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("充足的启动资金：开局额外发放 %lld 金。"), StartingGold);
}

void UCastleBadgeRuntimeSubsystem::HandleEditingPhaseEntered()
{
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	FreeShopRefreshes = Badges && Badges->HasBadge(ECastleBadgeType::FreeRefresh) ? FreeRefreshesPerEditingPhase : 0;
	if (FreeShopRefreshes > 0)
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("翻货免单章：进入编辑阶段，获得 %d 次免费刷新。"), FreeShopRefreshes);
	}
}

int32 UCastleBadgeRuntimeSubsystem::GetRemainingFreeShopRefreshes() const
{
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	return Badges && Badges->HasBadge(ECastleBadgeType::FreeRefresh) ? FreeShopRefreshes : 0;
}

bool UCastleBadgeRuntimeSubsystem::IsMatchingShopRefreshDue() const
{
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	return ShopRefreshes == ShopCycle - 1 && Badges && Badges->HasBadge(ECastleBadgeType::LoopPurchase);
}

void UCastleBadgeRuntimeSubsystem::RecordShopRefresh(const bool bUsedFreeRefresh)
{
	ShopRefreshes = (ShopRefreshes + 1) % ShopCycle;
	if (bUsedFreeRefresh)
	{
		check(FreeShopRefreshes > 0);
		--FreeShopRefreshes;
		UE_LOG(LogMoving_Castle, Display, TEXT("翻货免单章：本次刷新免费，剩余 %d 次。"), FreeShopRefreshes);
	}
}

int64 UCastleBadgeRuntimeSubsystem::GetNextShopPurchaseRebate() const
{
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	return ShopPurchases == ShopCycle - 1 && Badges ? Badges->GetShopRebateCards() : 0;
}

void UCastleBadgeRuntimeSubsystem::RecordShopPurchase()
{
	ShopPurchases = (ShopPurchases + 1) % ShopCycle;
}
