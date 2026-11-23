// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastlePlayerController.h"

#include "CastleBuildingDefinition.h"

#include "CastleBoard.h"
#include "CastleBackpackComponent.h"
#include "CastleBuildingBase.h"
#include "CastleEconomyComponent.h"
#include "CastleGridCursor.h"
#include "CastleShopComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"
#include "Interaction/CastlePatrolRoute.h"
#include "CastleMainTowerComponent.h"
#include "CastleNavigationSubsystem.h"
#include "CastleTowerDispatchConfigDataAsset.h"
#include "CastleTowerDispatchSubsystem.h"
#include "CastleMassEnemySpawner.h"
#include "MassEntityConfigAsset.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Support/CastleSupportFlightActors.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/UI/Items/CastleBadgeBarWidget.h"
#include "Castle/UI/Items/CastleBadgeReplaceModalWidget.h"
#include "Castle/UI/Items/CastleTempBuildBarWidget.h"
#include "Components/InputComponent.h"

ACastlePlayerController::ACastlePlayerController()
{
	// 控制器负责每帧更新视线指针，但不承担角色移动 Tick。
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Economy = CreateDefaultSubobject<UCastleEconomyComponent>(TEXT("Economy"));
	Shop = CreateDefaultSubobject<UCastleShopComponent>(TEXT("Shop"));
}

void ACastlePlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	// 屏幕中心是游戏内唯一指针，系统鼠标不再显示，也不再产生模板点击事件。
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	FInputModeGameOnly InputMode;
	InputMode.SetConsumeCaptureMouseDown(true);
	SetInputMode(InputMode);
	// 已配置关卡保留其路线 Actor；普通建造关卡自动提供一个空的玩家路径点编辑入口。
	if (!UGameplayStatics::GetActorOfClass(GetWorld(), ACastlePatrolRoute::StaticClass()))
	{
		GetWorld()->SpawnActor<ACastlePatrolRoute>();
	}

	FindActiveBoard();
	SpawnGridCursor();
	Shop->OnShopChanged.AddDynamic(this, &ACastlePlayerController::HandleShopChanged);
	FText EconomyReason;
	if (!Economy->TryAddGold(InitialGold, EconomyReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("初始金币发放失败：%s"), *EconomyReason.ToString());
	}

	int32 ViewportSizeX = 0;
	int32 ViewportSizeY = 0;
	GetViewportSize(ViewportSizeX, ViewportSizeY);
	if (ViewportSizeX > 0 && ViewportSizeY > 0)
	{
		// 将系统鼠标放到视口中心，保证首次获得焦点时射线方向稳定。
		SetMouseLocation(ViewportSizeX / 2, ViewportSizeY / 2);
	}
}

void ACastlePlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocalController())
	{
		UpdateTowerDispatchInput();
		FText EditReason;
		if (!Shop->IsShopOpen() && IsCastleEditingAllowed(EditReason))
		{
			UpdateGridCursor();
		}
		else
		{
			CurrentGridCursorTarget = FCastleGridCursorTarget();
			if (GridCursor) { GridCursor->HideCursor(); }
		}
		// 出售和放置会使数组槽位前移，空包无选中，有牌时把索引保持在有效范围。
		if (ActiveBoard != nullptr && ActiveBoard->PrototypeBackpack != nullptr)
		{
			const int32 Count = ActiveBoard->PrototypeBackpack->GetItemCount();
			SelectedBackpackIndex = Count == 0 ? INDEX_NONE : FMath::Clamp(SelectedBackpackIndex, 0, Count - 1);
		}
	}
}

void ACastlePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("CastlePlayerController 输入初始化失败：InputComponent 为空。"));
		return;
	}

	// 选择卡牌和放置属于棋盘交互，但输入入口统一放在自有控制器中，
	// 这样中心指针、鼠标捕获和后续 UI 不会被棋盘 Actor 分散管理。
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ACastlePlayerController::SelectPrototypeSlot1);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ACastlePlayerController::SelectPrototypeSlot2);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ACastlePlayerController::SelectPrototypeSlot3);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ACastlePlayerController::SelectPrototypeSlot4);
	InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ACastlePlayerController::SelectBackpackSlot5);
	InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &ACastlePlayerController::SelectBackpackSlot6);
	InputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &ACastlePlayerController::SelectBackpackSlot7);
	InputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &ACastlePlayerController::SelectBackpackSlot8);
	InputComponent->BindKey(EKeys::Nine, IE_Pressed, this, &ACastlePlayerController::SelectBackpackSlot9);
	InputComponent->BindKey(EKeys::Zero, IE_Pressed, this, &ACastlePlayerController::SelectBackpackSlot10);
	// 视口每次滚动会同时产生方向键和轴事件；只绑定方向键，避免一次滚动被处理两次。
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &ACastlePlayerController::SelectPreviousBackpackSlot);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &ACastlePlayerController::SelectNextBackpackSlot);
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ACastlePlayerController::ToggleShop);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ACastlePlayerController::PlacePrototypeBuildingAtCursor);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ACastlePlayerController::ReleaseTowerDispatchInput).bConsumeInput = false;
	// 战斗控制快捷键允许在暂停期间继续响应，确保按 T 暂停后可再次按 T 恢复或按 F/R 操作。
	InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ACastlePlayerController::ToggleBattlePauseShortcut).bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ACastlePlayerController::ToggleBattleSpeedShortcut).bExecuteWhenPaused = true;
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ACastlePlayerController::RetryBattleShortcut).bExecuteWhenPaused = true;
}

void ACastlePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld()))
	{
		Badges->OnBadgesChanged.RemoveDynamic(this, &ACastlePlayerController::HandleItemBadgeChanged);
	}
	for (UUserWidget* Widget : DirectorItemWidgets) { Widget->RemoveFromParent(); }
	DirectorItemWidgets.Reset();
	BadgeReplaceWidget = nullptr;
	for (ACastleMassEnemySpawner* Spawner : EnemyPreviewSpawners)
	{
		if (IsValid(Spawner)) { Spawner->Destroy(); }
	}
	EnemyPreviewSpawners.Reset();
	Shop->OnShopChanged.RemoveDynamic(this, &ACastlePlayerController::HandleShopChanged);
	if (GridCursor != nullptr)
	{
		GridCursor->Destroy();
		GridCursor = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void ACastlePlayerController::FindActiveBoard()
{
	if (GetWorld() == nullptr)
	{
		return;
	}

	ActiveBoard = Cast<ACastleBoard>(UGameplayStatics::GetActorOfClass(GetWorld(), ACastleBoard::StaticClass()));
	if (ActiveBoard == nullptr && !bLoggedMissingBoard)
	{
		bLoggedMissingBoard = true;
		UE_LOG(LogMoving_Castle, Error, TEXT("中心网格指针初始化失败：当前世界没有 ACastleBoard。"));
	}
}

void ACastlePlayerController::SpawnGridCursor()
{
	if (GetWorld() == nullptr || GridCursor != nullptr)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GridCursor = GetWorld()->SpawnActor<ACastleGridCursor>(ACastleGridCursor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (GridCursor == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("中心网格指针初始化失败：无法生成 ACastleGridCursor。"));
	}
}

void ACastlePlayerController::UpdateGridCursor()
{
	if (ActiveBoard == nullptr)
	{
		FindActiveBoard();
	}
	if (GridCursor == nullptr)
	{
		SpawnGridCursor();
	}
	if (ActiveBoard == nullptr || GridCursor == nullptr)
	{
		CurrentGridCursorTarget = FCastleGridCursorTarget();
		return;
	}

	int32 ViewportSizeX = 0;
	int32 ViewportSizeY = 0;
	GetViewportSize(ViewportSizeX, ViewportSizeY);
	if (ViewportSizeX <= 0 || ViewportSizeY <= 0)
	{
		CurrentGridCursorTarget = FCastleGridCursorTarget();
		GridCursor->HideCursor();
		return;
	}

	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	if (!DeprojectScreenPositionToWorld(ViewportSizeX * 0.5f, ViewportSizeY * 0.5f, RayOrigin, RayDirection))
	{
		CurrentGridCursorTarget = FCastleGridCursorTarget();
		GridCursor->HideCursor();
		return;
	}

	FCastleGridCursorTarget NewTarget;
	const FVector RayEnd = RayOrigin + RayDirection * GridCursorTraceDistance;
	if (ActiveBoard->TryResolveGridCursorTarget(RayOrigin, RayEnd, NewTarget))
	{
		CurrentGridCursorTarget = NewTarget;
		// 高亮颜色和点击建造共用棋盘校验，背包选择或命中方向变化后会在本帧重新计算。
		FCastleBuildingDescriptor Descriptor;
		FText FailureReason;
		const bool bCanPlace = ActiveBoard->PrototypeBackpack != nullptr
			&& ActiveBoard->PrototypeBackpack->TryGetItemAt(SelectedBackpackIndex, Descriptor)
			&& ActiveBoard->CanPlaceBuildingAtCursor(CurrentGridCursorTarget, Descriptor, FailureReason);
		GridCursor->ApplyTarget(CurrentGridCursorTarget, bCanPlace);
	}
	else
	{
		CurrentGridCursorTarget = FCastleGridCursorTarget();
		GridCursor->HideCursor();
	}
}

void ACastlePlayerController::SelectPrototypeSlot1()
{
	SelectBackpackSlot(0);
}

void ACastlePlayerController::SelectPrototypeSlot2()
{
	SelectBackpackSlot(1);
}

void ACastlePlayerController::SelectPrototypeSlot3()
{
	SelectBackpackSlot(2);
}

void ACastlePlayerController::SelectPrototypeSlot4()
{
	SelectBackpackSlot(3);
}

// 每个按键都转到同一个实际槽位选择入口，避免购买新牌后仍只查找四种样例牌。
void ACastlePlayerController::SelectBackpackSlot5() { SelectBackpackSlot(4); }
void ACastlePlayerController::SelectBackpackSlot6() { SelectBackpackSlot(5); }
void ACastlePlayerController::SelectBackpackSlot7() { SelectBackpackSlot(6); }
void ACastlePlayerController::SelectBackpackSlot8() { SelectBackpackSlot(7); }
void ACastlePlayerController::SelectBackpackSlot9() { SelectBackpackSlot(8); }
void ACastlePlayerController::SelectBackpackSlot10() { SelectBackpackSlot(9); }

void ACastlePlayerController::SelectPreviousBackpackSlot()
{
	StepBackpackSelection(false);
}

void ACastlePlayerController::SelectNextBackpackSlot()
{
	StepBackpackSelection(true);
}

void ACastlePlayerController::StepBackpackSelection(bool bNext)
{
	// 模态界面当下拥有滚轮；即使界面没有消费该事件，也不能切换它背后的建造卡。
	if (bModalInputActive)
	{
		return;
	}
	if (!IsValid(ActiveBoard) || !IsValid(ActiveBoard->PrototypeBackpack))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("控制器 %s 滚轮选牌失败：棋盘 %s 或其 PrototypeBackpack 无效。"),
			*GetPathName(), *GetPathNameSafe(ActiveBoard.Get()));
		return;
	}

	// 卡牌在背包内连续存放；尾部半透明键位不属于卡牌序列，无需扫描或过滤。
	const int32 Count = ActiveBoard->PrototypeBackpack->GetItemCount();
	if (Count == 0)
	{
		ShowGameplayMessage(TEXT("背包为空，没有可切换的建筑卡牌。"), false);
		return;
	}
	if (SelectedBackpackIndex < 0 || SelectedBackpackIndex >= Count)
	{
		// 同一输入帧内可能已消耗尾部卡牌。下方 Tick 仍负责同步库存，不在这里猜测新的起点。
		ShowGameplayMessage(TEXT("建造栏正在同步已变化的背包，请再滚动一次。"), false);
		return;
	}

	// 首尾没有可选的相邻卡牌。越过实际持有范围的方向不提交选择，保留当前高亮且不跳到另一端。
	const int32 TargetIndex = SelectedBackpackIndex + (bNext ? 1 : -1);
	if (TargetIndex >= 0 && TargetIndex < Count)
	{
		SelectBackpackSlot(TargetIndex);
	}
}

