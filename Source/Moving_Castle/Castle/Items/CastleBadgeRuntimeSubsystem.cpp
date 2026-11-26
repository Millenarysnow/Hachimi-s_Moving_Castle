#include "Items/CastleBadgeRuntimeSubsystem.h"

#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleEconomyComponent.h"
#include "CastleMainTowerComponent.h"
#include "CastleNavigationSubsystem.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Moving_Castle.h"
#include "Subsystems/CastleShieldTowerSubsystem.h"
#include "Units/Navigation/CastleTowerDispatchSubsystem.h"
#include <cmath>

namespace
{
	/** 采购签和返利按两种独立的交易计数结算，使用相同的十次周期。 */
	constexpr int32 ShopCycle = 10;
	constexpr int32 FreeRefreshesPerEditingPhase = 3;
	constexpr int32 WhistleRecipientsPerWave = 3;
	constexpr double WhistleDuration = 4.0;
	constexpr double MarchDuration = 3.0;
	constexpr int32 LapsPerReassignment = 10;

	bool IsFunctionTower(const ACastleBuildingBase& Building)
	{
		const ECastleBuildingType Type = Building.GetBuildingType();
		return Type != ECastleBuildingType::NormalRoom;
	}
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
	UCastleCombatSubsystem* Combat = Collection.InitializeDependency<UCastleCombatSubsystem>();
	Collection.InitializeDependency<UCastleShieldTowerSubsystem>();
	RunStartedHandle = Director->OnRunStarted.AddUObject(this, &UCastleBadgeRuntimeSubsystem::HandleRunStarted);
	EditingEnteredHandle = Director->OnEditingPhaseEntered.AddUObject(this, &UCastleBadgeRuntimeSubsystem::HandleEditingPhaseEntered);
	BattleClearedHandle = Director->OnBattleClearedForItems.AddUObject(this, &UCastleBadgeRuntimeSubsystem::HandleBattleCleared);
	DirectorPhaseHandle = Director->OnDirectorPhaseChanged.AddUObject(this, &UCastleBadgeRuntimeSubsystem::HandleDirectorPhaseChanged);
	WaveStartedHandle = Combat->OnWaveStarted.AddUObject(this, &UCastleBadgeRuntimeSubsystem::HandleWaveStarted);
}

void UCastleBadgeRuntimeSubsystem::Deinitialize()
{
	if (UCastleDirectorSubsystem* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>())
	{
		Director->OnRunStarted.Remove(RunStartedHandle);
		Director->OnEditingPhaseEntered.Remove(EditingEnteredHandle);
		Director->OnBattleClearedForItems.Remove(BattleClearedHandle);
		Director->OnDirectorPhaseChanged.Remove(DirectorPhaseHandle);
	}
	if (UCastleCombatSubsystem* Combat = GetWorld()->GetSubsystem<UCastleCombatSubsystem>())
	{
		Combat->OnWaveStarted.Remove(WaveStartedHandle);
	}
	if (ACastleBoard* Board = BattleBoard.Get()) { Board->OnBuildingTriggered.Remove(BuildingTriggeredHandle); }
	Super::Deinitialize();
}

void UCastleBadgeRuntimeSubsystem::HandleRunStarted()
{
	ShopPurchases = ShopRefreshes = FreeShopRefreshes = 0;
	bLastBalloonClaspUsed = false;
	ResetBattleEffects();
	if (ACastleBoard* Board = BattleBoard.Get()) { Board->OnBuildingTriggered.Remove(BuildingTriggeredHandle); }
	BattleBoard.Reset();

	// 导演只在唯一棋盘和本局依赖全部准备好后发布开局；在同一棋盘订阅后续真实建筑触发。
	TActorIterator<ACastleBoard> Boards(GetWorld());
	ACastleBoard* RunBoard = Boards ? *Boards : nullptr;
	if (Boards) { ++Boards; }
	if (!RunBoard || Boards)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("世界 %s 的开局徽章无法绑定建筑事件：需要唯一棋盘。"), *GetPathNameSafe(GetWorld()));
		return;
	}
	BattleBoard = RunBoard;
	BuildingTriggeredHandle = RunBoard->OnBuildingTriggered.AddUObject(this, &UCastleBadgeRuntimeSubsystem::HandleBuildingTriggered);

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

