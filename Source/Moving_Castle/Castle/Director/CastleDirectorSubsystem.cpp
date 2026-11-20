// Copyright Epic Games, Inc. All Rights Reserved.
#include "CastleDirectorSubsystem.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"

#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorSettings.h"
#include "CastleBoard.h"
#include "CastlePlayerController.h"
#include "CastleMainTowerComponent.h"
#include "CastleShopComponent.h"
#include "CastleBackpackComponent.h"
#include "CastleBuildingBase.h"
#include "CastleNavigationSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Units/Mass/CastleMassSpawner.h"
#include "Units/Mass/CastleMassUnitSubsystem.h"
#include "Units/Navigation/Interaction/CastlePatrolRoute.h"
#include "MassEntitySubsystem.h"
#include "MassEntityConfigAsset.h"

bool UCastleDirectorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UCastleDirectorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCastleDirectorSubsystem, STATGROUP_Tickables);
}

void UCastleDirectorSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Tickable 对象在初始化后即可 Tick；只在世界真正开始游戏后执行一次启动尝试。
	if (!bStartAttempted && GetWorld()->HasBegunPlay()) { TryAutoStart(); }
	if (PendingNode.IsValid())
	{
		// 回调可能由 Mass 处理中的碰撞触发；直到世界子系统 Tick 才消费唯一选择。
		ACastleDirectorNodeActor* Selected = PendingNode.Get();
		PendingNode.Reset();
		FText Reason;
		TryStartSelectedBattle(Selected, Reason);
	}
}

void UCastleDirectorSubsystem::TryAutoStart()
{
	bStartAttempted = true;
	const UCastleDirectorSettings* Settings = GetDefault<UCastleDirectorSettings>();
	// 设置保存正式地图资产路径；PIE 的包名前缀不属于地图身份，比较前由引擎接口移除。
	FString MapName = GetWorld()->GetOutermost()->GetName();
	MapName = UWorld::RemovePIEPrefix(MapName);
	bool bAutoStart = false;
	for (const FSoftObjectPath& Path : Settings->AutoStartMaps)
	{
		if (Path.GetLongPackageName() == MapName) { bAutoStart = true; break; }
	}
	if (!bAutoStart)
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("导演未启动：地图 %s 不在 AutoStartMaps 中。"), *MapName);
		return;
	}
	Config = Settings->DirectorConfig.LoadSynchronous();
	FText Reason;
	if (!StartDirector(Reason))
	{
		// 启动后半段失败时撤下已准备的界面，不留下 HUD 或模态输入的半启动状态。
		for (UUserWidget* Widget : {HUDWidget.Get(), RewardWidget.Get(), GameOverWidget.Get()}) { if (Widget) { Widget->RemoveFromParent(); } }
		HUDWidget = nullptr;
		RewardWidget = nullptr;
		GameOverWidget = nullptr;
		ShowMessage(FText::FromString(TEXT("导演系统未启动：") + Reason.ToString()), true);
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *StatusMessage.ToString());
	}
}

