// Copyright Epic Games, Inc. All Rights Reserved.
#include "CastleDirectorSubsystem.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/Items/CastleItemGrantLibrary.h"
#include "Castle/Items/CastlePendingRoomReward.h"
#include "Castle/UI/Director/CastleTowerUpgradeRewardWidget.h"
#include "Castle/UI/Items/CastleWildcardRoomPickerWidget.h"
#include "Castle/SkyIslands/CastleSkyIslandBase.h"
#include "Castle/SkyIslands/CastleSkyIslandTestFixture.h"

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
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Units/Mass/CastleMassUnitSubsystem.h"
#include "Units/Navigation/CastleTowerDispatchSubsystem.h"
#include "Units/Navigation/Interaction/CastlePatrolRoute.h"
#include "MassEntitySubsystem.h"
#include "MassEntityConfigAsset.h"
#include "UObject/StrongObjectPtr.h"

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
	if (bDepartureRequested)
	{
		// 离开委托只记录意图，在交互调用栈返回后才可销毁传送门所属 Actor。
		bDepartureRequested = false;
		FText Failure;
		if (!TryDepartSkyIsland(Failure)) { ShowMessage(Failure, true); }
	}
	if (PendingNode.IsValid())
	{
		// 回调可能由 Mass 处理中的碰撞触发；直到世界子系统 Tick 才消费唯一选择。
		ACastleDirectorNodeActor* Selected = PendingNode.Get();
		PendingNode.Reset();
		FText Reason;
		if (Selected->GetKind() == ECastleDirectorNodeKind::RewardIsland || Selected->GetKind() == ECastleDirectorNodeKind::RestIsland)
		{
			if (!TryEnterSkyIsland(*Selected, *Config, Reason)) { ShowMessage(Reason, true); }
		}
		else { TryStartSelectedBattle(Selected, Reason); }
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
		for (UUserWidget* Widget : {HUDWidget.Get(), RewardWidget.Get(), GameOverWidget.Get(), static_cast<UUserWidget*>(TowerUpgradeWidget.Get())}) { if (Widget) { Widget->RemoveFromParent(); } }
		HUDWidget = nullptr;
		RewardWidget = nullptr;
		GameOverWidget = nullptr;
		TowerUpgradeWidget = nullptr;
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
	PatrolRoute = Cast<ACastlePatrolRoute>(UGameplayStatics::GetActorOfClass(GetWorld(), ACastlePatrolRoute::StaticClass()));
	auto* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>();
	const FCastleTowerLevelConfig& Initial = Config->TowerLevels[0];
	if (!IsValid(PatrolRoute) || !Dispatch || Board->GetUnlockedLayerCount() != Initial.UnlockedLayers)
	{
		OutReason = INVTEXT("正式关卡缺少路线、出兵子系统或初始主塔层数与 Lv1 成长表不符。");
		return false;
	}
	if (!Dispatch->Configure(Config->DispatchConfig, OutReason)
		|| !Controller->BindSkyIslandInput(Config->SkyIslandInteractKey, OutReason)
		|| !PatrolRoute->SetMaxPlayerWaypoints(Config->InitialPathPointLimit)) { return false; }
	if (const TArray<FVector> Points = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->GetRequestedWaypoints(); !Points.IsEmpty())
	{
		if (!GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->RequestTowerDispatchRoute(Points, OutReason)) { return false; }
	}
	// 候选节点保持隐藏且无碰撞，只有所有启动依赖准备成功后才一起激活。
	TArray<TObjectPtr<ACastleDirectorNodeActor>> Candidates;
	if (!PrepareNodes(1, Candidates, OutReason)) { return false; }
	if (!Controller->GetShop()->TrySetRoomLimit(Initial.RoomLimit, OutReason)
		|| !Controller->GetShop()->TrySetOfferNumberRange(1, Initial.MaxShopNumber, OutReason)
		|| !Tower->SetTowerBalloonTier(Initial.BalloonCount, Initial.BaseMaxHealth, true, OutReason)
		|| !Controller->MountDirectorItemHUD(Board->PrototypeBackpack, OutReason))
	{
		for (ACastleDirectorNodeActor* Node : Candidates) { Node->Destroy(); }
		return false;
	}
	Level = 1;
	RunNPCCount = Config->InitialNPCCount;
	Experience = 0;
	ClearedSteps = 0;
	bShopPermission = false;
	bShopPermissionBeforeIsland = false;
	bRunning = true;
	BattleClearedHandle = Combat->OnBattleCleared.AddUObject(this, &UCastleDirectorSubsystem::HandleBattleCleared);
	BattleFailedHandle = Combat->OnBattleFailed.AddUObject(this, &UCastleDirectorSubsystem::HandleBattleFailed);
	Controller->GetShop()->CloseShop();
	Nodes = MoveTemp(Candidates);
	Tower->OnHealthChanged.AddUniqueDynamic(this, &UCastleDirectorSubsystem::HandleTowerHealthChanged);
	if (UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld()))
	{
		Badges->OnBadgesChanged.AddUniqueDynamic(this, &UCastleDirectorSubsystem::HandleBadgesChanged);
		HandleBadgesChanged();
	}
	SetPhase(ECastleDirectorPhase::Editing);
	for (ACastleDirectorNodeActor* Node : Nodes) { Node->ActivateNode(); }
	// 下一关指引由 WBP HUD 读取，避免同一句话叠加到屏幕调试层。
	StatusMessage = FText::FromString(TEXT("飞向节点进入下一关"));
	UE_LOG(LogMoving_Castle, Display, TEXT("导演启动：编辑阶段，第 1 关，主塔 Lv1，房间上限 %d。"), Initial.RoomLimit);
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