void UCastleBadgeRuntimeSubsystem::ResetBattleEffects()
{
	bWaveActive = false;
	bFirstTowerEntryUsed = false;
	WhistleRecipients = 0;
	NPCsEnteredThisWave.Reset();
	NPCMovement.Reset();
	CompletedLapsInCycle = 0;
	EarnedTemporaryNPCs = 0;
}

void UCastleBadgeRuntimeSubsystem::HandleDirectorPhaseChanged(const ECastleDirectorPhase Phase)
{
	if (Phase != ECastleDirectorPhase::Battle) { ResetBattleEffects(); }
}

void UCastleBadgeRuntimeSubsystem::HandleWaveStarted(const int32 WaveIndex, const int32 TotalWaves)
{
	if (WaveIndex == 1) { ResetBattleEffects(); }
	// 战斗成功进入第一波和后续波次都由同一个委托发布，旧申请令牌不能归还新波机会。
	++WaveSerial;
	bWaveActive = true;
	bFirstTowerEntryUsed = false;
	WhistleRecipients = 0;
	NPCsEnteredThisWave.Reset();
}

void UCastleBadgeRuntimeSubsystem::RecordCompletedPatrolLaps(const int64 NewLaps)
{
	if (!bWaveActive || NewLaps == 0) { return; }
	check(NewLaps > 0);
	const int32 Combined = CompletedLapsInCycle + static_cast<int32>(NewLaps % LapsPerReassignment);
	const int64 Earned = NewLaps / LapsPerReassignment + Combined / LapsPerReassignment;
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	if (Badges && Badges->HasBadge(ECastleBadgeType::LapReassignment))
	{
		if (Earned > MAX_int64 - EarnedTemporaryNPCs)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("世界 %s 的一圈调岗令待派人数溢出。"), *GetPathNameSafe(GetWorld()));
			return;
		}
		EarnedTemporaryNPCs += Earned;
	}
	CompletedLapsInCycle = Combined % LapsPerReassignment;
}

bool UCastleBadgeRuntimeSubsystem::TryDispatchEarnedTemporaryNPCs(UCastleTowerDispatchSubsystem& Dispatch)
{
	while (EarnedTemporaryNPCs > 0)
	{
		FText Reason;
		if (!Dispatch.TryDispatchTemporarySingleLapNPC(Reason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("世界 %s 的一圈调岗令派兵失败：%s。"), *GetPathNameSafe(GetWorld()), *Reason.ToString());
			return false;
		}
		// 只在成功提交实体后消费一个名额，Mass 中从不直接生成实体。
		--EarnedTemporaryNPCs;
	}
	return true;
}

uint64 UCastleBadgeRuntimeSubsystem::ReserveFirstTowerEntry(int64& OutNumberBonus)
{
	OutNumberBonus = 0;
	if (!bWaveActive || bFirstTowerEntryUsed) { return 0; }
	bFirstTowerEntryUsed = true;
	if (const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld()))
	{
		OutNumberBonus = Badges->GetSpareBowstringBonus();
	}
	return WaveSerial;
}

void UCastleBadgeRuntimeSubsystem::CancelFirstTowerEntry(const uint64 Reservation)
{
	if (Reservation != 0 && bWaveActive && Reservation == WaveSerial) { bFirstTowerEntryUsed = false; }
}

TMap<FMassEntityHandle, FCastleBadgeNPCMovement> UCastleBadgeRuntimeSubsystem::CaptureNPCMovement(const double Now)
{
	for (auto It = NPCMovement.CreateIterator(); It; ++It)
	{
		// 两类效果均到期后才可移除；单独到期的一组通过数值查询返回单位倍率。
		if (Now >= It.Value().Whistle.EndsAt && Now >= It.Value().March.EndsAt) { It.RemoveCurrent(); }
	}
	return NPCMovement;
}

const FCastleBadgeNPCMovement* UCastleBadgeRuntimeSubsystem::FindNPCMovement(const FMassEntityHandle Entity) const
{
	return NPCMovement.Find(Entity);
}

