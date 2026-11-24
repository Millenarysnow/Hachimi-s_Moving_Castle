// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleTowerDispatchSubsystem.h"
#include "Castle/Director/CastleDirectorSubsystem.h"

#include "CastleMainTowerComponent.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleNavigationSubsystem.h"
#include "CastleTowerDispatchConfigDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "Moving_Castle.h"

namespace
{
	bool UsesTutorialDispatchRoute(UWorld* World)
	{
		for (TActorIterator<ACastlePatrolRoute> It(World); It; ++It)
		{
			if (It->IsTutorialRoute()) { return true; }
		}
		return false;
	}
}

bool UCastleTowerDispatchSubsystem::Reject(FText& OutReason, const FString& Detail, const bool bConfigurationError) const
{
	OutReason = FText::FromString(Detail);
	if (bConfigurationError)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔出兵 [%s]：%s"), *GetPathNameSafe(Config), *Detail);
	}
	else
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("主塔出兵拒绝：%s"), *Detail);
	}
	return false;
}

bool UCastleTowerDispatchSubsystem::Configure(UCastleTowerDispatchConfigDataAsset* Configuration, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!IsValid(Configuration) || bSessionActive)
	{
		return Reject(OutReason, FString::Printf(TEXT("无法登记 %s：资源无效或出兵会话仍在进行。"), *GetPathNameSafe(Configuration)), true);
	}
	if (!Configuration->ValidateConfiguration(OutReason)) { return false; }
	Config = Configuration;
	return true;
}

bool UCastleTowerDispatchSubsystem::StartBattleDispatchSession(UMassEntityConfigAsset* NPCConfig, const int32 InitialPoolCount, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (bSessionActive || !ActiveNPCs.IsEmpty() || !IsValid(Config) || !IsValid(NPCConfig)
		|| !NPCConfig->FindTrait(UCastleMassNPCTrait::StaticClass()) || InitialPoolCount < 1)
	{
		return Reject(OutReason, FString::Printf(TEXT("无法开始会话：旧会话须已清理，且需要有效出兵配置、NPC 模板 %s 和正数待命池。"),
			*GetPathNameSafe(NPCConfig)), true);
	}
	if (!Config->ValidateConfiguration(OutReason)) { return false; }
	auto* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	if (!Navigation) { return Reject(OutReason, TEXT("当前世界缺少巡逻导航子系统。"), true); }
	// 复制点列后再切换请求，既有玩家点不包含双门，也不与导航内部数组发生原地别名。
	const TArray<FVector> ExistingWaypoints = Navigation->GetRequestedWaypoints();
	if (!ExistingWaypoints.IsEmpty() && !Navigation->RequestTowerDispatchRoute(ExistingWaypoints, OutReason)) { return false; }
	SessionNPCConfig = NPCConfig;
	ReserveCount = SessionTotalNPCs = InitialPoolCount;
	bSessionActive = true;
	bAutoDispatchEnabled = false;
	bRequiresFirstManualDispatch = UsesTutorialDispatchRoute(GetWorld());
	AutoDispatchElapsed = 0.0;
	StopManualDispatch();
	UE_LOG(LogMoving_Castle, Display, TEXT("主塔战斗出兵会话已开始，待命 %d 名 NPC。"), ReserveCount);
	return true;
}

bool UCastleTowerDispatchSubsystem::EndBattleDispatchSession(FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!bSessionActive) { return Reject(OutReason, TEXT("当前没有主塔出兵会话。"), false); }
	const auto* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
	// 正式导演可在后续关卡复用已提交开环，单独的出兵演示结束后恢复闭环。
	if (!bEndingSession && !(Director && Director->IsRunning()) && !UsesTutorialDispatchRoute(GetWorld()))
	{
		auto* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
		if (!Navigation) { return Reject(OutReason, TEXT("当前世界缺少巡逻导航子系统，无法恢复普通巡逻。"), true); }
		const TArray<FVector> Waypoints = Navigation->GetRequestedWaypoints();
		if (!Waypoints.IsEmpty() && !Navigation->RequestSharedPatrolRoute(Waypoints, OutReason)) { return false; }
	}
	bEndingSession = true;
	bAutoDispatchEnabled = false;
	StopManualDispatch();
	ReclaimFinishedNPCs();
	return true;
}

bool UCastleTowerDispatchSubsystem::BeginBattleDispatch(const int32 TotalPoolCount)
{
	FText Reason;
	return StartBattleDispatchSession(Config ? Config->NPCConfiguration.Get() : nullptr, TotalPoolCount, Reason);
}

