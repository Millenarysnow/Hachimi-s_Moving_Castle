#include "CastleGambleAcceptanceIsland.h"

#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastleMainTowerComponent.h"
#include "CastlePlayerController.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PawnMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Items/CastleItemGrantLibrary.h"
#include "Items/CastlePendingRoomReward.h"
#include "Moving_Castle.h"
#include "TimerManager.h"
#include "UI/Items/CastleWildcardRoomPickerWidget.h"

#if !UE_BUILD_SHIPPING
namespace
{
	/** 验收场景只借用本局对象，进出位置通过独立载体保存，不写入任何正式资产。 */
	void StartGambleAcceptance(UWorld* World)
	{
		if (!World) { UE_LOG(LogMoving_Castle, Error, TEXT("CastleTestGambleSkyIsland：缺少游戏世界。")); return; }
		auto* Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
		auto* Director = World->GetSubsystem<UCastleDirectorSubsystem>();
		const auto* TowerSystem = World->GetSubsystem<UCastleMainTowerSubsystem>();
		const auto* Tower = TowerSystem ? TowerSystem->GetRegisteredMainTower() : nullptr;
		const auto* Config = Director ? Director->GetConfig() : nullptr;
		const auto ShowFailure = [Controller](const FText& Reason)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("CastleTestGambleSkyIsland：%s"), *Reason.ToString());
			if (Controller) { Controller->ShowDirectorMessage(Reason); }
		};
		if (World->WorldType != EWorldType::PIE || !Controller || !Director || !Director->IsRunning()
			|| Director->GetPhase() != ECastleDirectorPhase::Editing || !Config || !Tower || !IsValid(Tower->GetOwner())
			|| !IsValid(Controller->GetPawn()) || Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored())
		{
			ShowFailure(INVTEXT("请在 MainMap 的 PIE 编辑阶段关闭弹窗后执行赌博空岛验收命令。"));
			return;
		}
		if (TActorIterator<ACastleGambleAcceptanceIsland>(World))
		{
			ShowFailure(INVTEXT("赌博空岛验收已启动，请先使用离开传送门。"));
			return;
		}

		const AActor& TowerActor = *Tower->GetOwner();
		const FVector Origin = TowerActor.GetActorLocation() + TowerActor.GetActorForwardVector() * Config->SkyIslandSpawnDistance
			+ FVector::UpVector * Config->SkyIslandHeightOffset;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Island = World->SpawnActor<ACastleGambleAcceptanceIsland>(Origin, FRotator(0, TowerActor.GetActorRotation().Yaw, 0), Params);
		if (!Island) { ShowFailure(INVTEXT("无法生成赌博空岛验收载体。")); return; }
		FText Reason;
		const FCastleSkyIslandContext Context{Director, Controller, ECastleSkyIslandType::Gamble, Director->GetNextStep(), Config->RunSeed};
		if (!Island->Enter(Context, Config->SkyIslandInteractKey, Reason))
		{
			Island->Destroy();
			ShowFailure(Reason);
			return;
		}
		Controller->ShowDirectorMessage(FText::Format(INVTEXT("已到达赌博空岛。对准机台或离开传送门并按 {0}。"),
			Config->SkyIslandInteractKey.GetDisplayName()));
	}

	FAutoConsoleCommandWithWorld GambleAcceptanceCommand(TEXT("CastleTestGambleSkyIsland"),
		TEXT("MainMap PIE：往返赌博空岛并验收三台机台。"), FConsoleCommandWithWorldDelegate::CreateStatic(&StartGambleAcceptance));
}
#endif

bool ACastleGambleAcceptanceIsland::Enter(const FCastleSkyIslandContext& Context, const FKey& InteractKey, FText& OutReason)
{
	auto* Controller = Context.Controller.Get();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!IsValid(Pawn) || !Pawn->GetMovementComponent() || !InteractKey.IsValid())
	{
		OutReason = INVTEXT("验收传送需要有效飞行 Pawn、移动组件与交互按键。");
		return false;
	}
	if (!InitializeIsland(Context, OutReason)) { return false; }
	ReturnTransform = Pawn->GetActorTransform();
	ReturnView = Controller->GetControlRotation();
	const FTransform Arrival = GetPlayerArrivalTransform();
	if (!Pawn->TeleportTo(Arrival.GetLocation(), Arrival.Rotator()))
	{
		OutReason = INVTEXT("验收载体无法传送玩家至空岛。");
		return false;
	}
	Pawn->GetMovementComponent()->StopMovementImmediately();
	// 到达锚点相对空岛定义，显式同步飞行视角及独立验收键。
	Controller->SetControlRotation(Arrival.Rotator());
	Controller->HideDirectorEditingPresentation();
	FixtureInput = NewObject<UInputComponent>(this);
	FixtureInput->Priority = 100;
	FixtureInput->BindKey(InteractKey, IE_Pressed, this, &ACastleGambleAcceptanceIsland::Interact).bConsumeInput = true;
	Controller->PushInputComponent(FixtureInput);
	OnDepartureRequested.AddWeakLambda(this, [this]
	{
		if (bDepartureQueued) { return; }
		bDepartureQueued = true;
		// 在交互调用栈返回后才销毁载体和离开传送门。
		GetWorldTimerManager().SetTimerForNextTick(this, &ACastleGambleAcceptanceIsland::Depart);
	});
	return true;
}

