// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CastleGameplayTypes.h"
#include "CastlePlayerController.generated.h"

class ACastleBoard;
class ACastleGridCursor;
class UCastleEconomyComponent;
class UCastleShopComponent;
class ACastleMassSpawner;

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

	/** 当前玩家的独立金币账户，奖励和资源建筑可通过此入口接入收支。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|经济")
	UCastleEconomyComponent* GetEconomy() const { return Economy; }

	/** 当前玩家的商店逻辑，未来波次结束时可直接打开此组件。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|商店")
	UCastleShopComponent* GetShop() const { return Shop; }

	/** 实际背包槽位索引，空背包时为 INDEX_NONE，UI 使用它显示当前选中卡。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|背包")
	int32 GetSelectedBackpackIndex() const { return SelectedBackpackIndex; }

	/** B 键切换商店；首次打开初始化商品，关闭后恢复中心指针建造。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|商店")
	void ToggleShop();

	/** 测试控制台命令：CastleAddGold 100，通过账户接口发放金币并显示操作结果。 */
	UFUNCTION(Exec)
	void CastleAddGold(int32 Amount);

	/** 测试控制台命令：CastleSpendGold 100，用于直接验证扣款和余额不足。 */
	UFUNCTION(Exec)
	void CastleSpendGold(int32 Amount);

	/** 验收命令：CastleSpawnPatrolNPCs 32，沿完整共享路线生成一批可见 NPC；重复执行替换上一批。 */
	UFUNCTION(Exec)
	void CastleSpawnPatrolNPCs(int32 Count = 32);

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

	/** 数字键按当前背包顺序选牌，1 至 9、0 对应十个实际槽位。 */
	void SelectPrototypeSlot1();
	void SelectPrototypeSlot2();
	void SelectPrototypeSlot3();
	void SelectPrototypeSlot4();
	void SelectBackpackSlot5();
	void SelectBackpackSlot6();
	void SelectBackpackSlot7();
	void SelectBackpackSlot8();
	void SelectBackpackSlot9();
	void SelectBackpackSlot10();
	void SelectBackpackSlot(int32 SlotIndex);

	/** 商店状态改变后统一切换鼠标、移动、视角和准星，不依赖打开商店的触发来源。 */
	UFUNCTION()
	void HandleShopChanged();

	/** 统一把收支、选牌和商店失败原因写入日志及屏幕，保证无资源时也有反馈。 */
	void ShowGameplayMessage(const FString& Message, bool bSuccess) const;

	/** 账户与商店是控制器的独立组件，不将经济状态塞进棋盘或 HUD。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "经济")
	TObjectPtr<UCastleEconomyComponent> Economy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "商店")
	TObjectPtr<UCastleShopComponent> Shop;

	/** 当前测试局开局发放的金币，正式经济规则接入后可在控制器蓝图中调整。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "经济", meta = (ClampMin = "0"))
	int32 InitialGold = 100;

	/** 当前选中的实际背包槽位和已经应用到输入系统的商店状态。 */
	int32 SelectedBackpackIndex = 0;
	bool bShopInputActive = false;

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

	/** 仅由验收命令创建的那一批，避免重复命令不断累积测试单位。 */
	UPROPERTY(Transient)
	TObjectPtr<ACastleMassSpawner> PatrolPreviewSpawner;
};