void ACastlePlayerController::SelectBackpackSlot(int32 SlotIndex)
{
	if (Shop->IsShopOpen())
	{
		return;
	}
	FCastleBuildingDescriptor Descriptor;
	if (ActiveBoard == nullptr || ActiveBoard->PrototypeBackpack == nullptr
		|| !ActiveBoard->PrototypeBackpack->TryGetItemAt(SlotIndex, Descriptor))
	{
		ShowGameplayMessage(TEXT("该背包槽位为空，请选择有卡牌的槽位。"), false);
		return;
	}
	SelectedBackpackIndex = SlotIndex;
	ShowGameplayMessage(FString::Printf(TEXT("已选中背包第 %d 格：%s，数字 %d。"), SlotIndex + 1,
		*Descriptor.Definition->DisplayName.ToString(), Descriptor.Number), true);
}

bool ACastlePlayerController::TryPopBalloonFromRay(const FVector& Start, const FVector& End, bool& bOutConsumed)
{
	bOutConsumed = false;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CastleBalloonClick), false, GetPawn());
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params)) { return false; }
	const ACastleSupplyBalloon* Balloon = Cast<ACastleSupplyBalloon>(Hit.GetActor());
	if (!Balloon) { return false; }
	bOutConsumed = true;
	// 即使非法落点导致投放失败，这次输入也只属于气球，不允许同一下左键误放建筑。
	return GetWorld()->GetSubsystem<UCastleSupportTowerSubsystem>()->PopBalloon(Balloon->GetTargetId());
}

