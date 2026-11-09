// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CastleFlightPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
struct FInputActionValue;

/**
 * 移动城堡的自有飞行玩家 Pawn。
 *
 * 这是独立于第三人称模板的控制对象。玩家始终处于飞行移动模式，WASD 在
 * 相机水平朝向上移动，空格/左 Ctrl 控制升降，鼠标用于旋转视角。
 */
UCLASS()
class MOVING_CASTLE_API ACastleFlightPawn : public ACharacter
{
	GENERATED_BODY()

public:
	/** 创建第一人称视角所需的相机和飞行移动默认值。 */
	ACastleFlightPawn();

	/** 返回相机弹簧臂，便于后续蓝图调整观察距离。 */
	FORCEINLINE USpringArmComponent* GetCameraBoom() const
	{
		return CameraBoom;
	}

	/** 返回玩家相机，供控制器和后续交互系统读取。 */
	FORCEINLINE UCameraComponent* GetFollowCamera() const
	{
		return FollowCamera;
	}

	/** 兼容触摸或外部 UI 的二维移动入口。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|飞行控制")
	void DoMove(float Right, float Forward);

	/** 兼容触摸或外部 UI 的视角入口。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|飞行控制")
	void DoLook(float Yaw, float Pitch);

	/** 兼容外部 UI 的上升按下事件。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|飞行控制")
	void DoAscendStart();

	/** 兼容外部 UI 的上升释放事件。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|飞行控制")
	void DoAscendEnd();

	/** 兼容外部 UI 的下降按下事件。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|飞行控制")
	void DoDescendStart();

	/** 兼容外部 UI 的下降释放事件。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|飞行控制")
	void DoDescendEnd();

protected:
	/** 强制玩家进入飞行模式，避免关卡或角色蓝图恢复重力。 */
	virtual void BeginPlay() override;

	/** 每帧把按键状态转换为飞行移动输入。 */
	virtual void Tick(float DeltaSeconds) override;

	/** 绑定自有键盘、鼠标输入，不依赖模板 InputAction 资产。 */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** 键盘按键状态回调。 */
	void MoveForwardPressed();
	void MoveForwardReleased();
	void MoveBackwardPressed();
	void MoveBackwardReleased();
	void MoveRightPressed();
	void MoveRightReleased();
	void MoveLeftPressed();
	void MoveLeftReleased();
	void AscendPressed();
	void AscendReleased();
	void DescendPressed();
	void DescendReleased();

	/** 鼠标轴回调，输入值来自锁定在屏幕中心的系统鼠标。 */
	void HandleMouseYaw(float AxisValue);
	void HandleMousePitch(float AxisValue);

	/** 根据当前按键状态生成相对于相机水平朝向的飞行移动。 */
	void ApplyFlightMovement();

	/** 第一人称相机组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** 第一人称观察相机。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	/** 飞行最大速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "飞行控制", meta = (ClampMin = "1.0"))
	float FlightSpeed = 900.0f;

	/** 飞行加速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "飞行控制", meta = (ClampMin = "1.0"))
	float FlightAcceleration = 5000.0f;

	/** 松开按键后的飞行减速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "飞行控制", meta = (ClampMin = "1.0"))
	float FlightBrakingDeceleration = 5000.0f;

	/** 鼠标旋转倍率；引擎已在输入配置中设置 MouseX/MouseY 灵敏度，默认不再额外压低轴值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "飞行控制", meta = (ClampMin = "0.001"))
	float MouseSensitivity = 1.0f;

	/** 当前连续按键状态。 */
	bool bMoveForward = false;
	bool bMoveBackward = false;
	bool bMoveRight = false;
	bool bMoveLeft = false;
	bool bAscend = false;
	bool bDescend = false;
};