bool UCastleBadgeRuntimeSubsystem::ShouldChainTower(const ACastleBoard& Board, const ACastleBuildingBase& Source,
	const ACastleBuildingBase& Target) const
{
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	return !bDispatchingChain && BattleBoard.Get() == &Board && Badges && Badges->HasBadge(ECastleBadgeType::ChainReaction)
		&& &Target != &Source && Target.CanReceiveBuildingTrigger() && IsFunctionTower(Source)
		&& Target.GetBuildingType() == Source.GetBuildingType() && Target.GetBuildingColor() == Source.GetBuildingColor()
		&& Target.GetBuildingNumber() == Source.GetBuildingNumber();
}

void UCastleBadgeRuntimeSubsystem::HandleBuildingTriggered(ACastleBuildingBase* Source, const FCastleBuildingTriggerContext& Context)
{
	if (Context.bChainReactionChild || bDispatchingChain || !IsValid(Source) || !IsFunctionTower(*Source)) { return; }
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	if (!Badges) { return; }

	if (Context.Source == ECastleBuildingTriggerSource::NPCEntry)
	{
		// 包含序列号的实体句柄使同一 Mass 槽位中后来派出的士兵拥有独立的本波首次机会。
		const FMassEntityHandle Entity = Context.NPC.Entity;
		const double Now = GetWorld()->GetTimeSeconds();
		const float Whistle = Badges->GetShiftWhistleSpeedMultiplier();
		const float March = Badges->GetRapidMarchSpeedMultiplier(Source->GetBuildingLevel());
		if (!FMath::IsFinite(Whistle) || !FMath::IsFinite(March) || !FMath::IsFinite(Whistle * March))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("功能塔 %s 的值班汽笛或急速行军倍率溢出。"), *Source->GetPathName());
			return;
		}
		if (bWaveActive)
		{
			bool bAlreadyEntered = false;
			NPCsEnteredThisWave.Add(Entity, &bAlreadyEntered);
			if (!bAlreadyEntered && WhistleRecipients < WhistleRecipientsPerWave && Whistle > 1.0f)
			{
				++WhistleRecipients;
				NPCMovement.FindOrAdd(Entity).Whistle.Apply(Whistle, Now, WhistleDuration);
				UE_LOG(LogMoving_Castle, Display, TEXT("值班汽笛：NPC %d 获得 %.2f 倍移速，本波名额 %d/%d。"),
					Entity.Index, Whistle, WhistleRecipients, WhistleRecipientsPerWave);
			}
		}
		if (March > 1.0f)
		{
			NPCMovement.FindOrAdd(Entity).March.Apply(March, Now, MarchDuration);
		}
	}

	const ACastleBoard* Board = BattleBoard.Get();
	if (!Board || !Badges->HasBadge(ECastleBadgeType::ChainReaction)) { return; }
	TArray<TWeakObjectPtr<ACastleBuildingBase>> Targets;
	for (ACastleBuildingBase* Target : Board->GetPlacedBuildings())
	{
		if (!IsValid(Target))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 连锁反应中止：建筑占用包含无效对象。"), *Board->GetPathName());
			return;
		}
		if (ShouldChainTower(*Board, *Source, *Target)) { Targets.Add(Target); }
	}

	// 子触发保留原 NPC 与进入点，直接执行组件入口，避免重复广播棋盘、炮塔联动和徽章事件。
	TGuardValue<bool> ChainGuard(bDispatchingChain, true);
	FCastleBuildingTriggerContext ChildContext = Context;
	ChildContext.bChainReactionChild = true;
	for (const TWeakObjectPtr<ACastleBuildingBase>& Entry : Targets)
	{
		ACastleBuildingBase* Target = Entry.Get();
		if (!Target || !IsValid(Target->BuildingEffect) || !Target->BuildingEffect->Trigger(ChildContext))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("功能塔 %s 的连锁反应触发目标 %s 失败。"),
				*GetPathNameSafe(Source), *GetPathNameSafe(Target));
			return;
		}
	}
	if (!Targets.IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("连锁反应：%s 额外触发了 %d 座功能塔。"), *Source->GetName(), Targets.Num());
	}
}