bool UCastleDirectorSubsystem::StartDirector(FText& OutReason)
{
	if (!IsValid(Config))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("配置资产 %s 加载失败。"), *GetDefault<UCastleDirectorSettings>()->DirectorConfig.ToString()));
		return false;
	}
	if (!Config->ValidateConfig(OutReason)) { return false; }
	Combat = GetWorld()->GetSubsystem<UCastleCombatSubsystem>();
	if (!IsValid(Combat) || !Combat->InitializeCombat(Config->CombatConfig, OutReason))
	{
		if (!IsValid(Combat)) { OutReason = FText::FromString(TEXT("本世界缺少 CastleCombatSubsystem。")); }
		return false;
	}
	int32 BoardCount = 0;
	for (TActorIterator<ACastleBoard> It(GetWorld()); It; ++It) { Board = *It; ++BoardCount; }
	if (BoardCount != 1)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("需要唯一的城堡棋盘，当前棋盘数 %d。"), BoardCount));
		return false;
	}
	// MainMap 的开局依赖主塔侧格；旧地图仍可以显式使用地面格。
	if (!Board->ValidatePlacementSurface(OutReason)) { return false; }
	Tower = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>()->GetRegisteredMainTower();
	Controller = Cast<ACastlePlayerController>(GetWorld()->GetFirstPlayerController());
	if (!IsValid(Tower) || !IsValid(Controller) || !Controller->IsLocalController())
	{
		OutReason = FText::FromString(TEXT("主塔未注册，或本地控制器不是 CastlePlayerController。"));
		return false;
	}
	if (!EnsurePresentation(OutReason)) { return false; }
	// 候选节点保持隐藏且无碰撞，只有所有启动依赖准备成功后才一起激活。
	TArray<TObjectPtr<ACastleDirectorNodeActor>> Candidates;
	if (!PrepareNodes(1, Candidates, OutReason)) { return false; }
	if (!Controller->GetShop()->TrySetRoomLimit(Config->BaseRoomLimit, OutReason))
	{
		for (ACastleDirectorNodeActor* Node : Candidates) { Node->Destroy(); }
		return false;
	}
	Level = 1;
	Experience = 0;
	ClearedSteps = 0;
	bShopPermission = false;
	bRunning = true;
	BattleClearedHandle = Combat->OnBattleCleared.AddUObject(this, &UCastleDirectorSubsystem::HandleBattleCleared);
	BattleFailedHandle = Combat->OnBattleFailed.AddUObject(this, &UCastleDirectorSubsystem::HandleBattleFailed);
	Controller->GetShop()->CloseShop();
	Nodes = MoveTemp(Candidates);
	Tower->OnHealthChanged.AddUniqueDynamic(this, &UCastleDirectorSubsystem::HandleTowerHealthChanged);
	SetPhase(ECastleDirectorPhase::Editing);
	for (ACastleDirectorNodeActor* Node : Nodes) { Node->ActivateNode(); }
	ShowMessage(FText::FromString(TEXT("飞向节点进入下一关")), false);
	UE_LOG(LogMoving_Castle, Display, TEXT("导演启动：编辑阶段，第 1 关，主塔 Lv1，房间上限 %d。"), Config->BaseRoomLimit);
	if (Tower->CurrentHealth <= 0.0f) { EnterGameOver(FText::GetEmpty()); }
	return true;
}

int32 UCastleDirectorSubsystem::GetNodeCountForStep(const UCastleDirectorConfigDataAsset& InConfig, const int32 Step)
{
	// 只使用明确定义的无符号运算组合种子，避免平台 Hash 或有符号溢出导致不可复现。
	const uint32 Seed = static_cast<uint32>(InConfig.RunSeed) ^ (static_cast<uint32>(Step) * 0x9e3779b9u);
	FRandomStream Stream(static_cast<int32>(Seed));
	return Stream.RandRange(InConfig.NodeCountMin, InConfig.NodeCountMax);
}

ECastleDirectorNodeKind UCastleDirectorSubsystem::GetNodeKindForStep(const UCastleDirectorConfigDataAsset& InConfig, const int32 Step)
{
	return Step % InConfig.EliteEveryNSteps == 0 ? ECastleDirectorNodeKind::Elite : ECastleDirectorNodeKind::Normal;
}

bool UCastleDirectorSubsystem::PrepareNodes(const int32 Step, TArray<TObjectPtr<ACastleDirectorNodeActor>>& OutNodes, FText& OutReason)
{
	OutNodes.Reset();
	if (bAutomation) { return true; }
	if (!IsValid(Board) || !IsValid(Tower) || !IsValid(Tower->GetOwner()))
	{
		OutReason = FText::FromString(TEXT("节点生成失败：棋盘或主塔已失效。"));
		return false;
	}
	const int32 Count = GetNodeCountForStep(*Config, Step);
	const FVector Center = Tower->GetTargetLocation();
	const float CenterYaw = Board->GetActorForwardVector().Rotation().Yaw;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = CenterYaw - Config->NodeArcDegrees * 0.5f + Config->NodeArcDegrees * Index / (Count - 1);
		const FVector Direction = FRotator(0.0f, Angle, 0.0f).Vector();
		FVector Position = Center + Direction * Config->NodeRingRadius;
		Position.Z = Tower->GetOwner()->GetActorLocation().Z + Config->NodeHeightOffset;
		const FTransform Transform((Center - Position).Rotation(), Position);
		ACastleDirectorNodeActor* Node = GetWorld()->SpawnActorDeferred<ACastleDirectorNodeActor>(
			ACastleDirectorNodeActor::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Node)
		{
			for (ACastleDirectorNodeActor* Candidate : OutNodes) { Candidate->Destroy(); }
			OutNodes.Reset();
			OutReason = FText::FromString(FString::Printf(TEXT("节点生成失败：第 %d 个 Actor 无法创建。"), Index + 1));
			return false;
		}
		if (!Node->Configure(*Config, Step, GetNodeKindForStep(*Config, Step)))
		{
			Node->Destroy();
			for (ACastleDirectorNodeActor* Candidate : OutNodes) { Candidate->Destroy(); }
			OutNodes.Reset();
			OutReason = FText::FromString(FString::Printf(TEXT("节点标签配置失败：第 %d 个节点的配置资产 %s 无效。"), Index + 1, *Config->GetPathName()));
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
			return false;
		}
		Node->FinishSpawning(Transform);
		if (!Node->IsLabelReady())
		{
			Node->Destroy();
			for (ACastleDirectorNodeActor* Candidate : OutNodes) { Candidate->Destroy(); }
			OutNodes.Reset();
			OutReason = FText::FromString(FString::Printf(TEXT("节点标签实例化失败：第 %d 个节点的WBP %s 缺失或绑定无效。"),
				Index + 1, *GetPathNameSafe(Config->NodeLabelWidgetClass.Get())));
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
			return false;
		}
		OutNodes.Add(Node);
	}
	return true;
}