bool UCastleTowerDispatchSubsystem::EndBattleDispatch()
{
	FText Reason;
	return EndBattleDispatchSession(Reason);
}

bool UCastleTowerDispatchSubsystem::TryDispatchSingleNPC(FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!bSessionActive || bEndingSession) { return Reject(OutReason, TEXT("当前没有进行中的主塔战斗出兵会话。"), false); }
	if (ReserveCount == 0) { return Reject(OutReason, TEXT("主塔待命池已空。"), false); }
	const auto* Towers = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>();
	const auto* Tower = Towers ? Towers->GetRegisteredMainTower() : nullptr;
	if (!Tower || !Tower->IsAlive())
	{
		return Reject(OutReason, TEXT("主塔无效或已失去全部生命。"), false);
	}
	const auto* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	const auto Path = Navigation ? Navigation->AcquireSharedPath() : nullptr;
	if (!Path || !Path->bValid || !Path->bTowerDispatch || Path->Length <= UE_DOUBLE_SMALL_NUMBER)
	{
		return Reject(OutReason, Navigation && !Navigation->LastFailure.IsEmpty() ? Navigation->LastFailure.ToString()
			: FString(TEXT("门到门巡逻路线尚未就绪，请在编辑阶段提交有效路线。")), false);
	}
	auto* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
	auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!Units || !Mass || Mass->GetMutableEntityManager().IsProcessing())
	{
		return Reject(OutReason, TEXT("只能在 Mass 处理外派兵，且 Mass 子系统必须可用。"), true);
	}
	FVector Location, Direction;
	Path->SamplePatrol(0.0, Location, Direction);
	TArray<FCastleMassUnitHandle> Spawned;
	if (!Units->SpawnUnits(SessionNPCConfig, {FTransform(Direction.ToOrientationQuat(), Location)}, Spawned, OutReason))
	{
		return Reject(OutReason, OutReason.ToString(), true);
	}
	check(Spawned.Num() == 1);
	// 生成和启用在同一个游戏线程安全边界提交；出门实体直接绑定第一个路段，避免重叠路线误判。
	auto& Patrol = Mass->GetMutableEntityManager().GetFragmentDataChecked<FCastleMassPatrolFragment>(Spawned[0].Entity);
	Patrol.bEnabled = true;
	Patrol.bTowerDispatched = true;
	Patrol.PathRevision = Path->PathRevision;
	Patrol.RouteRevision = Path->RouteRevision;
	ActiveNPCs.Add({Spawned[0]});
	--ReserveCount;
	// 在成功提交实体后解除门控，让同一派出事件可以开启后续循环。
	if (bStartingManualDispatch) { bRequiresFirstManualDispatch = false; }
	check(ReserveCount + ActiveNPCs.Num() == SessionTotalNPCs);
	UE_LOG(LogMoving_Castle, Display, TEXT("主塔门 A 派出 1 名 NPC，待命 %d，在场 %d。"), ReserveCount, ActiveNPCs.Num());
	OnNPCDispatched.Broadcast(Spawned[0]);
	return true;
}

bool UCastleTowerDispatchSubsystem::SetAutoDispatchEnabled(const bool bEnabled)
{
	if (bEnabled && bRequiresFirstManualDispatch)
	{
		ShowMessage(INVTEXT("请先点击主塔，手动派出城主。"), false);
		return false;
	}
	if (!bSessionActive || bEndingSession)
	{
		FText Reason;
		return Reject(Reason, TEXT("开启或关闭自动出兵需要进行中的出兵会话。"), false);
	}
	if (bAutoDispatchEnabled != bEnabled)
	{
		bAutoDispatchEnabled = bEnabled;
		AutoDispatchElapsed = 0.0;
		ShowMessage(FText::FromString(bEnabled ? TEXT("主塔自动出兵已开启。") : TEXT("主塔已切回手动出兵。")), true);
	}
	return true;
}

bool UCastleTowerDispatchSubsystem::AddReserveNPCs(const int32 Delta)
{
	if (!bSessionActive || bEndingSession || Delta <= 0 || Delta > MAX_int32 - SessionTotalNPCs)
	{
		FText Reason;
		return Reject(Reason, FString::Printf(TEXT("无法增加 %d 名待命 NPC：需要有效会话、正数增量和足够容量。"), Delta), true);
	}
	ReserveCount += Delta;
	SessionTotalNPCs += Delta;
	check(ReserveCount + ActiveNPCs.Num() == SessionTotalNPCs);
	return true;
}