bool UCastleBadgeRuntimeSubsystem::TryGrantGameplayGold(const int64 Gold, const TCHAR* BadgeName) const
{
	const auto* Controller = Cast<ACastlePlayerController>(GetWorld()->GetFirstPlayerController());
	auto* Economy = Controller ? Controller->GetEconomy() : nullptr;
	if (!Economy || Gold <= 0 || Gold > MAX_int32)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("%s 结算失败：世界 %s 的金币账户 %s 无效，或奖励 %lld 超出正整数范围。"),
			BadgeName, *GetPathNameSafe(GetWorld()), *GetPathNameSafe(Economy), Gold);
		return false;
	}
	int32 FinalGold = 0;
	FText Reason;
	if (!Economy->TryAddGameplayGold(static_cast<int32>(Gold), Reason, &FinalGold)) { return false; }
	UE_LOG(LogMoving_Castle, Display, TEXT("%s：获得 %d 金币。"), BadgeName, FinalGold);
	return true;
}

void UCastleBadgeRuntimeSubsystem::HandleBattleCleared(const ECastleBattleKind Kind, const int32 Stage)
{
	ResetBattleEffects();
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	const int64 Reward = Badges ? Badges->GetDoubleDoorPassGold() : 0;
	if (Reward == 0) { return; }
	const ACastleBoard* Board = BattleBoard.Get();
	const auto* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	if (!Board || !Navigation || !FMath::IsFinite(Board->CellSize) || Board->CellSize <= 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("第 %d 关双门打卡牌结算失败：棋盘 %s、格宽或共享导航无效。"), Stage, *GetPathNameSafe(Board));
		return;
	}
	const auto Path = Navigation->AcquireSharedPath();
	// 没有正式玩家路线时无法满足长度条件；正好十格也不满足严格大于。
	if (!Path || !Path->bValid) { return; }
	if (!FMath::IsFinite(Path->Length))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 第 %d 关的共享路径长度无效。"), *Board->GetPathName(), Stage);
		return;
	}
	if (Path->Length > 10.0 * Board->CellSize) { TryGrantGameplayGold(Reward, TEXT("双门打卡牌")); }
}

bool UCastleBadgeRuntimeSubsystem::TryAwardBeeKill()
{
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	const int64 Gold = Badges ? Badges->GetBerserkBeeGold() : 0;
	return Gold == 0 || TryGrantGameplayGold(Gold, TEXT("狂暴工蜂"));
}

bool UCastleBadgeRuntimeSubsystem::TryResolveLastBalloonClasp(const UCastleMainTowerComponent& Tower, float& OutRetainedHealth)
{
	OutRetainedHealth = 0.0f;
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	if (bLastBalloonClaspUsed || !Badges || !Badges->HasBadge(ECastleBadgeType::LastBalloonClasp)) { return true; }
	auto* Shields = GetWorld()->GetSubsystem<UCastleShieldTowerSubsystem>();
	if (!Shields || Tower.MaxBalloons <= 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔 %s 无法结算最后一颗气球扣：气球容量或护盾子系统无效。"), *Tower.GetPathName());
		return false;
	}
	// 取不超过精确阈值的 float，避免恰好一颗气球的生命在转成 float 后被向上数成两颗。
	const double Limit = FMath::Min(static_cast<double>(Tower.CurrentHealth), static_cast<double>(Tower.MaxHealth) / Tower.MaxBalloons);
	float RetainedHealth = static_cast<float>(Limit);
	if (RetainedHealth > Limit) { RetainedHealth = std::nextafter(RetainedHealth, 0.0f); }
	if (RetainedHealth <= 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔 %s 的最后一颗气球生命 %.17g 无法表示为正数。"), *Tower.GetPathName(), Limit);
		return false;
	}

	int64 Consumed = 0;
	// 破盾通知可以同步调用其他玩法；先登记本局已使用，场上无盾时明确保留机会。
	bLastBalloonClaspUsed = true;
	if (!Shields->TryConsumeStrongestActiveShield(Consumed))
	{
		bLastBalloonClaspUsed = false;
		return true;
	}
	OutRetainedHealth = RetainedHealth;
	UE_LOG(LogMoving_Castle, Display, TEXT("最后一颗气球扣：%s 耗尽 %lld 次护盾抵挡，保留 %.2f 生命。"),
		*Tower.GetName(), Consumed, RetainedHealth);
	return true;
}