void ACastlePlayerController::PlacePrototypeBuildingAtCursor()
{
	// UI 使用鼠标时绝不继续向世界发送左键放置，避免购买同时误放建筑。
	if (Shop->IsShopOpen() || bRewardInputActive || bGameOverInputActive)
	{
		return;
	}
	// 气球点击属于战斗操作，在构筑门控前消费；同一次左键不会同时放置房间。
	FVector Origin, End;
	const bool bHasRay = TryGetDispatchAim(Origin, End);
	if (bHasRay)
	{
		bool bConsumed;
		const bool bPopped = TryPopBalloonFromRay(Origin, End, bConsumed);
		if (bConsumed)
		{
			if (!bPopped) { ShowDirectorMessage(FText::FromString(TEXT("气球无法完成空投，请检查 Output Log。"))); }
			return;
		}
	}
	FText EditReason;
	const bool bCanEdit = IsCastleEditingAllowed(EditReason);
	auto* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>();
	if (Dispatch && Dispatch->IsSessionActive() && bHasRay && IsMainTowerHit(Origin, End))
	{
		FText Reason;
		bHoldingTowerDispatch = Dispatch->BeginManualDispatch(Reason);
		if (!bHoldingTowerDispatch) { ShowDirectorMessage(Reason); }
		return;
	}
	if (!bCanEdit) { ShowDirectorMessage(EditReason); return; }
	if (ActiveBoard == nullptr)
	{
		FindActiveBoard();
	}
	if (ActiveBoard == nullptr)
	{
		return;
	}

	FText FailureReason;
	ActiveBoard->PlaceBackpackBuildingAtCursor(CurrentGridCursorTarget, SelectedBackpackIndex, FailureReason);
}

bool ACastlePlayerController::TryGetDispatchAim(FVector& OutStart, FVector& OutEnd) const
{
	int32 Width = 0, Height = 0;
	GetViewportSize(Width, Height);
	FVector Direction;
	if (Width <= 0 || Height <= 0 || !DeprojectScreenPositionToWorld(Width * 0.5f, Height * 0.5f, OutStart, Direction))
	{
		return false;
	}
	OutEnd = OutStart + Direction * GridCursorTraceDistance;
	return true;
}

bool ACastlePlayerController::IsMainTowerHit(const FVector& Start, const FVector& End) const
{
	const auto* Towers = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>();
	const auto* Tower = Towers ? Towers->GetRegisteredMainTower() : nullptr;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CastleTowerDispatchClick), false, GetPawn());
	return Tower && GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) && Hit.GetActor() == Tower->GetOwner();
}

void ACastlePlayerController::ReleaseTowerDispatchInput()
{
	bHoldingTowerDispatch = false;
	if (auto* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>()) { Dispatch->StopManualDispatch(); }
}

void ACastlePlayerController::UpdateTowerDispatchInput()
{
	auto* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>();
	const auto* Config = Dispatch ? Dispatch->GetConfiguration() : nullptr;
	// 棋盘与控制器的 BeginPlay 可按任一顺序进入；仅在配置已登记后绑定一次实际按键。
	if (Config && InputComponent && !BoundTowerDispatchKey.IsValid())
	{
		BoundTowerDispatchKey = Config->ToggleAutoDispatchKey;
		InputComponent->BindKey(BoundTowerDispatchKey, IE_Pressed, this, &ACastlePlayerController::ToggleTowerAutoDispatchShortcut);
	}
	if (!bHoldingTowerDispatch) { return; }
	FVector Start, End;
	if (!Dispatch || !Dispatch->IsSessionActive() || !IsInputKeyDown(EKeys::LeftMouseButton)
		|| Shop->IsShopOpen() || bRewardInputActive || bGameOverInputActive
		|| !TryGetDispatchAim(Start, End) || !IsMainTowerHit(Start, End))
	{
		ReleaseTowerDispatchInput();
	}
}

void ACastlePlayerController::ToggleTowerAutoDispatchShortcut()
{
	if (Shop->IsShopOpen() || bRewardInputActive || bGameOverInputActive) { return; }
	auto* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>();
	if (Dispatch && Dispatch->IsSessionActive()) { Dispatch->ToggleAutoDispatch(); }
}

void ACastlePlayerController::ToggleShop()
{
	if (Shop->IsShopOpen())
	{
		Shop->CloseShop();
		return;
	}
	FText AccessReason;
	if (const UCastleDirectorSubsystem* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
		Director && !Director->IsShopAccessAllowed(AccessReason)) { ShowDirectorMessage(AccessReason); return; }
	if (ActiveBoard == nullptr)
	{
		FindActiveBoard();
	}
	FText Reason;
	if ((!Shop->IsInitialized() && !Shop->InitializeShop(Economy, ActiveBoard, Reason)) || !Shop->TryOpenShop(Reason))
	{
		ShowGameplayMessage(Reason.ToString(), false);
	}
}