void UCastleTowerDispatchSubsystem::GetDispatchCounts(int32& OutReserveCount, int32& OutActiveCount) const
{
	OutReserveCount = ReserveCount;
	OutActiveCount = ActiveNPCs.Num();
}

bool UCastleTowerDispatchSubsystem::BeginManualDispatch(FText& OutReason)
{
	StopManualDispatch();
	const TGuardValue<bool> ManualRequest(bStartingManualDispatch, true);
	bManualDispatchHeld = TryDispatchSingleNPC(OutReason);
	return bManualDispatchHeld;
}

void UCastleTowerDispatchSubsystem::StopManualDispatch()
{
	bManualDispatchHeld = false;
	HoldDispatchElapsed = 0.0;
}

void UCastleTowerDispatchSubsystem::ReclaimFinishedNPCs()
{
	auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	auto* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
	if (!Mass || !Units || Mass->GetMutableEntityManager().IsProcessing()) { return; }
	auto& Manager = Mass->GetMutableEntityManager();
	for (int32 Index = ActiveNPCs.Num() - 1; Index >= 0; --Index)
	{
		FActiveNPC& NPC = ActiveNPCs[Index];
		if (!Manager.IsEntityActive(NPC.Handle.Entity))
		{
			// 只有实际销毁后才增加待命，从而保证生成、到门和回池时的数量守恒。
			ActiveNPCs.RemoveAtSwap(Index);
			++ReserveCount;
			UE_LOG(LogMoving_Castle, Display, TEXT("主塔回收 1 名 NPC，待命 %d，在场 %d。"), ReserveCount, ActiveNPCs.Num());
			continue;
		}
		const auto& Patrol = Manager.GetFragmentDataChecked<FCastleMassPatrolFragment>(NPC.Handle.Entity);
		if (!NPC.bDespawnRequested && (bEndingSession || Patrol.bReachedDispatchEntrance))
		{
			NPC.bDespawnRequested = Units->DespawnUnits({NPC.Handle});
			if (!NPC.bDespawnRequested)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("主塔 NPC %d 的 Mass 销毁提交失败。"), NPC.Handle.Entity.Index);
			}
		}
	}
	check(ReserveCount + ActiveNPCs.Num() == SessionTotalNPCs);
	if (bEndingSession && ActiveNPCs.IsEmpty())
	{
		ReserveCount = SessionTotalNPCs = 0;
		SessionNPCConfig = nullptr;
		bSessionActive = bEndingSession = false;
		UE_LOG(LogMoving_Castle, Display, TEXT("主塔战斗出兵会话已结束。"));
	}
}

void UCastleTowerDispatchSubsystem::Tick(const float DeltaSeconds)
{
	ReclaimFinishedNPCs();
	if (!bSessionActive || bEndingSession) { return; }
	// 一个实际周期只产生一次请求；非法路线的自动重试仍受配置频率限制。
	if (bAutoDispatchEnabled && ReserveCount > 0)
	{
		AutoDispatchElapsed += DeltaSeconds;
		if (AutoDispatchElapsed >= Config->AutoDispatchInterval)
		{
			AutoDispatchElapsed = FMath::Fmod(AutoDispatchElapsed, static_cast<double>(Config->AutoDispatchInterval));
			FText Reason;
			if (!TryDispatchSingleNPC(Reason)) { ShowMessage(Reason, false); }
		}
	}
	if (bManualDispatchHeld)
	{
		HoldDispatchElapsed += DeltaSeconds;
		if (HoldDispatchElapsed >= Config->HoldDispatchInterval)
		{
			HoldDispatchElapsed = FMath::Fmod(HoldDispatchElapsed, static_cast<double>(Config->HoldDispatchInterval));
			FText Reason;
			if (!TryDispatchSingleNPC(Reason))
			{
				StopManualDispatch();
				ShowMessage(Reason, false);
			}
		}
	}
}

bool UCastleTowerDispatchSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UCastleTowerDispatchSubsystem::IsTickable() const
{
	return !IsTemplate() && bSessionActive;
}

TStatId UCastleTowerDispatchSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCastleTowerDispatchSubsystem, STATGROUP_Tickables);
}

void UCastleTowerDispatchSubsystem::ShowMessage(const FText& Message, const bool bSuccess) const
{
	UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *Message.ToString());
	if (GEngine) { GEngine->AddOnScreenDebugMessage(2026112203, 4.0f, bSuccess ? FColor::Cyan : FColor::Yellow, Message.ToString()); }
}