void UCastleDirectorSubsystem::DestroyNodes()
{
	for (ACastleDirectorNodeActor* Node : Nodes) { if (IsValid(Node)) { Node->Destroy(); } }
	Nodes.Reset();
}

bool UCastleDirectorSubsystem::IsEditingAllowed(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!bRunning || Phase == ECastleDirectorPhase::Editing) { return true; }
	OutReason = FText::FromString(Phase == ECastleDirectorPhase::Battle ? TEXT("战斗中无法编辑")
		: Phase == ECastleDirectorPhase::Reward ? TEXT("选择奖励时无法编辑") : TEXT("游戏结束后无法编辑"));
	return false;
}

bool UCastleDirectorSubsystem::IsShopAccessAllowed(FText& OutReason) const
{
	if (!IsEditingAllowed(OutReason))
	{
		OutReason = FText::FromString(Phase == ECastleDirectorPhase::Battle ? TEXT("战斗中无法使用商店")
			: Phase == ECastleDirectorPhase::Reward ? TEXT("选择奖励时无法使用商店") : TEXT("游戏结束后无法使用商店"));
		return false;
	}
	if (bRunning && !bShopPermission)
	{
		OutReason = FText::FromString(TEXT("本轮未选择商店"));
		return false;
	}
	return true;
}

void UCastleDirectorSubsystem::HandleNodeTouched(ACastleDirectorNodeActor* Node)
{
	if (!bRunning || Phase != ECastleDirectorPhase::Editing || !Nodes.Contains(Node) || PendingNode.IsValid() || LastNodeSelectionFrame == GFrameCounter) { return; }
	LastNodeSelectionFrame = GFrameCounter;
	PendingNode = Node;
}