void ACastlePlayerController::ToggleBattlePauseShortcut()
{
	if (Shop->IsShopOpen() || bRewardInputActive || bGameOverInputActive)
	{
		return;
	}
	if (UCastleDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr)
	{
		if (Director->IsRunning() && Director->GetPhase() == ECastleDirectorPhase::Battle)
		{
			Director->ToggleBattlePause();
		}
	}
}

void ACastlePlayerController::ToggleBattleSpeedShortcut()
{
	if (Shop->IsShopOpen() || bRewardInputActive || bGameOverInputActive)
	{
		return;
	}
	if (UCastleDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr)
	{
		if (Director->IsRunning() && Director->GetPhase() == ECastleDirectorPhase::Battle)
		{
			Director->ToggleBattleSpeed();
		}
	}
}

void ACastlePlayerController::RetryBattleShortcut()
{
	if (Shop->IsShopOpen() || bRewardInputActive || bGameOverInputActive)
	{
		return;
	}
	if (UCastleDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr)
	{
		if (Director->IsRunning() && Director->GetPhase() == ECastleDirectorPhase::Battle)
		{
			Director->RestartCurrentMap();
		}
	}
}

void ACastlePlayerController::HandleShopChanged()
{
	RefreshModalInput();
}

bool ACastlePlayerController::IsDirectorEditingAllowed(FText& OutReason) const
{
	const UCastleDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
	OutReason = FText::GetEmpty();
	return !Director || Director->IsEditingAllowed(OutReason);
}

bool ACastlePlayerController::IsCastleEditingAllowed(FText& OutReason) const
{
	FCastleBadgeDefinition PendingBadge;
	const auto* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	if (BadgeReplaceWidget && Badges && Badges->TryGetPendingBadge(PendingBadge))
	{
		OutReason = INVTEXT("选择徽章替换槽位时无法编辑。");
		return false;
	}
	const auto* Dispatch = GetWorld() ? GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>() : nullptr;
	if (Dispatch && Dispatch->IsSessionActive())
	{
		OutReason = FText::FromString(TEXT("主塔出兵会话期间无法构筑或编辑路径点。"));
		return false;
	}
	return IsDirectorEditingAllowed(OutReason);
}

void ACastlePlayerController::ShowDirectorMessage(const FText& Message) const
{
	ShowGameplayMessage(Message.ToString(), false);
}

void ACastlePlayerController::CastleDamageMainTower(const float Damage)
{
	UCastleMainTowerSubsystem* Towers = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>();
	if (!Towers || !Towers->ApplyMainTowerDamage(Damage))
	{
		ShowGameplayMessage(TEXT("无法对主塔结算伤害，请检查主塔状态与日志。"), false);
	}
}

void ACastlePlayerController::CastleUpgradeMainTower(const int32 Tier)
{
	if (!IsValid(ActiveBoard)) { FindActiveBoard(); }
	UCastleMainTowerSubsystem* Towers = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>();
	UCastleMainTowerComponent* Health = Towers ? Towers->GetRegisteredMainTower() : nullptr;
	if (!IsValid(ActiveBoard) || !IsValid(Health) || Tier < 2 || Tier > 4
		|| !ActiveBoard->TowerBalloonLimitsByTier.IsValidIndex(Tier - 1))
	{
		ShowGameplayMessage(TEXT("主塔升级验收需要已生成主塔，且等级必须为 2～4。"), false);
		return;
	}
	const int32 Count = ActiveBoard->TowerBalloonLimitsByTier[Tier - 1];
	const float BaseHealth = ActiveBoard->TowerHealthPerBalloon * Count;
	FText Reason;
	// 两部分都通过前置准备，才会向战斗与 HUD 发布升级后的生命数据。
	if (!Health->PrepareTowerBalloonTier(Count, BaseHealth, true, Reason)
		|| !ActiveBoard->ExpandMainTowerLayers(Tier * 2 + 1, Reason))
	{
		ShowDirectorMessage(Reason);
		return;
	}
	if (!Health->SetTowerBalloonTier(Count, BaseHealth, true, Reason))
	{
		ShowDirectorMessage(Reason);
		return;
	}
	ShowGameplayMessage(FString::Printf(TEXT("主塔升级到 Lv%d，开放 %d 层，补满 %d 个气球。"),
		Tier, ActiveBoard->GetUnlockedLayerCount(), Count), true);
}