TArray<ECastleDirectorNodeKind> UCastleDirectorSubsystem::GetNodeKindsForStep(
	const UCastleDirectorConfigDataAsset& InConfig, const int32 Step)
{
	// 节点数与抽样共用同一个确定性流，所有平台按同一顺序消费。
	const uint32 Seed = static_cast<uint32>(InConfig.RunSeed) ^ (static_cast<uint32>(Step) * 0x9e3779b9u);
	FRandomStream Stream(static_cast<int32>(Seed));
	const int32 Count = Stream.RandRange(InConfig.NodeCountMin, InConfig.NodeCountMax);
	const ECastleDirectorNodeKind First = GetNodeKindForStep(InConfig, Step);
	TArray<ECastleDirectorNodeKind> Kinds;
	if (Step == 1 || First == ECastleDirectorNodeKind::Elite)
	{
		Kinds.Init(First, Count);
		return Kinds;
	}
	Kinds.Add(First);
	int32 RestWeight = InConfig.RestNodeWeight;
	const int64 FightOrRewardWeight = static_cast<int64>(InConfig.NormalNodeWeight) + InConfig.RewardNodeWeight;
	while (Kinds.Num() < Count)
	{
		const int64 TotalWeight = FightOrRewardWeight + RestWeight;
		if (TotalWeight <= 0)
		{
			// 配置无法填满全部节点时，交由生成事务拒绝发布节点批次。
			return {};
		}
		const double Draw = static_cast<double>(Stream.GetFraction()) * TotalWeight;
		if (Draw < InConfig.NormalNodeWeight) { Kinds.Add(ECastleDirectorNodeKind::Normal); }
		else if (Draw < FightOrRewardWeight) { Kinds.Add(ECastleDirectorNodeKind::RewardIsland); }
		else
		{
			Kinds.Add(ECastleDirectorNodeKind::RestIsland);
			RestWeight = 0;
		}
	}
	return Kinds;
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
	const TArray<ECastleDirectorNodeKind> Kinds = GetNodeKindsForStep(*Config, Step);
	const int32 Count = Kinds.Num();
	if (Count < 2)
	{
		OutReason = FText::Format(INVTEXT("配置 {0} 无法为第 {1} 步抽满节点。"), FText::FromString(Config->GetPathName()), Step);
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
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
		if (!Node->Configure(*Config, Step, Kinds[Index]))
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
	if (!bRunning || (Phase == ECastleDirectorPhase::Editing && UpgradeRewards.IsEmpty() && !WildcardPicker)) { return true; }
	if (Phase == ECastleDirectorPhase::Editing)
	{
		OutReason = INVTEXT("请先结束本次主塔升级领奖。");
		return false;
	}
	if (Phase == ECastleDirectorPhase::SkyIsland)
	{
		OutReason = INVTEXT("在空岛停留期间不能编辑主塔。");
		return false;
	}
	OutReason = FText::FromString(Phase == ECastleDirectorPhase::Battle ? TEXT("战斗中无法编辑")
		: Phase == ECastleDirectorPhase::Reward ? TEXT("选择奖励时无法编辑") : TEXT("游戏结束后无法编辑"));
	return false;
}

bool UCastleDirectorSubsystem::IsShopAccessAllowed(FText& OutReason) const
{
	if (Phase == ECastleDirectorPhase::SkyIsland)
	{
		OutReason = INVTEXT("请通过空岛商店交互打开房间商店。");
		return false;
	}
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
	if (!bRunning || Phase != ECastleDirectorPhase::Editing || !UpgradeRewards.IsEmpty() || WildcardPicker
		|| !Nodes.Contains(Node) || PendingNode.IsValid() || LastNodeSelectionFrame == GFrameCounter) { return; }
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
	if (Node->GetKind() != ECastleDirectorNodeKind::Normal && Node->GetKind() != ECastleDirectorNodeKind::Elite)
	{
		OutReason = INVTEXT("空岛节点不能作为战斗节点启动。");
		UE_LOG(LogMoving_Castle, Error, TEXT("%s %s"), *Node->GetPathName(), *OutReason.ToString());
		return false;
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("选择导演节点 %s：第 %d 关，类型 %d。"), *GetPathNameSafe(Node), GetNextStep(), static_cast<int32>(Node->GetKind()));
	UCastleNavigationSubsystem* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	const auto Path = Navigation ? Navigation->AcquireSharedPath() : nullptr;
	if (!Path || !Path->bValid || Path->bClosedLoop || !Path->bTowerDispatch || Path->Length <= UE_DOUBLE_SMALL_NUMBER)
	{
		const FString Reason = !Navigation || Navigation->LastFailure.IsEmpty() ? TEXT("正式门到门开环路线未就绪") : Navigation->LastFailure.ToString();
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
	auto* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>();
	if (!IsValid(Board) || !IsValid(Controller) || !IsValid(Combat) || !IsValid(Config) || !Dispatch || Dispatch->IsSessionActive())
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
	// 会话只登记待命池。玩家点击主塔或开启自动出兵后，NPC 才沿门到门快照派出。
	const bool bNPCsReady = Dispatch->StartBattleDispatchSession(Config->NPCConfiguration, RunNPCCount, Failure);
	if (!bNPCsReady || !Combat->StartBattle(Kind, PreviousCount + 1, Failure))
	{
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
	UE_LOG(LogMoving_Castle, Display, TEXT("导演开战提交：第 %d 关，本类第 %d 场，门到门待命池 %d 个 NPC。"), GetNextStep(), ActiveBattleOrdinal, RunNPCCount);
	return true;
}

bool UCastleDirectorSubsystem::RecallBattleNPCs(FText& OutReason)
{
	auto* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>();
	auto* Support = GetWorld()->GetSubsystem<UCastleSupportTowerSubsystem>();
	if (!Dispatch || !Support)
	{
		OutReason = INVTEXT("缺少出兵或支援塔子系统，无法回收本场 NPC。");
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	if (!Support->EndBattleSupport())
	{
		OutReason = INVTEXT("支援塔的本场增援或飞行对象未能回收。");
		return false;
	}
	return !Dispatch->IsSessionActive() || Dispatch->EndBattleDispatchSession(OutReason);
}

bool UCastleDirectorSubsystem::TryEnterSkyIsland(ACastleDirectorNodeActor& Node,
	const UCastleDirectorConfigDataAsset& IslandConfig, FText& OutReason)
{
	const int32 NodeIndex = Nodes.IndexOfByKey(&Node);
	if (NodeIndex == INDEX_NONE
		|| (Node.GetKind() != ECastleDirectorNodeKind::RewardIsland && Node.GetKind() != ECastleDirectorNodeKind::RestIsland))
	{
		OutReason = INVTEXT("当前节点批次中没有该空岛节点。");
		UE_LOG(LogMoving_Castle, Error, TEXT("导演配置 %s 空岛节点 %s 进入失败：%s"),
			*IslandConfig.GetPathName(), *Node.GetPathName(), *OutReason.ToString());
		return false;
	}
	// 仅用本局种子、关数和节点序号决策；拒绝进入或再次选择不推进随机流。
	const uint32 Seed = static_cast<uint32>(IslandConfig.RunSeed) ^ (static_cast<uint32>(GetNextStep()) * 0x9e3779b9u)
		^ (static_cast<uint32>(NodeIndex) * 0x85ebca6bu);
	FRandomStream Random(static_cast<int32>(Seed));
	const auto Type = Node.GetKind() == ECastleDirectorNodeKind::RestIsland ? ECastleSkyIslandType::Rest
		: static_cast<ECastleSkyIslandType>(Random.RandRange(0, 2));
	return TryEnterConfiguredSkyIsland(IslandConfig, Type, Random.GetCurrentSeed(), &Node, OutReason);
}

bool UCastleDirectorSubsystem::TryEnterConfiguredSkyIsland(const UCastleDirectorConfigDataAsset& IslandConfig,
	const ECastleSkyIslandType Type, const int32 Seed, const ACastleDirectorNodeActor* Node, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const FText TypeName = StaticEnum<ECastleSkyIslandType>()->GetDisplayNameTextByValue(static_cast<int64>(Type));
	const auto Reject = [&](const FText& Reason)
	{
		OutReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("导演配置 %s 空岛类型 %s，入口 %s：%s"),
			*IslandConfig.GetPathName(), *TypeName.ToString(), Node ? *Node->GetPathName() : TEXT("CastleEnterSkyIsland"),
			*Reason.ToString());
		return false;
	};
	// 待领奖励或已激活的空岛仍占用单局事务，任何入口都必须先通过同一组编辑阶段检查。
	APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	if (!bRunning || Phase != ECastleDirectorPhase::Editing || !UpgradeRewards.IsEmpty() || WildcardPicker
		|| !IsValid(Pawn) || !Pawn->GetMovementComponent() || !IsValid(Board) || !IsValid(Tower) || !IsValid(Tower->GetOwner())
		|| !IsValid(PatrolRoute) || ActiveSkyIsland)
	{
		return Reject(INVTEXT("只有可操作的编辑阶段且玩家、主塔与路线有效时才能进入空岛。"));
	}
	UClass* Class = IslandConfig.SkyIslandClasses.FindRef(Type).Get();
	if (!IsValid(Class) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return Reject(FText::Format(INVTEXT("空岛类型 {0} 尚未配置具体玩法类，或已登记的类无效：{1}。"),
			TypeName, FText::FromString(GetPathNameSafe(Class))));
	}
	// 所有玩法依赖由空岛初始化验证。准备失败的岛立即销毁，玩家和当前节点留在主塔。
	const FVector Origin = Tower->GetOwner()->GetActorLocation() + Board->GetActorForwardVector() * IslandConfig.SkyIslandSpawnDistance
		+ FVector::UpVector * IslandConfig.SkyIslandHeightOffset;
	const FRotator Rotation(0, Board->GetActorRotation().Yaw, 0);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACastleSkyIslandBase* Island = GetWorld()->SpawnActor<ACastleSkyIslandBase>(Class, Origin, Rotation, Params);
	if (!IsValid(Island)) { return Reject(INVTEXT("已登记的空岛类无法生成。")); }
	const FCastleSkyIslandContext Context{this, Controller.Get(), Type, GetNextStep(), Seed};
	if (!Island->InitializeIsland(Context, OutReason) || !Board->SetPlacementSurfacePresentationHidden(true, OutReason))
	{
		Island->Destroy();
		return Reject(OutReason);
	}
	const FTransform Arrival = Island->GetPlayerArrivalTransform();
	const FTransform ReturnTransform = Pawn->GetActorTransform();
	const FRotator ReturnControlRotation = Controller->GetControlRotation();
	if (!Pawn->TeleportTo(Arrival.GetLocation(), Arrival.Rotator()))
	{
		Island->Destroy();
		FText RestoreReason;
		if (!Board->SetPlacementSurfacePresentationHidden(false, RestoreReason)) { return Reject(RestoreReason); }
		return Reject(INVTEXT("玩家无法传送至空岛到达锚点。"));
	}
	// 传送成功后再提交返塔信息，并移除已离开编辑阶段的节点及其待处理重叠。
	IslandReturnTransform = ReturnTransform;
	IslandReturnControlRotation = ReturnControlRotation;
	Pawn->GetMovementComponent()->StopMovementImmediately();
	Controller->SetControlRotation(Arrival.Rotator());
	// 只在入岛传送成功后保存主塔权限；初始化或传送失败不改变进岛前的值。
	bShopPermissionBeforeIsland = bShopPermission;
	Controller->GetShop()->CloseShop();
	bShopPermission = false;
	PatrolRoute->CancelEditingDraft();
	Controller->HideDirectorEditingPresentation();
	ActiveSkyIsland = Island;
	bIslandShopGranted = false;
	Island->OnDepartureRequested.AddWeakLambda(this, [this] { bDepartureRequested = true; });
	PendingNode.Reset();
	DestroyNodes();
	SetPhase(ECastleDirectorPhase::SkyIsland);
	ShowMessage(FText::Format(INVTEXT("已到达空岛。瞄准物件并按 {0} 交互。"), IslandConfig.SkyIslandInteractKey.GetDisplayName()), false);
	UE_LOG(LogMoving_Castle, Display, TEXT("导演进入空岛 %s，第 %d 步。"), *Island->GetPathName(), GetNextStep());
	return true;
}

bool UCastleDirectorSubsystem::TryDepartSkyIsland(FText& OutReason)
{
	if (!ValidateIslandAction(ActiveSkyIsland, OutReason)) { return false; }
	APawn* Pawn = Controller->GetPawn();
	if (!IsValid(Pawn) || !Pawn->GetMovementComponent() || GetNextStep() >= MAX_int32)
	{
		OutReason = INVTEXT("玩家 Pawn 或下一步编号无效，无法离岛。");
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	// 玩家仍在岛上时准备下一批节点；任何准备失败都保留当前空岛和步数。
	TArray<TObjectPtr<ACastleDirectorNodeActor>> Candidates;
	if (!PrepareNodes(GetNextStep() + 1, Candidates, OutReason)) { return false; }
	if (!Board->SetPlacementSurfacePresentationHidden(false, OutReason))
	{
		for (ACastleDirectorNodeActor* Candidate : Candidates) { Candidate->Destroy(); }
		return false;
	}
	if (!Pawn->TeleportTo(IslandReturnTransform.GetLocation(), IslandReturnTransform.Rotator()))
	{
		for (ACastleDirectorNodeActor* Candidate : Candidates) { Candidate->Destroy(); }
		if (!Board->SetPlacementSurfacePresentationHidden(true, OutReason)) { return false; }
		OutReason = INVTEXT("无法传送回主塔视角位置。");
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	Pawn->GetMovementComponent()->StopMovementImmediately();
	Controller->SetControlRotation(IslandReturnControlRotation);
	Controller->GetShop()->CloseShop();
	ActiveSkyIsland->Destroy();
	ActiveSkyIsland = nullptr;
	// 返塔成功才丢弃本次访问的锚点与临时商店领取状态。
	IslandReturnTransform = FTransform::Identity;
	IslandReturnControlRotation = FRotator::ZeroRotator;
	bIslandShopGranted = false;
	++ClearedSteps;
	Nodes = MoveTemp(Candidates);
	// 所有离岛校验已完成。先恢复权限再广播编辑阶段，让界面读取同一份已提交状态。
	bShopPermission = bShopPermissionBeforeIsland;
	bShopPermissionBeforeIsland = false;
	SetPhase(ECastleDirectorPhase::Editing);
	for (ACastleDirectorNodeActor* Candidate : Nodes) { Candidate->ActivateNode(); }
	ShowMessage(INVTEXT("已离开空岛，飞向节点进入下一步。"), false);
	UE_LOG(LogMoving_Castle, Display, TEXT("导演离开空岛，已完成 %d 步。"), ClearedSteps);
	return true;
}

bool UCastleDirectorSubsystem::ValidateIslandAction(const ACastleSkyIslandBase* Island, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (Phase != ECastleDirectorPhase::SkyIsland || !IsValid(Island) || Island != ActiveSkyIsland || !IsValid(Controller))
	{
		OutReason = INVTEXT("空岛交互只接受当前单局激活的空岛。");
		UE_LOG(LogMoving_Castle, Error, TEXT("空岛 %s：%s"), *GetPathNameSafe(Island), *OutReason.ToString());
		return false;
	}
	return true;
}

bool UCastleDirectorSubsystem::TryInteractWithSkyIsland(const FVector& Start, const FVector& End, FText& OutReason)
{
	if (!ValidateIslandAction(ActiveSkyIsland, OutReason)) { return false; }
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CastleSkyIslandInteract), false, Controller->GetPawn());
	GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
	ICastleSkyIslandInteractable* Target = Cast<ICastleSkyIslandInteractable>(Hit.GetComponent());
	if (!Target) { Target = Cast<ICastleSkyIslandInteractable>(Hit.GetActor()); }
	if (!Target)
	{
		OutReason = INVTEXT("请将中心准星对准可交互的空岛物件。");
		return false;
	}
	const FCastleSkyIslandInteractContext Context{ActiveSkyIsland.Get(), Controller.Get(), Hit};
	return Target->CanInteract(Context, OutReason) && Target->ExecuteInteract(Context, OutReason);
}

bool UCastleDirectorSubsystem::RequestIslandRoomShop(ACastleSkyIslandBase* Island, FText& OutReason)
{
	if (!ValidateIslandAction(Island, OutReason)) { return false; }
	if (bIslandShopGranted)
	{
		OutReason = INVTEXT("这座空岛的免费商店批次已领取。");
		UE_LOG(LogMoving_Castle, Error, TEXT("空岛 %s：%s"), *Island->GetPathName(), *OutReason.ToString());
		return false;
	}
	if (!Controller->GetShop()->TryOpenForReward(Controller->GetEconomy(), Board, OutReason)) { return false; }
	bIslandShopGranted = true;
	return true;
}

bool UCastleDirectorSubsystem::RequestIslandRoomReward(ACastleSkyIslandBase* Island,
	UCastlePendingRoomReward* Reward, FText& OutReason)
{
	return ValidateIslandAction(Island, OutReason) && OpenWildcardPicker(Reward, OutReason);
}

bool UCastleDirectorSubsystem::RequestIslandBadgeReward(ACastleSkyIslandBase* Island,
	const FCastleBadgeDefinition& Badge, FText& OutReason)
{
	if (!ValidateIslandAction(Island, OutReason)) { return false; }
	auto* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	if (!Badges)
	{
		OutReason = INVTEXT("当前 GameInstance 缺少徽章子系统。");
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	bool bNeedsReplacement = false;
	return Badges->TryAcquireBadge(Badge, bNeedsReplacement, OutReason);
}

#if !UE_BUILD_SHIPPING
bool UCastleDirectorSubsystem::TryEnterSkyIslandByTypeForDebug(const ECastleSkyIslandType Type, FText& OutReason)
{
	if (!IsValid(Config))
	{
		const FString ConfigPath = GetDefault<UCastleDirectorSettings>()->DirectorConfig.ToString();
		OutReason = FText::FromString(FString::Printf(TEXT("正式导演配置 %s 未加载。"), *ConfigPath));
		UE_LOG(LogMoving_Castle, Error, TEXT("CastleEnterSkyIsland：%s"), *OutReason.ToString());
		return false;
	}
	// 调试入口无需场上有奖励节点，按本局种子和步数生成稳定的玩法种子。
	const uint32 Seed = static_cast<uint32>(Config->RunSeed) ^ (static_cast<uint32>(GetNextStep()) * 0x9e3779b9u);
	return TryEnterConfiguredSkyIsland(*Config, Type, static_cast<int32>(Seed), nullptr, OutReason);
}

bool UCastleDirectorSubsystem::TryEnterSkyIslandFixture(FText& OutReason)
{
	ACastleDirectorNodeActor* Selected = nullptr;
	for (ACastleDirectorNodeActor* Node : Nodes)
	{
		if (Node->GetKind() == ECastleDirectorNodeKind::RewardIsland || Node->GetKind() == ECastleDirectorNodeKind::RestIsland)
		{
			Selected = Node;
			break;
		}
	}
	if (!Selected)
	{
		OutReason = INVTEXT("本步没有空岛节点。先进行战斗，直到节点标签显示空岛或休息。");
		UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	TStrongObjectPtr<UCastleDirectorConfigDataAsset> FixtureConfig(DuplicateObject(Config.Get(), GetTransientPackage()));
	for (const auto Type : {ECastleSkyIslandType::Shop, ECastleSkyIslandType::NPC, ECastleSkyIslandType::Gamble, ECastleSkyIslandType::Rest})
	{
		FixtureConfig->SkyIslandClasses.Add(Type, ACastleSkyIslandTestFixture::StaticClass());
	}
	return TryEnterSkyIsland(*Selected, *FixtureConfig, OutReason);
}
#endif

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
	// 战斗结束时立即恢复 1x 倍速与未暂停状态，避免奖励和编辑阶段沿用战斗倍速。
	ResetBattleControls(true);
	++ClearedSteps;
	if (Kind == ECastleBattleKind::Elite)
	{
		// 解锁只随精英胜利次数推进，战斗开始和失败均不改变普通池。
		const FName UnlockTag = Ordinal == 1 ? FName(TEXT("Unlock.MiasmaWelp")) :
			Ordinal == 2 ? FName(TEXT("Unlock.LavaGoliath")) : NAME_None;
		if ((!UnlockTag.IsNone() && !Combat->UnlockEnemyTag(UnlockTag)) || !TryCommitRewards(true, true, true, Failure))
		{
			EnterGameOver(FText::Format(INVTEXT("精英胜利奖励提交失败。{0}"), Failure));
			return;
		}
		const FText AllGranted = INVTEXT("精英通关奖励已全部获得：经验、主塔回血、免费换批并开放商店");
		ShowMessage(AllGranted, false);
		UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *AllGranted.ToString());
		return;
	}
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
	if (Phase != ECastleDirectorPhase::Reward
		|| (Reward != ECastleDirectorReward::Experience && Reward != ECastleDirectorReward::Healing && Reward != ECastleDirectorReward::Shop))
	{
		OutReason = INVTEXT("当前阶段或奖励类型无效。");
		UE_LOG(LogMoving_Castle, Error, TEXT("导演 %s 无法领取奖励：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	return TryCommitRewards(Reward == ECastleDirectorReward::Experience, Reward == ECastleDirectorReward::Healing,
		Reward == ECastleDirectorReward::Shop, OutReason);
}

bool UCastleDirectorSubsystem::TryCommitRewards(const bool bExperience, const bool bHealing, const bool bShop, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const auto Reject = [this, &OutReason](const FText& Reason)
	{
		OutReason = Reason;
		ShowMessage(Reason, true);
		UE_LOG(LogMoving_Castle, Error, TEXT("导演奖励失败，配置 %s：%s"), *GetPathNameSafe(Config), *Reason.ToString());
		return false;
	};
	if (!bRunning || bApplyingReward || !IsValid(Config) || !IsValid(Board) || !IsValid(Controller)
		|| !IsValid(Tower) || !Tower->IsAlive() || !UpgradeRewards.IsEmpty() || !Config->TowerLevels.IsValidIndex(Level - 1))
	{
		return Reject(INVTEXT("奖励提交缺少有效配置、棋盘、控制器或存活主塔，或上一次升级领奖尚未结束。"));
	}
	TGuardValue<bool> Guard(bApplyingReward, true);
	if (!Config->ValidateTowerLevels(OutReason) || Config->ExpPerLevel < 1 || Config->RewardExp < 1)
	{
		return Reject(OutReason.IsEmpty() ? INVTEXT("经验配置必须为正数。") : OutReason);
	}
	const int64 TotalExp = static_cast<int64>(Experience) + (bExperience ? Config->RewardExp : 0);
	const int32 NewLevel = static_cast<int32>(FMath::Min<int64>(Config->TowerLevels.Num(), Level + TotalExp / Config->ExpPerLevel));
	const int32 NewExp = NewLevel == Config->TowerLevels.Num() ? 0 : TotalExp % Config->ExpPerLevel;
	const auto& Target = Config->TowerLevels[NewLevel - 1];
	const bool bLevelUp = NewLevel > Level;
	if (bHealing && (!FMath::IsFinite(Tower->MaxHealth) || Tower->MaxHealth <= 0.0f
		|| !FMath::IsFinite(Tower->CurrentHealth) || !FMath::IsFinite(Config->HealFraction)
		|| Config->HealFraction <= 0.0f || Config->HealFraction > 1.0f))
	{
		return Reject(INVTEXT("主塔生命或回血比例无效。"));
	}
	TArray<FCastleTowerUpgradeRewardEntry> PendingRows;
	TArray<TStrongObjectPtr<UCastlePendingRoomReward>> PendingRooms;
	// 跨级时逐级累积赠送，万能卡的数字使用最终已解锁范围。
	for (int32 Tier = Level + 1; Tier <= NewLevel; ++Tier)
	{
		const auto& Row = Config->TowerLevels[Tier - 1];
		if (Row.PathPointReward > 0)
		{
			PendingRows.Add({ECastleTowerUpgradeReward::PathPoint, Row.PathPointReward});
			PendingRooms.Emplace(nullptr);
		}
		if (Row.NPCReward > 0)
		{
			PendingRows.Add({ECastleTowerUpgradeReward::NPC, Row.NPCReward});
			PendingRooms.Emplace(nullptr);
		}
		for (int32 Card = 0; Card < Row.CustomRoomReward; ++Card)
		{
			FCastleWildcardRoomSpec Spec;
			Spec.DefaultMaxNumber = Target.MaxShopNumber;
			UCastlePendingRoomReward* Pending = nullptr;
			if (!UCastleItemGrantLibrary::TryCreateWildcardReward(INVTEXT("完全自定义房间"), Spec,
				Board->PrototypeBackpack, Board->BuildingDefinitions, Pending, OutReason))
			{
				return Reject(OutReason);
			}
			PendingRows.Add({ECastleTowerUpgradeReward::CustomRoom, 1});
			PendingRooms.Emplace(Pending);
		}
	}
	UCastleShopComponent* Shop = Controller->GetShop();
	TStrongObjectPtr<UCastleShopComponent> PreparedShop;
	if (bShop)
	{
		PreparedShop.Reset(NewObject<UCastleShopComponent>());
		if (!Shop->PrepareRewardShop(Controller->GetEconomy(), Board, Target.MaxShopNumber, *PreparedShop, OutReason))
		{
			return Reject(OutReason);
		}
	}
	TArray<TObjectPtr<ACastleDirectorNodeActor>> Candidates;
	if (!PrepareNodes(GetNextStep(), Candidates, OutReason)) { return Reject(OutReason); }
	const auto RejectWithNodes = [&](const FText& Reason)
	{
		for (ACastleDirectorNodeActor* Candidate : Candidates) { Candidate->Destroy(); }
		return Reject(Reason);
	};
	if (!Board->SetPlacementSurfacePresentationHidden(false, OutReason)) { return RejectWithNodes(OutReason); }
	// 气球表现预留完成后扩层，所有可能拒绝的资源操作必须发生在数值提交之前。
	if (bLevelUp && (!Tower->PrepareTowerBalloonTier(Target.BalloonCount, Target.BaseMaxHealth, true, OutReason)
		|| !Board->ExpandMainTowerLayers(Target.UnlockedLayers, OutReason)))
	{
		Tower->DiscardPreparedBalloonTier();
		const FText Failure = OutReason;
		FText RestoreReason;
		if (!Board->SetPlacementSurfacePresentationHidden(true, RestoreReason))
		{
			return RejectWithNodes(FText::Format(INVTEXT("{0} 建造面恢复失败。{1}"), Failure, RestoreReason));
		}
		return RejectWithNodes(Failure);
	}
	Level = NewLevel;
	Experience = NewExp;
	if (bLevelUp)
	{
		// 成长配置和气球表现已经准备；提交时的同步通知读取新等级及新商品上限。
		Shop->TrySetRoomLimit(Target.RoomLimit, OutReason);
		Shop->TrySetOfferNumberRange(1, Target.MaxShopNumber, OutReason);
		Tower->CommitPreparedBalloonTier(Target.BalloonCount, Target.BaseMaxHealth, true);
	}
	if (bHealing) { Tower->ApplyHealing(Tower->MaxHealth * Config->HealFraction); }
	if (bExperience && Level == Config->TowerLevels.Num())
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("主塔 Lv%d 已满级。"), Level);
	}
	UpgradeRewards = MoveTemp(PendingRows);
	for (const auto& Pending : PendingRooms) { UpgradeRoomRewards.Add(Pending.Get()); }
	if (TowerUpgradeWidget) { TowerUpgradeWidget->RefreshRewards(UpgradeRewards); }
	bShopPermission = bShop;
	if (PreparedShop.IsValid()) { Shop->CommitRewardShop(*PreparedShop); }
	Nodes = MoveTemp(Candidates);
	SetPhase(ECastleDirectorPhase::Editing);
	for (ACastleDirectorNodeActor* Candidate : Nodes) { Candidate->ActivateNode(); }
	StatusMessage = INVTEXT("飞向节点进入下一关");
	UE_LOG(LogMoving_Castle, Display, TEXT("奖励已提交：Lv%d，经验 %d/%d，房间上限 %d，主塔 %.2f/%.2f。"),
		Level, Experience, Config->ExpPerLevel, Shop->RoomLimit, Tower->CurrentHealth, Tower->MaxHealth);
	return true;
}

bool UCastleDirectorSubsystem::AddRunNPCCount(const int32 Delta, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!bRunning || Delta <= 0 || Delta > MAX_int32 - RunNPCCount)
	{
		OutReason = INVTEXT("NPC 待命池需要有效单局与正数增量，且不能溢出。");
		UE_LOG(LogMoving_Castle, Error, TEXT("导演 %s 拒绝增加 %d 名 NPC：%s"), *GetPathName(), Delta, *OutReason.ToString());
		return false;
	}
	if (Phase == ECastleDirectorPhase::Battle && !GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>()->AddReserveNPCs(Delta))
	{
		OutReason = INVTEXT("当前出兵会话拒绝增加 NPC。");
		return false;
	}
	RunNPCCount += Delta;
	UE_LOG(LogMoving_Castle, Display, TEXT("单局待命池增加 %d 名普通 NPC，共 %d 名。"), Delta, RunNPCCount);
	return true;
}

void UCastleDirectorSubsystem::ClaimTowerUpgradeReward(const int32 Index)
{
	if (!UpgradeRewards.IsValidIndex(Index) || WildcardPicker || Phase != ECastleDirectorPhase::Editing) { return; }
	const FCastleTowerUpgradeRewardEntry Entry = UpgradeRewards[Index];
	FText Reason;
	switch (Entry.Kind)
	{
	case ECastleTowerUpgradeReward::PathPoint:
		if (!PatrolRoute || !PatrolRoute->AddMaxPlayerWaypoints(Entry.Count))
		{
			ShowMessage(INVTEXT("路径点奖励无法提交，请检查路线入口及路径点上限。"), true);
			return;
		}
		break;
	case ECastleTowerUpgradeReward::NPC:
		if (!AddRunNPCCount(Entry.Count, Reason)) { ShowMessage(Reason, true); return; }
		break;
	case ECastleTowerUpgradeReward::CustomRoom:
		ActiveUpgradeRewardIndex = Index;
		if (!OpenWildcardPicker(UpgradeRoomRewards[Index], Reason))
		{
			ActiveUpgradeRewardIndex = INDEX_NONE;
			ShowMessage(Reason, true);
		}
		return;
	}
	UpgradeRewards.RemoveAt(Index);
	UpgradeRoomRewards.RemoveAt(Index);
	if (TowerUpgradeWidget) { TowerUpgradeWidget->RefreshRewards(UpgradeRewards); }
	UpdatePresentation();
}

bool UCastleDirectorSubsystem::OpenWildcardPicker(UCastlePendingRoomReward* Reward, FText& OutReason)
{
	if (WildcardPicker || !IsValid(Reward) || !Config->WildcardRoomPickerClass)
	{
		OutReason = INVTEXT("已有万能卡弹窗，或万能卡奖励及其 WBP 无效。");
		UE_LOG(LogMoving_Castle, Error, TEXT("导演 %s 无法显示万能卡：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	auto* Picker = CreateWidget<UCastleWildcardRoomPickerWidget>(Controller, Config->WildcardRoomPickerClass);
	if (!Picker || !Picker->BindReward(Reward, OutReason) || !Picker->BeginBoundReward(OutReason)) { return false; }
	Picker->OnPickerClosed.AddUniqueDynamic(this, &UCastleDirectorSubsystem::HandleWildcardClosed);
	WildcardPicker = Picker;
	Picker->AddToViewport(57);
	UpdatePresentation();
	return true;
}

void UCastleDirectorSubsystem::HandleWildcardClosed(const bool bClaimed)
{
	// 用户取消只关属性弹窗。确认后才删掉升级列表里的实际条目。
	if (WildcardPicker)
	{
		WildcardPicker->OnPickerClosed.RemoveDynamic(this, &UCastleDirectorSubsystem::HandleWildcardClosed);
		WildcardPicker->RemoveFromParent();
		WildcardPicker = nullptr;
	}
	if (bClaimed && UpgradeRewards.IsValidIndex(ActiveUpgradeRewardIndex))
	{
		UpgradeRewards.RemoveAt(ActiveUpgradeRewardIndex);
		UpgradeRoomRewards.RemoveAt(ActiveUpgradeRewardIndex);
		if (TowerUpgradeWidget) { TowerUpgradeWidget->RefreshRewards(UpgradeRewards); }
	}
	ActiveUpgradeRewardIndex = INDEX_NONE;
	UpdatePresentation();
}

void UCastleDirectorSubsystem::DiscardTowerUpgradeRewards()
{
	if (WildcardPicker || UpgradeRewards.IsEmpty()) { return; }
	UE_LOG(LogMoving_Castle, Display, TEXT("主塔升级面板关闭，丢弃 %d 个未领取项。"), UpgradeRewards.Num());
	UpgradeRewards.Reset();
	UpgradeRoomRewards.Reset();
	if (TowerUpgradeWidget) { TowerUpgradeWidget->RefreshRewards(UpgradeRewards); }
	UpdatePresentation();
}

void UCastleDirectorSubsystem::SetPhase(const ECastleDirectorPhase NewPhase)
{
	// 观察者只在界面和输入已与新阶段同步后收到事件，不驱动第二套流程状态。
	if (NewPhase == ECastleDirectorPhase::Battle && Phase != NewPhase)
	{
		// 各场战斗独立触发，上一场伤害不会进入本场的射速计算。
		bFirstTowerDamageInBattle = false;
	}
	Phase = NewPhase;
	UpdatePresentation();
	OnDirectorPhaseChanged.Broadcast(Phase);
}

void UCastleDirectorSubsystem::HandleTowerHealthChanged(float PreviousHealth, float CurrentHealth, float Damage)
{
	if (Phase == ECastleDirectorPhase::Battle && Damage > 0.0f && !bFirstTowerDamageInBattle)
	{
		bFirstTowerDamageInBattle = true;
		UE_LOG(LogMoving_Castle, Display, TEXT("本场主塔首次受伤，攻击塔和召唤物可应用已持有的首次掉血徽章。"));
	}
	if (const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld()))
	{
		if (UCastleMassUnitSubsystem* Mass = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>())
		{
			Mass->SetGlobalEnemyAttackSpeedMultiplier(Badges->GetEnemyAttackSpeedMultiplier(CurrentHealth / Tower->MaxHealth));
		}
	}
	if (bRunning && CurrentHealth <= 0.0f) { EnterGameOver(FText::GetEmpty()); }
}

void UCastleDirectorSubsystem::HandleBadgesChanged()
{
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	if (!IsValid(Tower) || !Badges || !Tower->SetMaxHealthMultiplier(Badges->GetMainTowerHealthMultiplier()))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("导演无法同步主塔 %s 的徽章生命倍率。"), *GetPathNameSafe(Tower));
		return;
	}
	// 低血攻速徽章可独立改变，主塔生命上限相同也必须更新敌人倍率。
	HandleTowerHealthChanged(Tower->CurrentHealth, Tower->CurrentHealth, 0.0f);
}

int32 UCastleDirectorSubsystem::GetCurrentMaxShopNumber() const
{
	return Controller->GetShop()->GetMaxOfferNumber();
}

void UCastleDirectorSubsystem::EnterGameOver(const FText& Failure)
{
	if (Phase == ECastleDirectorPhase::GameOver) { return; }
	// 恢复 1x 全局时间缩放并清掉战斗暂停标志；游戏结束界面自身随后负责保持世界暂停。
	ResetBattleControls(false);
	GameOverFailure = Failure;
	if (Phase == ECastleDirectorPhase::Battle)
	{
		FText RecallReason;
		if (!RecallBattleNPCs(RecallReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("游戏结束时无法回收 NPC：%s"), *RecallReason.ToString());
		}
	}
	PendingNode.Reset();
	if (ActiveSkyIsland) { ActiveSkyIsland->Destroy(); ActiveSkyIsland = nullptr; }
	bDepartureRequested = false;
	Controller->GetShop()->CloseShop();
	bShopPermission = false;
	bShopPermissionBeforeIsland = false;
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
	// 游戏结束和战斗界面右下角重试按钮共用同一重开入口。
	if (!bRunning || (Phase != ECastleDirectorPhase::GameOver && Phase != ECastleDirectorPhase::Battle)) { return; }
	FText RecallReason;
	if (Phase == ECastleDirectorPhase::Battle && !RecallBattleNPCs(RecallReason))
	{
		ShowMessage(RecallReason, true);
		return;
	}
	ResetBattleControls(true);
	bShopPermissionBeforeIsland = false;
	UGameplayStatics::OpenLevel(GetWorld(), FName(*UGameplayStatics::GetCurrentLevelName(GetWorld(), true)));
}

bool UCastleDirectorSubsystem::ToggleBattlePause()
{
	return SetBattlePaused(!bBattlePaused);
}

bool UCastleDirectorSubsystem::SetBattlePaused(const bool bInPaused)
{
	if (!bRunning || Phase != ECastleDirectorPhase::Battle || !IsValid(Controller))
	{
		return false;
	}
	if (bBattlePaused == bInPaused)
	{
		return true;
	}
	if (!bAutomation && !Controller->SetPause(bInPaused))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("导演战斗暂停切换失败：SetPause(%s) 返回 false。"), bInPaused ? TEXT("true") : TEXT("false"));
		return false;
	}
	bBattlePaused = bInPaused;
	OnBattleControlStateChanged.Broadcast();
	UE_LOG(LogMoving_Castle, Display, TEXT("导演战斗暂停状态切换为：%s。"), bBattlePaused ? TEXT("暂停") : TEXT("继续"));
	return true;
}

