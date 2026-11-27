#include "CastlePinballNPCSkyIsland.h"

#if !UE_BUILD_SHIPPING

#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastlePlayerController.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PawnMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Moving_Castle.h"
#include "TimerManager.h"

namespace
{
	void ShowPinballAcceptanceMessage(const FText& Reason, bool bError)
	{
		if (bError) { UE_LOG(LogMoving_Castle, Error, TEXT("弹珠机验收：%s"), *Reason.ToString()); }
		else { UE_LOG(LogMoving_Castle, Display, TEXT("弹珠机验收：%s"), *Reason.ToString()); }
		if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 6.f, bError ? FColor::Red : FColor::Cyan, Reason.ToString()); }
	}

	// 验收命令仅针对单机 PIE 编辑阶段，使用本局金币和 NPC 待命池。
	FAutoConsoleCommandWithWorld CastleTestNPCPinballIslandConsoleCommand(
		TEXT("CastleTestNPCPinballIsland"), TEXT("Visit the NPC pinball sky island from MainMap PIE."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&ACastlePinballNPCSkyIsland::StartAcceptanceVisit));
}

void ACastlePinballNPCSkyIsland::StartAcceptanceVisit(UWorld* World)
{
	auto* Director = World && World->WorldType == EWorldType::PIE ? World->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
	auto* Controller = Director ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	auto* Movement = Pawn ? Pawn->GetMovementComponent() : nullptr;
	const auto* DirectorConfig = Director ? Director->GetConfig() : nullptr;
	if (!Director || !Director->IsRunning() || Director->GetPhase() != ECastleDirectorPhase::Editing
		|| World->GetNetMode() != NM_Standalone || !Controller || !Controller->IsLocalController()
		|| !Movement || Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored()
		|| !DirectorConfig || !DirectorConfig->SkyIslandInteractKey.IsValid())
	{
		ShowPinballAcceptanceMessage(INVTEXT("请在 MainMap 的单机 PIE 编辑阶段关闭其他界面后运行命令。"), true);
		return;
	}
	if (const TActorIterator<ACastlePinballNPCSkyIsland> ExistingIsland(World); ExistingIsland)
	{
		ShowPinballAcceptanceMessage(INVTEXT("当前世界已存在弹珠机空岛，请先从离开传送门退出。"), true);
		return;
	}

	// 瞄准方向只取水平分量，生成位置与主塔留有正式空岛同量级的距离。
	const FRotator IslandRotation(0.0, Controller->GetControlRotation().Yaw, 0.0);
	const FVector Origin = Pawn->GetActorLocation() + IslandRotation.Vector() * DirectorConfig->SkyIslandSpawnDistance
		+ FVector::UpVector * DirectorConfig->SkyIslandHeightOffset;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Island = World->SpawnActor<ACastlePinballNPCSkyIsland>(StaticClass(), Origin, IslandRotation, Params);
	if (!Island)
	{
		ShowPinballAcceptanceMessage(INVTEXT("无法生成弹珠机验收空岛。"), true);
		return;
	}
	FText Reason;
	const FCastleSkyIslandContext Context{Director, Controller, ECastleSkyIslandType::NPC,
		Director->GetNextStep(), DirectorConfig->RunSeed};
	if (!Island->InitializeIsland(Context, Reason))
	{
		Island->Destroy();
		ShowPinballAcceptanceMessage(Reason, true);
		return;
	}

	Island->AcceptanceReturnTransform = Pawn->GetActorTransform();
	Island->AcceptanceReturnView = Controller->GetControlRotation();
	const FTransform Arrival = Island->GetPlayerArrivalTransform();
	if (!Pawn->TeleportTo(Arrival.GetLocation(), Arrival.Rotator()))
	{
		Island->Destroy();
		ShowPinballAcceptanceMessage(INVTEXT("玩家无法传送至弹珠机验收到达点。"), true);
		return;
	}
	Movement->StopMovementImmediately();
	Controller->SetControlRotation(Arrival.Rotator());
	Island->AcceptancePawn = Pawn;
	Island->AcceptanceInput = NewObject<UInputComponent>(Island);
	// 验收 E 仅用于经典模式交互且不消费输入，让设计模式下 Pawn 仍能收到 E 上升。
	Island->AcceptanceInput->BindKey(DirectorConfig->SkyIslandInteractKey, IE_Pressed,
		Island, &ACastlePinballNPCSkyIsland::InteractForAcceptance).bConsumeInput = false;
	Controller->PushInputComponent(Island->AcceptanceInput.Get());
	Island->OnDepartureRequested.AddUObject(Island, &ACastlePinballNPCSkyIsland::QueueAcceptanceReturn);
	ShowPinballAcceptanceMessage(INVTEXT("已到达弹珠机验收场地。对准投币台交互，结束后从离开传送门返回。"), false);
}