bool UCastleDirectorSubsystem::TryStartSelectedBattle(ACastleDirectorNodeActor* Node, FText& OutReason)
{
	if (!bRunning || Phase != ECastleDirectorPhase::Editing || !IsValid(Node) || !Nodes.Contains(Node))
	{
		OutReason = FText::FromString(TEXT("节点选择已失效或当前阶段不是编辑。"));
		UE_LOG(LogMoving_Castle, Error, TEXT("导演开战拒绝：节点 %s，%s"), *GetPathNameSafe(Node), *OutReason.ToString());
		return false;
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("选择导演节点 %s：第 %d 关，类型 %d。"), *GetPathNameSafe(Node), GetNextStep(), static_cast<int32>(Node->GetKind()));
	UCastleNavigationSubsystem* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	const auto Path = Navigation->AcquireSharedPath();
	if (!Path || !Path->bValid || !Path->bClosedLoop)
	{
		const FString Reason = Navigation->LastFailure.IsEmpty() ? TEXT("路线未就绪") : Navigation->LastFailure.ToString();
		OutReason = FText::FromString(TEXT("无法开战：") + Reason);
		ShowMessage(OutReason, false);
		UE_LOG(LogMoving_Castle, Warning, TEXT("%s"), *StatusMessage.ToString());
		return false;
	}
	const auto Reject = [this, &OutReason](const FString& Reason)
	{
		OutReason = FText::FromString(Reason);
		ShowMessage(OutReason, true);
		UE_LOG(LogMoving_Castle, Error, TEXT("导演开战失败：配置 %s，%s"), *GetPathNameSafe(Config), *Reason);
		return false;
	};
	auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!Mass || Mass->GetEntityManager().IsProcessing()) { return Reject(TEXT("Mass 不可用或正在处理，拒绝本次选择。")); }
	if (!IsValid(Board) || !IsValid(Controller) || !IsValid(Combat) || !IsValid(Config) || IsValid(BattleNPCSpawner))
	{
		return Reject(TEXT("棋盘、控制器、战斗系统、配置无效，或上场 NPC 尚未回收。"));
	}
	const ECastleBattleKind Kind = Node->GetKind() == ECastleDirectorNodeKind::Elite ? ECastleBattleKind::Elite : ECastleBattleKind::Normal;
	const int32 PreviousCount = Kind == ECastleBattleKind::Elite ? EliteBattles : NormalBattles;
	if (PreviousCount == MAX_int32) { return Reject(TEXT("本类战斗序号溢出。")); }
	FText Failure;
	// 显隐校验在撤下商店和巡逻草稿之前，失败不得推进开战事务。
	if (!Board->SetPlacementSurfacePresentationHidden(true, Failure)) { return Reject(Failure.ToString()); }
	const bool bPreviousPermission = bShopPermission;
	const bool bShopWasOpen = Controller->GetShop()->IsShopOpen();
	Controller->GetShop()->CloseShop();
	bShopPermission = false;
	for (TActorIterator<ACastlePatrolRoute> It(GetWorld()); It; ++It) { It->CancelEditingDraft(); }
	Controller->HideDirectorEditingPresentation();
	// 生成器禁用 BeginPlay 自动生成，显式调用以便同步核对完整批次数量。
	BattleNPCSpawner = GetWorld()->SpawnActorDeferred<ACastleMassSpawner>(ACastleMassSpawner::StaticClass(), FTransform::Identity,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	bool bNPCsReady = false;
	if (IsValid(BattleNPCSpawner))
	{
		BattleNPCSpawner->Configuration = Config->NPCConfiguration;
		BattleNPCSpawner->Count = Config->NPCCount;
		BattleNPCSpawner->bSpawnOnBeginPlay = false;
		BattleNPCSpawner->bFollowSharedPatrolRoute = true;
		BattleNPCSpawner->FinishSpawning(FTransform::Identity);
		bNPCsReady = BattleNPCSpawner->SpawnUnits() && BattleNPCSpawner->SpawnedUnits.Num() == Config->NPCCount;
	}
	if (!bNPCsReady || !Combat->StartBattle(Kind, PreviousCount + 1, Failure))
	{
		if (!bNPCsReady)
		{
			Failure = FText::FromString(FString::Printf(TEXT("NPC 配置 %s：期望 %d 个单位，实际 %d；生成未完整成功。"),
				*GetPathNameSafe(Config->NPCConfiguration), Config->NPCCount, IsValid(BattleNPCSpawner) ? BattleNPCSpawner->SpawnedUnits.Num() : 0));
		}
		FText CleanupFailure;
		const bool bRecalled = RecallBattleNPCs(CleanupFailure);
		FText SurfaceFailure;
		const bool bSurfaceRestored = Board->SetPlacementSurfacePresentationHidden(false, SurfaceFailure);
		bShopPermission = bPreviousPermission;
		if (bShopWasOpen) { Controller->ToggleShop(); }
		return Reject(Failure.ToString() + (bRecalled ? FString() : TEXT("；NPC 回收失败：") + CleanupFailure.ToString())
			+ (bSurfaceRestored ? FString() : TEXT("；建造面恢复失败：") + SurfaceFailure.ToString()));
	}
	ActiveBattleKind = Kind;
	ActiveBattleOrdinal = PreviousCount + 1;
	if (Kind == ECastleBattleKind::Elite) { ++EliteBattles; } else { ++NormalBattles; }
	DestroyNodes();
	SetPhase(ECastleDirectorPhase::Battle);
	ShowMessage(FText::FromString(TEXT("战斗进行中")), false);
	UE_LOG(LogMoving_Castle, Display, TEXT("导演开战提交：第 %d 关，本类第 %d 场，沿闭环派出 %d 个 NPC。"), GetNextStep(), ActiveBattleOrdinal, Config->NPCCount);
	return true;
}

bool UCastleDirectorSubsystem::RecallBattleNPCs(FText& OutReason)
{
	if (!BattleNPCSpawner) { return true; }
	auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!IsValid(BattleNPCSpawner) || !Mass || Mass->GetEntityManager().IsProcessing())
	{
		OutReason = FText::FromString(FString::Printf(TEXT("生成器 %s 无效或 Mass 正在处理，不能回收 NPC。"), *GetPathNameSafe(BattleNPCSpawner)));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	const int32 Count = BattleNPCSpawner->SpawnedUnits.Num();
	if (!GetWorld()->GetSubsystem<UCastleSupportTowerSubsystem>()->EndBattleSupport())
	{
		OutReason = FText::FromString(TEXT("支援塔的本场增援或飞行对象未能回收。"));
		return false;
	}
	// Spawner 的 void 回收接口不能报告失败；在同一批句柄上调用原生 bool 接口并确认提交。
	// 零句柄表示生成尚未开始；有句柄时必须成功提交死亡回收，不能丢掉失败批次。
	if (Count > 0 && !GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>()->DespawnUnits(BattleNPCSpawner->SpawnedUnits))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("NPC生成器 %s 的批量回收请求被拒绝。"), *BattleNPCSpawner->GetPathName()));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	BattleNPCSpawner->SpawnedUnits.Reset();
	BattleNPCSpawner->Destroy();
	BattleNPCSpawner = nullptr;
	UE_LOG(LogMoving_Castle, Display, TEXT("导演已回收 %d 个 NPC 并销毁本场生成器。"), Count);
	return true;
}

