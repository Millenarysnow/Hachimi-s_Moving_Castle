// Copyright Epic Games, Inc. All Rights Reserved.
#include "CastleDirectorSubsystem.h"

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
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"

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
	int32 BoardCount = 0;
	for (TActorIterator<ACastleBoard> It(GetWorld()); It; ++It) { Board = *It; ++BoardCount; }
	FCastleGroundCell Ground;
	if (BoardCount != 1 || !Board->TryGetGroundCell(Board->GridOrigin, Ground))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("需要唯一且已生成地面的棋盘，当前棋盘数 %d。"), BoardCount));
		return false;
	}
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
		Node->Configure(*Config, Step, GetNodeKindForStep(*Config, Step));
		Node->FinishSpawning(Transform);
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
	if (!bRunning || Phase != ECastleDirectorPhase::Editing || !Nodes.Contains(Node) || LastNodeSelectionFrame == GFrameCounter) { return; }
	LastNodeSelectionFrame = GFrameCounter;
	UE_LOG(LogMoving_Castle, Display, TEXT("选择导演节点 %s：第 %d 关，类型 %d。"), *GetPathNameSafe(Node), GetNextStep(), static_cast<int32>(Node->GetKind()));
	UCastleNavigationSubsystem* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	const auto Path = Navigation->AcquireSharedPath();
	if (!Path || !Path->bValid || !Path->bClosedLoop)
	{
		const FString Reason = Navigation->LastFailure.IsEmpty() ? TEXT("路线未就绪") : Navigation->LastFailure.ToString();
		ShowMessage(FText::FromString(TEXT("无法开战：") + Reason), false);
		UE_LOG(LogMoving_Castle, Warning, TEXT("%s"), *StatusMessage.ToString());
		return;
	}
	ShowMessage(FText::FromString(TEXT("战斗系统尚未接入（阶段 2）")), true);
	UE_LOG(LogMoving_Castle, Error, TEXT("战斗系统尚未接入（阶段 2）"));
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
	// 节点分配是最后一个可能失败的表现准备步骤；奖励自身事务成功后不再执行可失败操作。
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
		return Reject(OutReason.IsEmpty() ? TEXT("奖励提交失败，详见日志。") : OutReason.ToString());
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
	if (!HUDWidget || !RewardWidget || !GameOverWidget)
	{
		OutReason = FText::FromString(TEXT("界面创建失败：请检查三个 WBP 类和 BindWidget 控件。"));
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
	const bool bModal = Phase == ECastleDirectorPhase::Reward || Phase == ECastleDirectorPhase::GameOver;
	if (bModalInputActive != bModal)
	{
		bModalInputActive = bModal;
		Controller->SetIgnoreMoveInput(bModal);
		Controller->SetIgnoreLookInput(bModal);
	}
	const bool bShopOpen = Controller->GetShop()->IsShopOpen();
	Controller->bShowMouseCursor = bModal || bShopOpen;
	if (Phase == ECastleDirectorPhase::GameOver)
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(GameOverWidget->TakeWidget());
		Controller->SetInputMode(Mode);
	}
	else if (bModal || bShopOpen)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		if (bModal) { Mode.SetWidgetToFocus(RewardWidget->TakeWidget()); }
		Controller->SetInputMode(Mode);
	}
	else { FInputModeGameOnly Mode; Controller->SetInputMode(Mode); }
}

FText UCastleDirectorSubsystem::GetRewardLabel(const ECastleDirectorReward Reward) const
{
	if (!Config || !Tower) { return FText::FromString(TEXT("奖励数据未就绪")); }
	if (Reward == ECastleDirectorReward::Experience)
	{
		return FText::FromString(FString::Printf(TEXT("经验 +%d（Lv%d %d/%d）"), Config->RewardExp, Level, Experience, Config->ExpPerLevel));
	}
	if (Reward == ECastleDirectorReward::Healing)
	{
		return FText::FromString(FString::Printf(TEXT("回血 +%.0f（%.0f/%.0f）"), Tower->MaxHealth * Config->HealFraction, Tower->CurrentHealth, Tower->MaxHealth));
	}
	return FText::FromString(TEXT("开启商店（免费换一批新商品）"));
}

FString UCastleDirectorSubsystem::GetHUDSummary() const
{
	if (!bRunning || !Config || !Controller || !Tower) { return FString(); }
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
	return FString::Printf(TEXT("%s · 第 %d 关 %s\n主塔 Lv%d · 经验 %d/%d · 房间 %d/%d\n主塔生命 %.0f/%.0f"),
		PhaseText, GetNextStep(), GetNodeKindForStep(*Config, GetNextStep()) == ECastleDirectorNodeKind::Elite ? TEXT("精英") : TEXT("普通"),
		Level, Experience, Config->ExpPerLevel, OwnedRooms, Controller->GetShop()->RoomLimit, Tower->CurrentHealth, Tower->MaxHealth);
}

void UCastleDirectorSubsystem::Deinitialize()
{
	// 先移除外部生命事件，再销毁当前世界的节点和界面，避免关卡重载后遗留回调。
	if (Tower) { Tower->OnHealthChanged.RemoveDynamic(this, &UCastleDirectorSubsystem::HandleTowerHealthChanged); }
	DestroyNodes();
	for (UUserWidget* Widget : {HUDWidget.Get(), RewardWidget.Get(), GameOverWidget.Get()}) { if (Widget) { Widget->RemoveFromParent(); } }
	if (bModalInputActive && IsValid(Controller)) { Controller->SetIgnoreMoveInput(false); Controller->SetIgnoreLookInput(false); }
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