void ACastlePinballNPCSkyIsland::InteractForAcceptance()
{
	auto* Controller = GetIslandContext().Controller.Get();
	// 设计模式已改由主交互鼠标键点击本岛物件，验收 E 回调不执行交互。
	if (Controller && Controller->GetControlMode() == ECastleControlMode::Design) { return; }
	if (!Controller || Controller->GetPawn() != AcceptancePawn.Get())
	{
		ShowPinballAcceptanceMessage(INVTEXT("弹珠机验收失去了入岛时的玩家。"), true);
		return;
	}
	FVector Start;
	FRotator Rotation;
	Controller->GetPlayerViewPoint(Start, Rotation);
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(CastlePinballAcceptance), false, Controller->GetPawn());
	GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Rotation.Vector() * 100000.0, ECC_Visibility, Query);
	auto* Target = Cast<ICastleSkyIslandInteractable>(Hit.GetComponent());
	if (Hit.GetActor() != this || !Target)
	{
		UE_LOG(LogMoving_Castle, Verbose, TEXT("弹珠机验收射线：%s %s，命中 %s。"), *Start.ToString(),
			*Rotation.ToString(), *GetPathNameSafe(Hit.GetComponent()));
		ShowPinballAcceptanceMessage(INVTEXT("请将中心准星对准投币台或离开传送门。"), false);
		return;
	}
	FText Reason;
	const FCastleSkyIslandInteractContext Context{this, Controller, Hit};
	if (!Target->ExecuteInteract(Context, Reason)) { ShowPinballAcceptanceMessage(Reason, true); }
}

void ACastlePinballNPCSkyIsland::QueueAcceptanceReturn()
{
	// 离开意图由基座发出，延迟到输入回调返回后才销毁其组件。
	if (bAcceptanceReturnQueued) { return; }
	bAcceptanceReturnQueued = true;
	GetWorldTimerManager().SetTimerForNextTick(this, &ACastlePinballNPCSkyIsland::ReturnFromAcceptance);
}

void ACastlePinballNPCSkyIsland::ReturnFromAcceptance()
{
	bAcceptanceReturnQueued = false;
	auto* Controller = GetIslandContext().Controller.Get();
	auto* Pawn = AcceptancePawn.Get();
	auto* Movement = Pawn ? Pawn->GetMovementComponent() : nullptr;
	if (!Controller || !Pawn || Controller->GetPawn() != Pawn || !Movement
		|| !Pawn->TeleportTo(AcceptanceReturnTransform.GetLocation(), AcceptanceReturnTransform.Rotator()))
	{
		ShowPinballAcceptanceMessage(INVTEXT("无法返回入岛前的位置，请检查玩家状态后重试传送门。"), true);
		return;
	}
	Movement->StopMovementImmediately();
	Controller->SetControlRotation(AcceptanceReturnView);
	ShowPinballAcceptanceMessage(INVTEXT("已离开弹珠机验收场地并返回主塔。"), false);
	Destroy();
}

#endif