void UCastleDirectorSubsystem::HandleBattleCleared(const ECastleBattleKind Kind, const int32 Ordinal)
{
	if (!bRunning || Phase != ECastleDirectorPhase::Battle || Kind != ActiveBattleKind || Ordinal != ActiveBattleOrdinal)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("导演拒绝非法或重复清场通知：阶段 %d，收到类型 %d/序号 %d，预期 %d/%d。"),
			static_cast<int32>(Phase), static_cast<int32>(Kind), Ordinal, static_cast<int32>(ActiveBattleKind), ActiveBattleOrdinal);
		return;
	}
	FText Failure;
	if (!RecallBattleNPCs(Failure)) { EnterGameOver(FText::FromString(TEXT("战斗系统出错：") + Failure.ToString())); return; }
	++ClearedSteps;
	SetPhase(ECastleDirectorPhase::Reward);
	ShowMessage(FText::FromString(TEXT("通关奖励：请选择一项")), false);
	UE_LOG(LogMoving_Castle, Display, TEXT("导演清场：已通过 %d 关，进入三选一，建造面保持隐藏。"), ClearedSteps);
}

void UCastleDirectorSubsystem::HandleBattleFailed(const ECastleBattleKind Kind, const int32 Ordinal, const FText& Reason)
{
	UE_LOG(LogMoving_Castle, Error, TEXT("导演收到战斗失败：配置 %s，类型 %d，序号 %d，%s"),
		*GetPathNameSafe(Config), static_cast<int32>(Kind), Ordinal, *Reason.ToString());
	EnterGameOver(FText::FromString(TEXT("战斗系统出错：") + Reason.ToString()));
}