void ACastlePlayerController::CastleAddPatrolWaypoints(const int32 Delta)
{
	ACastlePatrolRoute* Route = Cast<ACastlePatrolRoute>(UGameplayStatics::GetActorOfClass(GetWorld(), ACastlePatrolRoute::StaticClass()));
	if (!IsValid(Route) || !Route->AddMaxPlayerWaypoints(Delta))
	{
		ShowGameplayMessage(TEXT("无法调整草稿路点限额，请检查路线入口和数值。"), false);
		return;
	}
	ShowGameplayMessage(FString::Printf(TEXT("当前路径点上限为 %d。"), Route->GetMaxPlayerWaypoints()), true);
}

void ACastlePlayerController::CastleStartTowerDispatch()
{
	auto* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>();
	const auto* Config = Dispatch ? Dispatch->GetConfiguration() : nullptr;
	if (!Config)
	{
		ShowGameplayMessage(TEXT("当前棋盘未配置主塔出兵 Data Asset。"), false);
		return;
	}
	FText Reason;
	if (!IsDirectorEditingAllowed(Reason)) { ShowDirectorMessage(Reason); return; }
	const auto* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	if (!Navigation || Navigation->GetRequestedWaypoints().IsEmpty())
	{
		ShowGameplayMessage(TEXT("请先按 P 放置路径点，再按 Enter 提交后开始出兵。"), false);
		return;
	}
	if (!Dispatch->StartBattleDispatchSession(Config->NPCConfiguration, Config->InitialNPCCount, Reason))
	{
		ShowDirectorMessage(Reason);
		return;
	}
	ShowGameplayMessage(FString::Printf(TEXT("主塔出兵会话开始，待命 %d 名，自动出兵按键 %s。"),
		Config->InitialNPCCount, *Config->ToggleAutoDispatchKey.GetDisplayName().ToString()), true);
}

void ACastlePlayerController::CastleEndTowerDispatch()
{
	auto* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>();
	FText Reason;
	if (!Dispatch || !Dispatch->EndBattleDispatchSession(Reason)) { ShowDirectorMessage(Reason); }
}

void ACastlePlayerController::SetDirectorModalState(const bool bRewardOpen, const bool bGameOverOpen, UUserWidget* FocusWidget)
{
	// 所有模态入口共享同一商店事件处理器，重复接线不会叠加监听者。
	Shop->OnShopChanged.AddUniqueDynamic(this, &ACastlePlayerController::HandleShopChanged);
	bRewardInputActive = bRewardOpen;
	bGameOverInputActive = bGameOverOpen;
	DirectorFocusWidget = FocusWidget;
	// 界面种类接力时即使总输入锁不变，也需要刷新最终焦点和输入模式。
	RefreshModalInput(true);
}

void ACastlePlayerController::HideDirectorEditingPresentation()
{
	CurrentGridCursorTarget = FCastleGridCursorTarget();
	if (IsValid(GridCursor)) { GridCursor->HideCursor(); }
}

bool ACastlePlayerController::MountDirectorItemHUD(UCastleBackpackComponent* Backpack, FText& OutReason)
{
	UClass* TempClass = LoadClass<UCastleTempBuildBarWidget>(nullptr,
		TEXT("/Game/MovingCastle/UI/Items/WBP_TempBuildBar.WBP_TempBuildBar_C"));
	UClass* BadgeClass = LoadClass<UCastleBadgeBarWidget>(nullptr,
		TEXT("/Game/MovingCastle/UI/Items/WBP_BadgeBar.WBP_BadgeBar_C"));
	UClass* ReplaceClass = LoadClass<UCastleBadgeReplaceModalWidget>(nullptr,
		TEXT("/Game/MovingCastle/UI/Items/WBP_BadgeReplaceModal.WBP_BadgeReplaceModal_C"));
	UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	if (!TempClass || !BadgeClass || !ReplaceClass || !Badges || !IsValid(Backpack))
	{
		OutReason = INVTEXT("临时建筑栏、徽章栏或替换弹窗的 WBP、背包或徽章源缺失。");
		return false;
	}
	auto* Temp = CreateWidget<UCastleTempBuildBarWidget>(this, TempClass);
	auto* Bar = CreateWidget<UCastleBadgeBarWidget>(this, BadgeClass);
	auto* Modal = CreateWidget<UCastleBadgeReplaceModalWidget>(this, ReplaceClass);
	if (!Temp || !Bar || !Modal || !Temp->BindBackpack(Backpack) || !Bar->BindBadges(Badges) || !Modal->BindBadges(Badges))
	{
		OutReason = INVTEXT("临时建筑栏、徽章栏或替换弹窗的必需控件无法绑定。");
		return false;
	}
	// 三个资产的内部布局保留在 WBP，控制器只负责为正式视口分配锚点和层级。
	Temp->AddToViewport(20);
	Temp->SetAnchorsInViewport(FAnchors(0.0f, 0.58f));
	Temp->SetPositionInViewport(FVector2D(24.0, 0.0), false);
	Bar->AddToViewport(20);
	Bar->SetAnchorsInViewport(FAnchors(0.0f, 0.12f));
	Bar->SetPositionInViewport(FVector2D(24.0, 0.0), false);
	Modal->AddToViewport(55);
	Modal->SetAnchorsInViewport(FAnchors(0.5f, 0.5f));
	Modal->SetAlignmentInViewport(FVector2D(0.5, 0.5));
	Modal->SetPositionInViewport(FVector2D::ZeroVector, false);
	DirectorItemWidgets = {Temp, Bar, Modal};
	BadgeReplaceWidget = Modal;
	Badges->OnBadgesChanged.AddUniqueDynamic(this, &ACastlePlayerController::HandleItemBadgeChanged);
	// 无待替换项时沿用 BeginPlay 已设置的游戏输入，避免再次捕获鼠标打断当前输入帧。
	RefreshModalInput();
	return true;
}

