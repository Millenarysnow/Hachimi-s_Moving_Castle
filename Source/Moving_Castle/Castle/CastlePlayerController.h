// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CastleGameplayTypes.h"
#include "CastlePlayerController.generated.h"

class ACastleBoard;
class ACastleGridCursor;

/**
 * 移动城堡的自有玩家控制器。
 *
 * 控制器隐藏系统鼠标并把视口中心作为唯一指针位置，每帧将中心射线交给
 * 棋盘解析，再驱动浅蓝色网格面指示器。建筑放置输入会在此基础上继续扩展。
 */
UCLASS()
class MOVING_CASTLE_API ACastlePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** 初始化中心指针和游戏输入模式。 */
	ACastlePlayerController();

	/** 返回当前帧的网格目标副本，供 HUD 和后续放置系统读取。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|网格指针")
	FCastleGridCursorTarget GetCurrentGridCursorTarget() const
	{
		return CurrentGridCursorTarget;
	}

	/** 直接使用当前中心指针目标放置样例卡，供输入绑定和后续 UI 调用。 */
	void PlacePrototypeBuildingAtCursor();

protected:
	/** 隐藏鼠标、锁定输入并创建指示器。 */
	virtual void BeginPlay() override;

	/** 每帧用屏幕中心射线更新网格目标。 */
	virtual void Tick(float DeltaSeconds) override;

	/** 保留父类生命周期入口，但不再添加第三人称模板的 Input Mapping Context。 */
	virtual void SetupInputComponent() override;

	/** 销毁 PIE 中创建的指示器 Actor。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 查找当前世界唯一的逻辑棋盘。 */
	void FindActiveBoard();

	/** 创建浅蓝色网格面指示器。 */
	void SpawnGridCursor();

	/** 执行屏幕中心反投影和棋盘吸附。 */
	void UpdateGridCursor();

	/** 数字键输入回调，选择棋盘原型背包中的样例卡。 */
	void SelectPrototypeSlot1();
	void SelectPrototypeSlot2();
	void SelectPrototypeSlot3();
	void SelectPrototypeSlot4();

	/** 当前关卡的棋盘实例。 */
	UPROPERTY(Transient)
	TObjectPtr<ACastleBoard> ActiveBoard;

	/** 当前玩家对应的可视化指示器。 */
	UPROPERTY(Transient)
	TObjectPtr<ACastleGridCursor> GridCursor;

	/** 中心射线最大距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格指针", meta = (ClampMin = "100.0"))
	float GridCursorTraceDistance = 100000.0f;

	/** 当前帧解析结果；无有效目标时 bValid 为 false。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格指针")
	FCastleGridCursorTarget CurrentGridCursorTarget;

	/** 防止棋盘尚未生成时每帧重复刷屏。 */
	bool bLoggedMissingBoard = false;
};