bool UCastleDirectorSubsystem::TryChooseReward(const ECastleDirectorReward Reward, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const auto Reject = [this, &OutReason](const FString& Reason)
	{
		OutReason = FText::FromString(Reason);
		ShowMessage(OutReason, true);
		UE_LOG(LogMoving_Castle, Error, TEXT("导演奖励失败：%s"), *Reason);
		return false;
	};
	if (!bRunning || Phase != ECastleDirectorPhase::Reward || bApplyingReward || !IsValid(Config)
		|| !IsValid(Controller) || !IsValid(Tower) || !Tower->IsAlive())
	{
		return Reject(TEXT("阶段不允许领取奖励，或配置、控制器、主塔已失效。"));
	}
	TGuardValue<bool> Guard(bApplyingReward, true);
	// 先校验奖励种类和全部候选数值，不提前改经验、等级、生命或商店商品。
	if (Reward != ECastleDirectorReward::Experience && Reward != ECastleDirectorReward::Healing && Reward != ECastleDirectorReward::Shop)
	{
		return Reject(TEXT("未知奖励类型。"));
	}
	int32 NewLevel = Level;
	int32 NewExp = Experience;
	int32 NewLimit = Controller->GetShop()->RoomLimit;
	if (Reward == ECastleDirectorReward::Experience)
	{
		const int64 Total = static_cast<int64>(Experience) + Config->RewardExp;
		const int64 CandidateLevel = static_cast<int64>(Level) + Total / Config->ExpPerLevel;
		if (CandidateLevel > MAX_int32) { return Reject(TEXT("等级超过 int32 范围。")); }
		const int64 CandidateLimit = static_cast<int64>(Config->BaseRoomLimit) + (CandidateLevel - 1) * Config->RoomLimitPerLevel;
		if (CandidateLimit > MAX_int32) { return Reject(TEXT("经验或房间上限超过 int32 范围。")); }
		NewLevel = static_cast<int32>(CandidateLevel);
		NewExp = static_cast<int32>(Total % Config->ExpPerLevel);
		NewLimit = static_cast<int32>(CandidateLimit);
	}
	if (Reward == ECastleDirectorReward::Healing && (!FMath::IsFinite(Tower->MaxHealth) || Tower->MaxHealth <= 0.0f
		|| !FMath::IsFinite(Tower->CurrentHealth))) { return Reject(TEXT("主塔生命配置无效，无法回血。")); }
	TArray<TObjectPtr<ACastleDirectorNodeActor>> Candidates;
	if (!PrepareNodes(GetNextStep(), Candidates, OutReason)) { return Reject(OutReason.ToString()); }
	// 先同步恢复建造面；奖励失败时重新隐藏，避免奖励已提交后才发现建造面有问题。
	if (!Board->SetPlacementSurfacePresentationHidden(false, OutReason))
	{
		for (ACastleDirectorNodeActor* Candidate : Candidates) { Candidate->Destroy(); }
		return Reject(OutReason.ToString());
	}
	bool bCommitted = false;
	if (Reward == ECastleDirectorReward::Experience)
	{
		bCommitted = Controller->GetShop()->TrySetRoomLimit(NewLimit, OutReason);
		if (bCommitted) { Level = NewLevel; Experience = NewExp; }
	}
	else if (Reward == ECastleDirectorReward::Healing) { bCommitted = Tower->ApplyHealing(Tower->MaxHealth * Config->HealFraction); }
	else { bCommitted = Controller->GetShop()->TryOpenForReward(Controller->GetEconomy(), Board, OutReason); }
	if (!bCommitted)
	{
		for (ACastleDirectorNodeActor* Candidate : Candidates) { Candidate->Destroy(); }
		FText SurfaceFailure;
		const bool bSurfaceHidden = Board->SetPlacementSurfacePresentationHidden(true, SurfaceFailure);
		const FString RewardFailure = OutReason.IsEmpty() ? TEXT("奖励提交失败，详见日志。") : OutReason.ToString();
		return Reject(RewardFailure + (bSurfaceHidden ? FString() : TEXT("；建造面回滚失败：") + SurfaceFailure.ToString()));
	}
	bShopPermission = Reward == ECastleDirectorReward::Shop;
	// 到此奖励已经完整提交，才发布编辑阶段并让候选节点进入世界交互。
	Nodes = MoveTemp(Candidates);
	SetPhase(ECastleDirectorPhase::Editing);
	for (ACastleDirectorNodeActor* Candidate : Nodes) { Candidate->ActivateNode(); }
	ShowMessage(FText::FromString(TEXT("飞向节点进入下一关")), false);
	UE_LOG(LogMoving_Castle, Display, TEXT("奖励 %d 已提交：Lv%d，经验 %d/%d，房间上限 %d，主塔 %.2f/%.2f。"),
		static_cast<int32>(Reward), Level, Experience, Config->ExpPerLevel, Controller->GetShop()->RoomLimit, Tower->CurrentHealth, Tower->MaxHealth);
	return true;
}

void UCastleDirectorSubsystem::SetPhase(const ECastleDirectorPhase NewPhase)
{
	// 观察者只在界面和输入已与新阶段同步后收到事件，不驱动第二套流程状态。
	Phase = NewPhase;
	UpdatePresentation();
	OnDirectorPhaseChanged.Broadcast(Phase);
}

void UCastleDirectorSubsystem::HandleTowerHealthChanged(float PreviousHealth, float CurrentHealth, float Damage)
{
	if (bRunning && CurrentHealth <= 0.0f) { EnterGameOver(FText::GetEmpty()); }
}

void UCastleDirectorSubsystem::EnterGameOver(const FText& Failure)
{
	if (Phase == ECastleDirectorPhase::GameOver) { return; }
	GameOverFailure = Failure;
	PendingNode.Reset();
	Controller->GetShop()->CloseShop();
	bShopPermission = false;
	DestroyNodes();
	SetPhase(ECastleDirectorPhase::GameOver);
	if (!bAutomation && !Controller->SetPause(true))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("导演游戏结束：SetPause(true) 失败。"));
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("游戏结束，通过 %d 关。"), ClearedSteps);
}

void UCastleDirectorSubsystem::RestartCurrentMap()
{
	if (!bRunning || Phase != ECastleDirectorPhase::GameOver) { return; }
	UGameplayStatics::OpenLevel(GetWorld(), FName(*UGameplayStatics::GetCurrentLevelName(GetWorld(), true)));
}

void UCastleDirectorSubsystem::ShowMessage(const FText& Message, const bool bError)
{
	StatusMessage = Message;
	if (!bAutomation && GEngine) { GEngine->AddOnScreenDebugMessage(2026111704, 8.0f, bError ? FColor::Red : FColor::Yellow, Message.ToString()); }
}

