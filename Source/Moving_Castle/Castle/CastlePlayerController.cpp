// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastlePlayerController.h"

#include "CastleBoard.h"
#include "CastleGridCursor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"

ACastlePlayerController::ACastlePlayerController()
{
	// 控制器负责每帧更新视线指针，但不承担角色移动 Tick。
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
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
		UpdateGridCursor();
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
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ACastlePlayerController::PlacePrototypeBuildingAtCursor);
}

void ACastlePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
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
	if (ActiveBoard == nullptr)
	{
		FindActiveBoard();
	}
	if (ActiveBoard != nullptr)
	{
		ActiveBoard->SelectPrototypeSlot(0);
	}
}

void ACastlePlayerController::SelectPrototypeSlot2()
{
	if (ActiveBoard == nullptr)
	{
		FindActiveBoard();
	}
	if (ActiveBoard != nullptr)
	{
		ActiveBoard->SelectPrototypeSlot(1);
	}
}

void ACastlePlayerController::SelectPrototypeSlot3()
{
	if (ActiveBoard == nullptr)
	{
		FindActiveBoard();
	}
	if (ActiveBoard != nullptr)
	{
		ActiveBoard->SelectPrototypeSlot(2);
	}
}

void ACastlePlayerController::SelectPrototypeSlot4()
{
	if (ActiveBoard == nullptr)
	{
		FindActiveBoard();
	}
	if (ActiveBoard != nullptr)
	{
		ActiveBoard->SelectPrototypeSlot(3);
	}
}

void ACastlePlayerController::PlacePrototypeBuildingAtCursor()
{
	if (ActiveBoard == nullptr)
	{
		FindActiveBoard();
	}
	if (ActiveBoard == nullptr)
	{
		return;
	}

	FText FailureReason;
	ActiveBoard->PlacePrototypeBuildingAtCursor(CurrentGridCursorTarget, FailureReason);
}