bool UCastleDirectorSubsystem::ToggleBattleSpeed()
{
	return SetBattleDoubleSpeed(!bBattleDoubleSpeed);
}

bool UCastleDirectorSubsystem::SetBattleDoubleSpeed(const bool bInDoubleSpeed)
{
	if (!bRunning || Phase != ECastleDirectorPhase::Battle || !GetWorld())
	{
		return false;
	}
	if (bBattleDoubleSpeed == bInDoubleSpeed)
	{
		return true;
	}
	bBattleDoubleSpeed = bInDoubleSpeed;
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), bBattleDoubleSpeed ? 2.0f : 1.0f);
	OnBattleControlStateChanged.Broadcast();
	UE_LOG(LogMoving_Castle, Display, TEXT("导演战斗倍速切换为：%s。"), bBattleDoubleSpeed ? TEXT("2x") : TEXT("1x"));
	return true;
}

void UCastleDirectorSubsystem::ResetBattleControls(const bool bClearWorldPause)
{
	const bool bChanged = bBattlePaused || bBattleDoubleSpeed;
	if (bClearWorldPause && bBattlePaused && !bAutomation && IsValid(Controller))
	{
		Controller->SetPause(false);
	}
	if (bBattleDoubleSpeed && GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.0f);
	}
	bBattlePaused = false;
	bBattleDoubleSpeed = false;
	if (bChanged)
	{
		OnBattleControlStateChanged.Broadcast();
	}
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
	TowerUpgradeWidget = CreateWidget<UCastleTowerUpgradeRewardWidget>(Controller, Config->TowerUpgradeRewardWidgetClass);
	UCastleDirectorHUDWidget* DirectorHUD = Cast<UCastleDirectorHUDWidget>(HUDWidget);
	if (!DirectorHUD || !Cast<UCastleRewardChoiceWidget>(RewardWidget) || !Cast<UCastleGameOverWidget>(GameOverWidget)
		|| !TowerUpgradeWidget || !TowerUpgradeWidget->BindDirector(*this)
		|| !DirectorHUD->HasRequiredBindings()
		|| !Cast<UCastleRewardChoiceWidget>(RewardWidget)->HasRequiredBindings()
		|| !Cast<UCastleGameOverWidget>(GameOverWidget)->HasRequiredBindings())
	{
		OutReason = FText::FromString(FString::Printf(TEXT("界面创建或绑定失败：HUD=%s，Reward=%s，GameOver=%s；请检查父类、必需控件和图标贴图。"),
			*GetPathNameSafe(Config->HUDWidgetClass.Get()), *GetPathNameSafe(Config->RewardWidgetClass.Get()), *GetPathNameSafe(Config->GameOverWidgetClass.Get())));
		return false;
	}
	// 先用已注册主塔初始化生命栏，再让 WBP 进入视口；生命不依赖首帧 Slate Tick。
	if (!DirectorHUD->BindMainTowerHealth(Tower))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("HUD %s 无法绑定主塔 %s。"),
			*GetPathNameSafe(Config->HUDWidgetClass.Get()), *GetPathNameSafe(Tower)));
		return false;
	}
	HUDWidget->AddToViewport(10);
	RewardWidget->AddToViewport(50);
	GameOverWidget->AddToViewport(60);
	TowerUpgradeWidget->AddToViewport(54);
	RewardWidget->SetVisibility(ESlateVisibility::Collapsed);
	GameOverWidget->SetVisibility(ESlateVisibility::Collapsed);
	TowerUpgradeWidget->SetVisibility(ESlateVisibility::Collapsed);
	return true;
}