bool UCastleDirectorSubsystem::EnsurePresentation(FText& OutReason)
{
	if (bAutomation) { return true; }
	HUDWidget = CreateWidget<UUserWidget>(Controller, Config->HUDWidgetClass);
	RewardWidget = CreateWidget<UUserWidget>(Controller, Config->RewardWidgetClass);
	GameOverWidget = CreateWidget<UUserWidget>(Controller, Config->GameOverWidgetClass);
	if (!Cast<UCastleDirectorHUDWidget>(HUDWidget) || !Cast<UCastleRewardChoiceWidget>(RewardWidget) || !Cast<UCastleGameOverWidget>(GameOverWidget)
		|| !Cast<UCastleDirectorHUDWidget>(HUDWidget)->HasRequiredBindings()
		|| !Cast<UCastleRewardChoiceWidget>(RewardWidget)->HasRequiredBindings()
		|| !Cast<UCastleGameOverWidget>(GameOverWidget)->HasRequiredBindings())
	{
		OutReason = FText::FromString(FString::Printf(TEXT("界面创建或绑定失败：HUD=%s，Reward=%s，GameOver=%s；请检查父类和必需 BindWidget 控件。"),
			*GetPathNameSafe(Config->HUDWidgetClass.Get()), *GetPathNameSafe(Config->RewardWidgetClass.Get()), *GetPathNameSafe(Config->GameOverWidgetClass.Get())));
		return false;
	}
	HUDWidget->AddToViewport(10);
	RewardWidget->AddToViewport(50);
	GameOverWidget->AddToViewport(60);
	RewardWidget->SetVisibility(ESlateVisibility::Collapsed);
	GameOverWidget->SetVisibility(ESlateVisibility::Collapsed);
	return true;
}

void UCastleDirectorSubsystem::UpdatePresentation()
{
	if (bAutomation) { return; }
	if (HUDWidget) { HUDWidget->SetVisibility(ESlateVisibility::HitTestInvisible); }
	if (RewardWidget) { RewardWidget->SetVisibility(Phase == ECastleDirectorPhase::Reward ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (GameOverWidget) { GameOverWidget->SetVisibility(Phase == ECastleDirectorPhase::GameOver ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	Controller->SetDirectorModalState(Phase == ECastleDirectorPhase::Reward, Phase == ECastleDirectorPhase::GameOver,
		Phase == ECastleDirectorPhase::GameOver ? GameOverWidget.Get() : RewardWidget.Get());
}

FText UCastleDirectorSubsystem::GetRewardLabel(const ECastleDirectorReward Reward) const
{
	if (!IsValid(Config) || !IsValid(Tower))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("导演奖励标签读取失败：配置 %s 或主塔组件 %s 已失效。"), *GetPathNameSafe(Config), *GetPathNameSafe(Tower));
		return FText::FromString(TEXT("奖励数据无效，详见错误日志。"));
	}
	if (Reward == ECastleDirectorReward::Experience)
	{
		return FText::FromString(FString::Printf(TEXT("经验 +%d（Lv%d %d/%d）"), Config->RewardExp, Level, Experience, Config->ExpPerLevel));
	}
	if (Reward == ECastleDirectorReward::Healing)
	{
		return FText::FromString(FString::Printf(TEXT("回血 +%.0f（%.0f/%.0f）"), Tower->MaxHealth * Config->HealFraction, Tower->CurrentHealth, Tower->MaxHealth));
	}
	if (Reward == ECastleDirectorReward::Shop) { return FText::FromString(TEXT("开启商店（免费换一批新商品）")); }
	UE_LOG(LogMoving_Castle, Error, TEXT("导演奖励标签读取失败：配置 %s 收到未知奖励类型 %d。"), *GetPathNameSafe(Config), static_cast<int32>(Reward));
	return FText::FromString(TEXT("奖励类型无效，详见错误日志。"));
}

FString UCastleDirectorSubsystem::GetHUDSummary() const
{
	if (!bRunning) { return FString(); }
	if (!IsValid(Config) || !IsValid(Controller) || !IsValid(Tower))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("导演 HUD 读取失败：配置 %s、控制器 %s 或主塔 %s 已失效。"),
			*GetPathNameSafe(Config), *GetPathNameSafe(Controller), *GetPathNameSafe(Tower));
		return TEXT("导演 HUD 数据无效，详见错误日志。");
	}
	if (!IsValid(Board) || !IsValid(Board->PrototypeBackpack))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("导演 HUD 房间计数失败：棋盘 %s 或其背包失效。"), *GetPathNameSafe(Board));
		return TEXT("导演 HUD 房间计数失败：棋盘或背包失效，详见日志。");
	}
	// 商店未打开时尚未绑定依赖；HUD 始终读取本局棋盘和背包的真实占用格数。
	int32 OwnedRooms = Board->GetOccupiedRoomCellCount();
	for (const FCastleBuildingDescriptor& Item : Board->PrototypeBackpack->GetItems())
	{
		OwnedRooms += ACastleBuildingBase::GetExpectedFootprintCellCount(Item.Level);
	}
	const TCHAR* PhaseText = Phase == ECastleDirectorPhase::Editing ? TEXT("编辑中") : Phase == ECastleDirectorPhase::Battle
		? TEXT("战斗中") : Phase == ECastleDirectorPhase::Reward ? TEXT("选择奖励") : TEXT("游戏结束");
	const int32 DisplayStep = Phase == ECastleDirectorPhase::Reward ? ClearedSteps : GetNextStep();
	FString Summary = FString::Printf(TEXT("%s · 第 %d 关 %s\n主塔 Lv%d · 经验 %d/%d · 房间 %d/%d\n主塔生命 %.0f/%.0f"),
		PhaseText, DisplayStep, GetNodeKindForStep(*Config, DisplayStep) == ECastleDirectorNodeKind::Elite ? TEXT("精英") : TEXT("普通"),
		Level, Experience, Config->ExpPerLevel, OwnedRooms, Controller->GetShop()->RoomLimit, Tower->CurrentHealth, Tower->MaxHealth);
	if (Phase == ECastleDirectorPhase::Battle)
	{
		int32 Enemies = 0, PendingGroups = 0;
		if (!IsValid(Combat) || !Combat->GetBattleProgress(Enemies, PendingGroups))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("导演 HUD 读取战斗进度失败：战斗系统 %s。"), *GetPathNameSafe(Combat));
			return Summary + TEXT("\n剩余敌人数读取失败，详见日志。");
		}
		Summary += FString::Printf(TEXT("\n剩余敌人 %d · 待出场组 %d"), Enemies, PendingGroups);
	}
	return Summary;
}

