// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleFlightPawn.h"

#include "CastlePlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Moving_Castle.h"

namespace
{
	/** 飞行速度档位，单位为厘米每秒。 */
	constexpr float FlightSpeedTiers[] = {200.0f, 350.0f, 550.0f, 900.0f, 1350.0f, 1900.0f, 2600.0f, 3600.0f};
	constexpr float FlightAccelerationRatio = 5.5f;
}

ACastleFlightPawn::ACastleFlightPawn()
{
	// 飞行移动需要每帧读取按键状态，但不需要额外的游戏逻辑 Tick。
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	ConfigureObserverCollision();

	// 不让角色网格或碰撞体自动跟随移动方向旋转，视角方向由控制器单独管理。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	MovementComponent->SetMovementMode(MOVE_Flying);
	MovementComponent->GravityScale = 0.0f;
	ApplyFlightSpeedTier();
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
	ConfigureObserverCollision();
	ApplyFlightSpeedTier();

	CameraBoom->TargetArmLength = 0.0f;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 64.0f);
}

void ACastleFlightPawn::ConfigureObserverCollision()
{
	// 导演节点以 WorldDynamic 查询 Pawn。保留双向 Overlap，其他通道均忽略，包括玩家自己的交互射线。
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	Capsule->SetGenerateOverlapEvents(true);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void ACastleFlightPawn::ApplyFlightSpeedTier()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float Speed = FlightSpeedTiers[FlightSpeedTierIndex];
	Movement->MaxFlySpeed = Speed;
	Movement->MaxAcceleration = Speed * FlightAccelerationRatio;
	Movement->BrakingDecelerationFlying = Speed * FlightAccelerationRatio;
}

void ACastleFlightPawn::DecreaseFlightSpeed()
{
	StepFlightSpeed(-1);
}

void ACastleFlightPawn::IncreaseFlightSpeed()
{
	StepFlightSpeed(1);
}

void ACastleFlightPawn::StepFlightSpeed(const int32 Direction)
{
	const auto* CastleController = Cast<ACastlePlayerController>(Controller);
	if (!IsValid(CastleController))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("飞行 Pawn %s 无法调节飞行速度：控制器 %s 无效。"),
			*GetPathName(), *GetPathNameSafe(Controller));
		return;
	}
	const int32 NextIndex = FlightSpeedTierIndex + Direction;
	// 模态界面独占输入。触及端点后继续按键时，速度和提示均不变。
	if (CastleController->IsModalInputActive() || NextIndex < 0 || NextIndex >= UE_ARRAY_COUNT(FlightSpeedTiers)) { return; }
	FlightSpeedTierIndex = NextIndex;
	ApplyFlightSpeedTier();
	UE_LOG(LogMoving_Castle, Display, TEXT("飞行速度 %d/%d 档（%.0f cm/s）。"),
		FlightSpeedTierIndex + 1, UE_ARRAY_COUNT(FlightSpeedTiers), FlightSpeedTiers[FlightSpeedTierIndex]);
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
	// 经典模式仍由 Space / Ctrl 升降，设计模式只由 Q / E 升降，两个预设共享回调但互不激活。
	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ACastleFlightPawn::DesignDescendPressed);
	PlayerInputComponent->BindKey(EKeys::Q, IE_Released, this, &ACastleFlightPawn::DescendReleased);
	PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &ACastleFlightPawn::DesignAscendPressed);
	PlayerInputComponent->BindKey(EKeys::E, IE_Released, this, &ACastleFlightPawn::AscendReleased);

	// 只绑定按下事件，两个模式的调速键均不随操作预设变化。
	PlayerInputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &ACastleFlightPawn::DecreaseFlightSpeed);
	PlayerInputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &ACastleFlightPawn::IncreaseFlightSpeed);

	// 鼠标轴只传入相对移动量；当前模式及模态门控由控制器查询。
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
	const ACastlePlayerController* CastleController = Cast<ACastlePlayerController>(Controller);
	if (!IsValid(CastleController))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("飞行 Pawn %s 无法处理经典模式上升键：玩家控制器 %s 不是有效的 CastlePlayerController。"),
			*GetPathName(), *GetNameSafe(Controller));
		return;
	}
	if (CastleController->GetControlMode() == ECastleControlMode::Classic)
	{
		bAscend = true;
	}
}

void ACastleFlightPawn::AscendReleased()
{
	bAscend = false;
}

void ACastleFlightPawn::DescendPressed()
{
	const ACastlePlayerController* CastleController = Cast<ACastlePlayerController>(Controller);
	if (!IsValid(CastleController))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("飞行 Pawn %s 无法处理经典模式下降键：玩家控制器 %s 不是有效的 CastlePlayerController。"),
			*GetPathName(), *GetNameSafe(Controller));
		return;
	}
	if (CastleController->GetControlMode() == ECastleControlMode::Classic)
	{
		bDescend = true;
	}
}

void ACastleFlightPawn::DescendReleased()
{
	bDescend = false;
}

void ACastleFlightPawn::DesignAscendPressed()
{
	const ACastlePlayerController* CastleController = Cast<ACastlePlayerController>(Controller);
	if (!IsValid(CastleController))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("飞行 Pawn %s 无法处理设计模式 E 键：玩家控制器 %s 不是有效的 CastlePlayerController。"),
			*GetPathName(), *GetNameSafe(Controller));
		return;
	}
	if (CastleController->GetControlMode() == ECastleControlMode::Design)
	{
		bAscend = true;
	}
}

void ACastleFlightPawn::DesignDescendPressed()
{
	const ACastlePlayerController* CastleController = Cast<ACastlePlayerController>(Controller);
	if (!IsValid(CastleController))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("飞行 Pawn %s 无法处理设计模式 Q 键：玩家控制器 %s 不是有效的 CastlePlayerController。"),
			*GetPathName(), *GetNameSafe(Controller));
		return;
	}
	if (CastleController->GetControlMode() == ECastleControlMode::Design)
	{
		bDescend = true;
	}
}

void ACastleFlightPawn::HandleMouseYaw(float AxisValue)
{
	const auto* CastleController = Cast<ACastlePlayerController>(Controller);
	if (CastleController && CastleController->CanRotateCameraFromMouse() && !FMath::IsNearlyZero(AxisValue))
	{
		AddControllerYawInput(AxisValue * MouseSensitivity);
	}
}

void ACastleFlightPawn::HandleMousePitch(float AxisValue)
{
	const auto* CastleController = Cast<ACastlePlayerController>(Controller);
	if (CastleController && CastleController->CanRotateCameraFromMouse() && !FMath::IsNearlyZero(AxisValue))
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