void ACastleGambleAcceptanceIsland::Interact()
{
	ACastlePlayerController* Controller = GetIslandContext().Controller.Get();
	if (!Controller || Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored() || bDepartureQueued) { return; }
	FVector Start;
	FRotator View;
	Controller->GetPlayerViewPoint(Start, View);
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleGambleAcceptance), false, Controller->GetPawn());
	GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + View.Vector() * 100000.0, ECC_Visibility, Query);
	auto* Target = Cast<ICastleSkyIslandInteractable>(Hit.GetComponent());
	if (!Target || Hit.GetActor() != this)
	{
		Controller->ShowDirectorMessage(INVTEXT("请瞄准赌博空岛上的机台或离开传送门。"));
		return;
	}
	const FCastleSkyIslandInteractContext Context{this, Controller, Hit};
	FText Reason;
	if (!Target->ExecuteInteract(Context, Reason)) { Controller->ShowDirectorMessage(Reason); }
}

void ACastleGambleAcceptanceIsland::Depart()
{
	bDepartureQueued = false;
	ACastlePlayerController* Controller = GetIslandContext().Controller.Get();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!IsValid(Pawn) || !Pawn->GetMovementComponent() || !Pawn->TeleportTo(ReturnTransform.GetLocation(), ReturnTransform.Rotator()))
	{
		const FText Reason = INVTEXT("无法返回主塔，验收空岛仍然保留。");
		UE_LOG(LogMoving_Castle, Error, TEXT("验收空岛 %s：%s"), *GetPathName(), *Reason.ToString());
		if (Controller) { Controller->ShowDirectorMessage(Reason); }
		return;
	}
	Pawn->GetMovementComponent()->StopMovementImmediately();
	Controller->SetControlRotation(ReturnView);
	Destroy();
	Controller->ShowDirectorMessage(INVTEXT("已离开赌博空岛并返回主塔。"));
}

bool ACastleGambleAcceptanceIsland::PresentRoomPrize(UCastlePendingRoomReward* Reward, FText& OutReason)
{
	const auto* Director = GetIslandContext().Director.Get();
	const auto* DirectorConfig = Director ? Director->GetConfig() : nullptr;
	ACastlePlayerController* Controller = GetIslandContext().Controller.Get();
	if (Picker || !DirectorConfig || !DirectorConfig->WildcardRoomPickerClass || !Controller || !IsValid(Reward))
	{
		OutReason = INVTEXT("验收房间奖励缺少有效万能卡面板或已有弹窗。");
		return false;
	}
	auto* NewPicker = CreateWidget<UCastleWildcardRoomPickerWidget>(Controller, DirectorConfig->WildcardRoomPickerClass);
	if (!NewPicker) { OutReason = INVTEXT("无法创建验收万能卡面板。"); return false; }
	if (!NewPicker->BindReward(Reward, OutReason) || !NewPicker->BeginBoundReward(OutReason)) { return false; }
	Picker = NewPicker;
	Picker->OnPickerClosed.AddUniqueDynamic(this, &ACastleGambleAcceptanceIsland::OnPickerClosed);
	Picker->AddToViewport(57);
	Controller->SetDirectorModalState(true, false, Picker);
	return true;
}

bool ACastleGambleAcceptanceIsland::PresentBadgePrize(const UCastleBadgeDataAsset& Badge, FText& OutReason)
{
	// 正式 HUD 早已订阅此子系统，满槽后由同一个替换面板接管输入。
	auto* BadgeSubsystem = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	if (!BadgeSubsystem) { OutReason = INVTEXT("验收空岛缺少徽章子系统。"); return false; }
	bool bNeedsReplacement = false;
	return UCastleItemGrantLibrary::TryGrantBadge(&Badge, BadgeSubsystem, bNeedsReplacement, OutReason);
}

void ACastleGambleAcceptanceIsland::OnPickerClosed(bool bClaimed)
{
	ClosePicker();
}

void ACastleGambleAcceptanceIsland::ClosePicker()
{
	if (!Picker) { return; }
	Picker->OnPickerClosed.RemoveDynamic(this, &ACastleGambleAcceptanceIsland::OnPickerClosed);
	Picker->RemoveFromParent();
	Picker = nullptr;
	if (auto* Controller = GetIslandContext().Controller.Get()) { Controller->SetDirectorModalState(false, false, nullptr); }
}

void ACastleGambleAcceptanceIsland::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	if (auto* Controller = GetIslandContext().Controller.Get(); Controller && FixtureInput)
	{
		Controller->PopInputComponent(FixtureInput);
	}
	ClosePicker();
	Super::EndPlay(EndPlayReason);
}
