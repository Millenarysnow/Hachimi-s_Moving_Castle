// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleFlightPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"

ACastleFlightPawn::ACastleFlightPawn()
{
	// 飞行移动需要每帧读取按键状态，但不需要额外的游戏逻辑 Tick。
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	// 不让角色网格或碰撞体自动跟随移动方向旋转，视角方向由控制器单独管理。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	MovementComponent->SetMovementMode(MOVE_Flying);
	MovementComponent->GravityScale = 0.0f;
	MovementComponent->MaxFlySpeed = FlightSpeed;
	MovementComponent->MaxAcceleration = FlightAcceleration;
	MovementComponent->BrakingDecelerationFlying = FlightBrakingDeceleration;
	MovementComponent->AirControl = 1.0f;

	// 相机臂长度为 0，玩家从角色眼睛位置观察世界，屏幕中心天然就是指针。
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 0.0f;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 64.0f);

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// 默认不显示模板人形网格，避免第一人称视角看到自己的头部占位模型。
	GetMesh()->SetOwnerNoSee(true);
}

void ACastleFlightPawn::BeginPlay()
{
	Super::BeginPlay();

	// 关卡实例或蓝图可能保存了旧的行走参数，BeginPlay 再次统一到飞行状态。
	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	MovementComponent->SetMovementMode(MOVE_Flying);
	MovementComponent->GravityScale = 0.0f;
	MovementComponent->MaxFlySpeed = FlightSpeed;
	MovementComponent->MaxAcceleration = FlightAcceleration;
	MovementComponent->BrakingDecelerationFlying = FlightBrakingDeceleration;

	CameraBoom->TargetArmLength = 0.0f;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 64.0f);
}

void ACastleFlightPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyFlightMovement();
}

void ACastleFlightPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (PlayerInputComponent == nullptr)
	{
		return;
	}

	// 直接绑定键盘按键，避免依赖第三人称模板的 Enhanced Input 资产和蓝图变量。
	PlayerInputComponent->BindKey(EKeys::W, IE_Pressed, this, &ACastleFlightPawn::MoveForwardPressed);
	PlayerInputComponent->BindKey(EKeys::W, IE_Released, this, &ACastleFlightPawn::MoveForwardReleased);
	PlayerInputComponent->BindKey(EKeys::S, IE_Pressed, this, &ACastleFlightPawn::MoveBackwardPressed);
	PlayerInputComponent->BindKey(EKeys::S, IE_Released, this, &ACastleFlightPawn::MoveBackwardReleased);
	PlayerInputComponent->BindKey(EKeys::D, IE_Pressed, this, &ACastleFlightPawn::MoveRightPressed);
	PlayerInputComponent->BindKey(EKeys::D, IE_Released, this, &ACastleFlightPawn::MoveRightReleased);
	PlayerInputComponent->BindKey(EKeys::A, IE_Pressed, this, &ACastleFlightPawn::MoveLeftPressed);
	PlayerInputComponent->BindKey(EKeys::A, IE_Released, this, &ACastleFlightPawn::MoveLeftReleased);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ACastleFlightPawn::AscendPressed);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &ACastleFlightPawn::AscendReleased);
	PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Pressed, this, &ACastleFlightPawn::DescendPressed);
	PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Released, this, &ACastleFlightPawn::DescendReleased);

	// 鼠标轴在控制器中锁定到视口中心，轴值只表示相对移动量。
	PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &ACastleFlightPawn::HandleMouseYaw);
	PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &ACastleFlightPawn::HandleMousePitch);
}

void ACastleFlightPawn::DoMove(float Right, float Forward)
{
	if (Controller == nullptr)
	{
		return;
	}

	const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	AddMovementInput(ForwardDirection, Forward);
	AddMovementInput(RightDirection, Right);
}

void ACastleFlightPawn::DoLook(float Yaw, float Pitch)
{
	AddControllerYawInput(Yaw * MouseSensitivity);
	AddControllerPitchInput(Pitch * MouseSensitivity);
}

void ACastleFlightPawn::DoAscendStart()
{
	bAscend = true;
}

void ACastleFlightPawn::DoAscendEnd()
{
	bAscend = false;
}

void ACastleFlightPawn::DoDescendStart()
{
	bDescend = true;
}

void ACastleFlightPawn::DoDescendEnd()
{
	bDescend = false;
}

void ACastleFlightPawn::MoveForwardPressed()
{
	bMoveForward = true;
}

void ACastleFlightPawn::MoveForwardReleased()
{
	bMoveForward = false;
}

void ACastleFlightPawn::MoveBackwardPressed()
{
	bMoveBackward = true;
}

void ACastleFlightPawn::MoveBackwardReleased()
{
	bMoveBackward = false;
}

void ACastleFlightPawn::MoveRightPressed()
{
	bMoveRight = true;
}

void ACastleFlightPawn::MoveRightReleased()
{
	bMoveRight = false;
}

void ACastleFlightPawn::MoveLeftPressed()
{
	bMoveLeft = true;
}

void ACastleFlightPawn::MoveLeftReleased()
{
	bMoveLeft = false;
}

void ACastleFlightPawn::AscendPressed()
{
	bAscend = true;
}

void ACastleFlightPawn::AscendReleased()
{
	bAscend = false;
}

void ACastleFlightPawn::DescendPressed()
{
	bDescend = true;
}

void ACastleFlightPawn::DescendReleased()
{
	bDescend = false;
}

void ACastleFlightPawn::HandleMouseYaw(float AxisValue)
{
	if (!FMath::IsNearlyZero(AxisValue))
	{
		AddControllerYawInput(AxisValue * MouseSensitivity);
	}
}

void ACastleFlightPawn::HandleMousePitch(float AxisValue)
{
	if (!FMath::IsNearlyZero(AxisValue))
	{
		// 鼠标向上移动时视角向上，符合常见第一人称操作习惯。
		AddControllerPitchInput(-AxisValue * MouseSensitivity);
	}
}

void ACastleFlightPawn::ApplyFlightMovement()
{
	if (Controller == nullptr)
	{
		return;
	}

	const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	const FRotationMatrix RotationMatrix(YawRotation);
	const FVector ForwardDirection = RotationMatrix.GetUnitAxis(EAxis::X);
	const FVector RightDirection = RotationMatrix.GetUnitAxis(EAxis::Y);

	const float ForwardInput = (bMoveForward ? 1.0f : 0.0f) - (bMoveBackward ? 1.0f : 0.0f);
	const float RightInput = (bMoveRight ? 1.0f : 0.0f) - (bMoveLeft ? 1.0f : 0.0f);
	const float VerticalInput = (bAscend ? 1.0f : 0.0f) - (bDescend ? 1.0f : 0.0f);

	if (!FMath::IsNearlyZero(ForwardInput))
	{
		AddMovementInput(ForwardDirection, ForwardInput);
	}
	if (!FMath::IsNearlyZero(RightInput))
	{
		AddMovementInput(RightDirection, RightInput);
	}
	if (!FMath::IsNearlyZero(VerticalInput))
	{
		AddMovementInput(FVector::UpVector, VerticalInput);
	}
}