void UCastleDirectorSubsystem::UnbindCombatEvents()
{
	if (IsValid(Combat))
	{
		Combat->OnBattleCleared.Remove(BattleClearedHandle);
		Combat->OnBattleFailed.Remove(BattleFailedHandle);
	}
}

void UCastleDirectorSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	// 世界结束时 Mass 自己释放实体；不要在子系统析构顺序不确定时再次派发 Despawn。
	UnbindCombatEvents();
	PendingNode.Reset();
	BattleNPCSpawner = nullptr;
	bRunning = false;
	Super::OnWorldEndPlay(InWorld);
}

void UCastleDirectorSubsystem::Deinitialize()
{
	// 先移除外部生命事件，再销毁当前世界的节点和界面，避免关卡重载后遗留回调。
	if (Tower) { Tower->OnHealthChanged.RemoveDynamic(this, &UCastleDirectorSubsystem::HandleTowerHealthChanged); }
	UnbindCombatEvents();
	DestroyNodes();
	for (UUserWidget* Widget : {HUDWidget.Get(), RewardWidget.Get(), GameOverWidget.Get()}) { if (Widget) { Widget->RemoveFromParent(); } }
	if (IsValid(Controller)) { Controller->SetDirectorModalState(false, false, nullptr); }
	bRunning = false;
	Super::Deinitialize();
}

#if WITH_DEV_AUTOMATION_TESTS
void UCastleDirectorSubsystem::ConfigureForAutomation(UCastleDirectorConfigDataAsset* InConfig, ACastleBoard* InBoard,
	ACastlePlayerController* InController, UCastleMainTowerComponent* InTower)
{
	bAutomation = true;
	bStartAttempted = true;
	Config = InConfig; Board = InBoard; Controller = InController; Tower = InTower;
	bRunning = true;
	Tower->OnHealthChanged.AddUniqueDynamic(this, &UCastleDirectorSubsystem::HandleTowerHealthChanged);
	Controller->GetShop()->TrySetRoomLimit(Config->BaseRoomLimit, StatusMessage);
	SetPhase(ECastleDirectorPhase::Editing);
}

bool UCastleDirectorSubsystem::SetPhaseForAutomation(const ECastleDirectorPhase NewPhase)
{
	if (!bAutomation) { return false; }
	if (NewPhase == ECastleDirectorPhase::Reward) { ++ClearedSteps; }
	if (NewPhase == ECastleDirectorPhase::Battle) { bShopPermission = false; Controller->GetShop()->CloseShop(); DestroyNodes(); }
	if (NewPhase == ECastleDirectorPhase::GameOver) { EnterGameOver(FText::GetEmpty()); }
	else { SetPhase(NewPhase); }
	return true;
}
#endif
