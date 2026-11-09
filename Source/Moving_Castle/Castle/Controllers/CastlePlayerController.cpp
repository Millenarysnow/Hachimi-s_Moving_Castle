// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastlePlayerController.h"

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
		if (!Shop->IsShopOpen())
		{
			UpdateGridCursor();
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
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ACastlePlayerController::ToggleShop);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ACastlePlayerController::PlacePrototypeBuildingAtCursor);
}

void ACastlePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
		GridCursor->ApplyTarget(CurrentGridCursorTarget);
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
		*Descriptor.BuildingClass->GetName(), Descriptor.Number), true);
}

void ACastlePlayerController::PlacePrototypeBuildingAtCursor()
{
	// UI 使用鼠标时绝不继续向世界发送左键放置，避免购买同时误放建筑。
	if (Shop->IsShopOpen())
	{
		return;
	}
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

void ACastlePlayerController::ToggleShop()
{
	if (Shop->IsShopOpen())
	{
		Shop->CloseShop();
		return;
	}
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

void ACastlePlayerController::HandleShopChanged()
{
	const bool bNewOpen = Shop->IsShopOpen();
	if (bNewOpen == bShopInputActive)
	{
		return;
	}
	bShopInputActive = bNewOpen;
	bShowMouseCursor = bNewOpen;
	// Ignore 标志只在状态切换时设置，避免多次购买把内部忽略计数不断叠加。
	SetIgnoreMoveInput(bNewOpen);
	SetIgnoreLookInput(bNewOpen);
	if (bNewOpen)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
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