void UCastleDirectorSubsystem::UpdatePresentation()
{
	if (bAutomation) { return; }
	const bool bUpgradeOpen = Phase == ECastleDirectorPhase::Editing && !UpgradeRewards.IsEmpty() && !WildcardPicker;
	const bool bRewardOpen = Phase == ECastleDirectorPhase::Reward || bUpgradeOpen || WildcardPicker;
	const bool bModalPhase = bRewardOpen || Phase == ECastleDirectorPhase::GameOver;
	// 正常游玩阶段根容器采用 SelfHitTestInvisible，空白区域不阻挡准星，鼠标可见时仍可点击 HUD 按钮；模态阶段禁止穿透到底层按钮。
	if (HUDWidget) { HUDWidget->SetVisibility(bModalPhase ? ESlateVisibility::HitTestInvisible : ESlateVisibility::SelfHitTestInvisible); }
	if (RewardWidget) { RewardWidget->SetVisibility(Phase == ECastleDirectorPhase::Reward ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (GameOverWidget) { GameOverWidget->SetVisibility(Phase == ECastleDirectorPhase::GameOver ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (TowerUpgradeWidget) { TowerUpgradeWidget->SetVisibility(bUpgradeOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	UUserWidget* Focus = RewardWidget;
	if (bUpgradeOpen) { Focus = TowerUpgradeWidget; }
	if (WildcardPicker) { Focus = WildcardPicker; }
	if (Phase == ECastleDirectorPhase::GameOver) { Focus = GameOverWidget; }
	Controller->SetDirectorModalState(bRewardOpen, Phase == ECastleDirectorPhase::GameOver, Focus);
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
	if (!IsValid(Config) || !IsValid(Controller))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("导演 HUD 读取失败：配置 %s 或控制器 %s 已失效。"),
			*GetPathNameSafe(Config), *GetPathNameSafe(Controller));
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
		? TEXT("战斗中") : Phase == ECastleDirectorPhase::Reward ? TEXT("选择奖励")
		: Phase == ECastleDirectorPhase::SkyIsland ? TEXT("空岛") : TEXT("游戏结束");
	const int32 DisplayStep = Phase == ECastleDirectorPhase::Reward ? ClearedSteps : GetNextStep();
	// 生命在独立的 WBP 行中与爱心图标对齐，不再混在摘要里。
	FString Summary = FString::Printf(TEXT("%s · 第 %d 关 %s\n主塔 Lv%d · 经验 %d/%d · 房间 %d/%d"),
		PhaseText, DisplayStep, GetNodeKindForStep(*Config, DisplayStep) == ECastleDirectorNodeKind::Elite ? TEXT("精英") : TEXT("普通"),
		Level, Experience, Config->ExpPerLevel, OwnedRooms, Controller->GetShop()->RoomLimit);
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
	ResetBattleControls(false);
	UnbindCombatEvents();
	PendingNode.Reset();
	bShopPermissionBeforeIsland = false;
	bRunning = false;
	Super::OnWorldEndPlay(InWorld);
}

void UCastleDirectorSubsystem::Deinitialize()
{
	// 先移除外部生命事件，再销毁当前世界的节点和界面，避免关卡重载后遗留回调。
	ResetBattleControls(false);
	if (Tower) { Tower->OnHealthChanged.RemoveDynamic(this, &UCastleDirectorSubsystem::HandleTowerHealthChanged); }
	if (UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld()))
	{
		Badges->OnBadgesChanged.RemoveDynamic(this, &UCastleDirectorSubsystem::HandleBadgesChanged);
	}
	UnbindCombatEvents();
	if (ActiveSkyIsland) { ActiveSkyIsland->Destroy(); ActiveSkyIsland = nullptr; }
	bShopPermissionBeforeIsland = false;
	DestroyNodes();
	for (UUserWidget* Widget : {HUDWidget.Get(), RewardWidget.Get(), GameOverWidget.Get(),
		static_cast<UUserWidget*>(TowerUpgradeWidget.Get()), static_cast<UUserWidget*>(WildcardPicker.Get())})
	{
		if (Widget) { Widget->RemoveFromParent(); }
	}
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
	RunNPCCount = Config->InitialNPCCount;
	PatrolRoute = Cast<ACastlePatrolRoute>(UGameplayStatics::GetActorOfClass(GetWorld(), ACastlePatrolRoute::StaticClass()));
	Controller->GetShop()->TrySetRoomLimit(Config->TowerLevels[0].RoomLimit, StatusMessage);
	Controller->GetShop()->TrySetOfferNumberRange(1, Config->TowerLevels[0].MaxShopNumber, StatusMessage);
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