void ACastlePlayerController::HandleItemBadgeChanged()
{
	RefreshModalInput(true);
}

bool ACastlePlayerController::BindSkyIslandInput(const FKey& Key, FText& OutReason)
{
	if (!InputComponent || !Key.IsValid())
	{
		OutReason = INVTEXT("无法绑定空岛交互键，输入组件或按键配置无效。");
		UE_LOG(LogMoving_Castle, Error, TEXT("控制器 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	InputComponent->BindKey(Key, IE_Pressed, this, &ACastlePlayerController::InteractWithSkyIsland);
	return true;
}

void ACastlePlayerController::InteractWithSkyIsland()
{
	auto* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
	if (!Director || Director->GetPhase() != ECastleDirectorPhase::SkyIsland || bModalInputActive) { return; }
	FVector Start;
	FRotator Rotation;
	GetPlayerViewPoint(Start, Rotation);
	FText Reason;
	if (!Director->TryInteractWithSkyIsland(Start, Start + Rotation.Vector() * GridCursorTraceDistance, Reason))
	{
		ShowDirectorMessage(Reason);
	}
}

void ACastlePlayerController::CastleTestSkyIsland()
{
#if !UE_BUILD_SHIPPING
	auto* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
	FText Reason;
	if (!Director || !Director->TryEnterSkyIslandFixture(Reason)) { ShowDirectorMessage(Reason); }
#endif
}

void ACastlePlayerController::RefreshModalInput(const bool bForceRefresh)
{
	FCastleBadgeDefinition PendingBadge;
	const auto* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	// 使用库存真相源，避免依赖多个动态委托监听者的刷新顺序。
	const bool bBadgeReplaceOpen = BadgeReplaceWidget && Badges && Badges->TryGetPendingBadge(PendingBadge);
	const bool bNewOpen = Shop->IsShopOpen() || bRewardInputActive || bGameOverInputActive || bBadgeReplaceOpen;
	// 普通商店的购买/刷新通知不改变开关，沿用旧行为，不重设模式或抢回焦点。
	if (bNewOpen == bModalInputActive && !bForceRefresh) { return; }
	if (bNewOpen != bModalInputActive)
	{
		bModalInputActive = bNewOpen;
		// 只由本方法持有一份忽略计数；奖励和商店交接绝不各自重复增减。
		SetIgnoreMoveInput(bNewOpen);
		SetIgnoreLookInput(bNewOpen);
	}
	bShowMouseCursor = bNewOpen;
	// 游戏结束会在本帧暂停世界，必须立即清理目标，不能等下一帧控制器 Tick。
	FText EditReason;
	if (bNewOpen || !IsCastleEditingAllowed(EditReason))
	{
		CurrentGridCursorTarget = FCastleGridCursorTarget();
		if (GridCursor) { GridCursor->HideCursor(); }
	}
	if (bGameOverInputActive)
	{
		FInputModeUIOnly Mode;
		if (DirectorFocusWidget) { Mode.SetWidgetToFocus(DirectorFocusWidget->TakeWidget()); }
		SetInputMode(Mode);
	}
	else if (bNewOpen)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		if (bBadgeReplaceOpen) { Mode.SetWidgetToFocus(BadgeReplaceWidget->TakeWidget()); }
		else if (bRewardInputActive && DirectorFocusWidget) { Mode.SetWidgetToFocus(DirectorFocusWidget->TakeWidget()); }
		SetInputMode(Mode);
		CurrentGridCursorTarget = FCastleGridCursorTarget();
		if (GridCursor != nullptr)
		{
			GridCursor->HideCursor();
		}
	}
	else
	{
		FInputModeGameOnly Mode;
		Mode.SetConsumeCaptureMouseDown(true);
		SetInputMode(Mode);
	}
}

void ACastlePlayerController::ShowGameplayMessage(const FString& Message, bool bSuccess) const
{
	UE_LOG(LogMoving_Castle, Log, TEXT("城堡操作：%s"), *Message);
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(20261108, 5.0f, bSuccess ? FColor::Cyan : FColor::Yellow, Message);
	}
}

void ACastlePlayerController::CastleAddGold(int32 Amount)
{
	FText Reason;
	const bool bSuccess = Economy->TryAddGold(Amount, Reason);
	ShowGameplayMessage(bSuccess ? FString::Printf(TEXT("已增加 %d 金币，余额 %d。"), Amount, Economy->GetGold()) : Reason.ToString(), bSuccess);
}

void ACastlePlayerController::CastleSpendGold(int32 Amount)
{
	FText Reason;
	const bool bSuccess = Economy->TrySpendGold(Amount, Reason);
	ShowGameplayMessage(bSuccess ? FString::Printf(TEXT("已扣除 %d 金币，余额 %d。"), Amount, Economy->GetGold()) : Reason.ToString(), bSuccess);
}

void ACastlePlayerController::CastleSpawnEnemy(const FString& EnemyType, int32 WaveCount)
{
	const auto Reject = [this](const FString& Message)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("CastleSpawnEnemy：%s"), *Message);
		ShowGameplayMessage(Message, false);
	};
	// 名称就是资产契约中的四个公开兵种，不按未知输入猜测配置。
	if (EnemyType != TEXT("LavaGoliath") && EnemyType != TEXT("LavaPup")
		&& EnemyType != TEXT("MiasmaWelp") && EnemyType != TEXT("ShadowSwarm"))
	{
		Reject(TEXT("未知类型。用法：CastleSpawnEnemy LavaGoliath|LavaPup|MiasmaWelp|ShadowSwarm [正整数波数]"));
		return;
	}
	if (WaveCount <= 0 || (EnemyType == TEXT("ShadowSwarm") && WaveCount > MAX_int32 / 8))
	{
		Reject(TEXT("波数必须是正整数，且总实体数量不得溢出。"));
		return;
	}
	FVector Target;
	UCastleMainTowerSubsystem* Main = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>();
	if (!Main || !Main->GetMainTowerTargetLocation(Target))
	{
		Reject(TEXT("当前世界没有存活主塔，无法在外圈圆环生成敌人。"));
		return;
	}
	const FString AssetPath = TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_") + EnemyType;
	UMassEntityConfigAsset* Configuration = LoadObject<UMassEntityConfigAsset>(nullptr, *AssetPath);
	if (!Configuration)
	{
		Reject(FString::Printf(TEXT("敌人资产 %s 缺失或加载失败。"), *AssetPath));
		return;
	}
	// 延迟生成 Actor，先设置禁用 BeginPlay 自动生成，再完成生命周期，避免误刷默认测试兵。
	ACastleMassEnemySpawner* Spawner = GetWorld()->SpawnActorDeferred<ACastleMassEnemySpawner>(
		ACastleMassEnemySpawner::StaticClass(), FTransform::Identity, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Spawner)
	{
		Reject(TEXT("无法创建敌人批量生成器。"));
		return;
	}
	Spawner->bSpawnOnBeginPlay = false;
	Spawner->Configuration = Configuration;
	Spawner->Count = WaveCount;
	Spawner->InnerRadius = 1800.0f;
	Spawner->OuterRadius = 2300.0f;
	Spawner->RandomSeed = FMath::Rand();
	if (EnemyType == TEXT("ShadowSwarm"))
	{
		Spawner->MinClusterCount = 6;
		Spawner->MaxClusterCount = 8;
		Spawner->ClusterSpreadRadius = 120.0f;
	}
	Spawner->FinishSpawning(FTransform::Identity);
	if (!Spawner->SpawnUnits())
	{
		Reject(FString::Printf(TEXT("%s 批量生成失败，详见日志。"), *AssetPath));
		Spawner->Destroy();
		return;
	}
	EnemyPreviewSpawners.Add(Spawner);
	ShowGameplayMessage(FString::Printf(TEXT("已生成 %s：%d 波，%d 只，主塔外圈 1800~2300 cm。"),
		*EnemyType, WaveCount, Spawner->SpawnedUnits.Num()), true);
}
