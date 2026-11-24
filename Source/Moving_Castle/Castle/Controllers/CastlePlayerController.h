// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CastleGameplayTypes.h"
#include "Castle/Input/CastleControlMode.h"
#include "CastlePlayerController.generated.h"

class ACastleBoard;
class ACastleBuildingBase;
class ACastleGridCursor;
class ACastleSkyIslandBase;
class UCastleEconomyComponent;
class UCastleShopComponent;
class ACastleMassSpawner;
class ACastleMassEnemySpawner;
class UUserWidget;
class UCastleBackpackComponent;
class UCastleBadgeReplaceModalWidget;
class UCastleControlSettingsWidget;
class UCastleBuildingDemolishMenuWidget;

/** 提供给准星、设置界面及教程的即时模式变更事件。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCastleControlModeChanged, ECastleControlMode);
/** 回收成功后使用事务提交前的描述通知教程，原建筑 Actor 已被棋盘销毁。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCastleBuildingDemolished, const FCastleBuildingDescriptor&);

/**
 * 移动城堡的自有玩家控制器。
 *
 * 控制器统一仲裁鼠标、模态输入和世界瞄准。棋盘、空岛和路线编辑共享当前瞄准源。
 */
UCLASS()
class MOVING_CASTLE_API ACastlePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** 初始化中心指针和游戏输入模式。 */
	ACastlePlayerController();

	ECastleControlMode GetControlMode() const { return ControlMode; }
	/** 设置界面保存本地偏好；自动化可显式指定模式且不写入配置。 */
	void SetControlMode(ECastleControlMode NewMode, bool bSaveConfig = true);
	FKey GetPrimaryPlaceInteractKey() const;
	FKey GetDemolishKey() const;
	FText GetPrimaryPlaceInteractKeyDisplayText() const;
	FText GetDemolishKeyDisplayText() const;
	bool TryGetAimScreenPosition(float& OutScreenX, float& OutScreenY) const;
	bool TryGetAimWorldRay(FVector& OutOrigin, FVector& OutDirection) const;
	bool CanRotateCameraFromMouse() const;
	bool IsModalInputActive() const { return bModalInputActive; }
	bool ShouldDrawCenterCrosshair() const;
	FCastleControlModeChanged OnControlModeChanged;
	FCastleBuildingDemolished OnBuildingDemolishedToBackpack;

	/** 齿轮和弹窗共用控制器实例，保持一份设置与拆除状态。 */
	void ToggleControlSettings();
	void CloseBuildingDemolishMenu();
	void ConfirmBuildingDemolish(const UCastleBuildingDemolishMenuWidget& Menu);
	const UCastleBuildingDemolishMenuWidget* GetBuildingDemolishMenu() const { return BuildingDemolishMenu; }

	/** 返回当前帧的网格目标副本，供 HUD 和后续放置系统读取。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|网格指针")
	FCastleGridCursorTarget GetCurrentGridCursorTarget() const
	{
		return CurrentGridCursorTarget;
	}

	/** 主交互优先处理路径、空岛和气球，再按点击瞬间的瞄准目标放置。 */
	void PlacePrototypeBuildingAtCursor();

	/** 执行主交互射线的气球分支；命中即消费点击，返回值单独表示是否成功投放。 */
	bool TryPopBalloonFromRay(const FVector& Start, const FVector& End, bool& bOutConsumed);

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

	/** 导演界面与商店共用单一输入状态，避免 IgnoreInput 计数叠加或遗漏恢复。 */
	void SetDirectorModalState(bool bRewardOpen, bool bGameOverOpen, UUserWidget* FocusWidget);
	/** 正式导演启动时挂接道具 HUD；三个 WBP 全部绑定成功才加入视口。 */
	bool MountDirectorItemHUD(UCastleBackpackComponent* Backpack, FText& OutReason);
	/** 资产配置的交互键在导演完成启动校验时绑定。 */
	bool BindSkyIslandInput(const FKey& Key, FText& OutReason);
	/** 空岛完成初始化才接管当前岛引用，销毁时按实例注销。 */
	void RegisterActiveSkyIsland(ACastleSkyIslandBase* Island);
	void UnregisterActiveSkyIsland(const ACastleSkyIslandBase* Island);
	/** 查询可接收建造输入的已登记空岛，供控制器与 HUD 共用。 */
	ACastleSkyIslandBase* GetInteractiveSkyIsland() const;
	UFUNCTION(Exec) void CastleTestSkyIsland();

	/** 开战事务准备时立即清除高亮，避免等下一帧控制器 Tick 才撤下旧建造目标。 */
	void HideDirectorEditingPresentation();

	/** 查询导演编辑权限；未运行导演的世界保持原有行为。 */
	bool IsDirectorEditingAllowed(FText& OutReason) const;
	/** 构筑输入同时服从导演阶段与独立出兵会话。 */
	bool IsCastleEditingAllowed(FText& OutReason) const;

	/** 对外显示流程门控原因，输入拒绝必须可见。 */
	void ShowDirectorMessage(const FText& Message) const;

	/** 测试控制台命令：CastleAddGold 100，通过账户接口发放金币并显示操作结果。 */
	UFUNCTION(Exec)
	void CastleAddGold(int32 Amount);

	/** 测试控制台命令：CastleSpendGold 100，用于直接验证扣款和余额不足。 */
	UFUNCTION(Exec)
	void CastleSpendGold(int32 Amount);

	/** 验收命令：CastleSpawnPatrolNPCs 32，沿完整共享路线生成一批可见 NPC；重复执行替换上一批。 */
	UFUNCTION(Exec)
	void CastleSpawnPatrolNPCs(int32 Count = 32);

	/** 验收命令：CastleSpawnEnemy LavaGoliath|LavaPup|MiasmaWelp|ShadowSwarm [波数]。 */
	UFUNCTION(Exec)
	void CastleSpawnEnemy(const FString& EnemyType, int32 WaveCount = 1);

	/** 验收入口：通过真实受击入口扣血，同时驱动 HUD 与塔顶气球。 */
	UFUNCTION(Exec)
	void CastleDamageMainTower(float Damage);

	/** 验收入口：升到指定等级（2～4），新气球上限使用棋盘等级配置。 */
	UFUNCTION(Exec)
	void CastleUpgradeMainTower(int32 Tier);

	/** 验收入口：调整玩家可再放置的草稿点数。 */
	UFUNCTION(Exec)
	void CastleAddPatrolWaypoints(int32 Delta);

	/** 验收入口：在正式地图用已配置的待命数量和 NPC 模板开始出兵。 */
	UFUNCTION(Exec)
	void CastleStartTowerDispatch();

	UFUNCTION(Exec)
	void CastleEndTowerDispatch();

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
	void UpdateGridCursor(ACastleSkyIslandBase* Island = nullptr);
	void RetractIslandPlacement();
	void HandleWorldPointerPressed(FKey Key);
	void HandleWorldPointerReleased(FKey Key);
	void HandleClosePopup();
	void BeginTemporaryHUDPointer();
	void EndTemporaryHUDPointer();
	bool CanUseBuildingDemolishMenu(FText& OutReason) const;
	bool IsDemolishableBuilding(const ACastleBuildingBase* Building) const;
	ACastleBuildingBase* FindDemolishTargetAt(FVector2D ScreenPosition) const;
	bool OpenBuildingDemolishMenu(ACastleBuildingBase& Building, FVector2D ScreenPosition);
	void HandleDemolishOutsideClick(FKey Key, FVector2D ScreenPosition);
	void InitializePopupClasses();
	void RemoveBuildingDemolishWidget();
	void RemoveControlSettingsWidget();

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
	/** 滚轮按背包当前卡牌顺序换牌，向上选前一张、向下选后一张，到首尾时不跨越。 */
	void SelectPreviousBackpackSlot();
	void SelectNextBackpackSlot();
	void StepBackpackSelection(bool bNext);

	/** 数字键与滚轮共用的选中提交入口，负责更新原有索引和玩家提示。 */
	void SelectBackpackSlot(int32 SlotIndex);

	/** 战斗期快捷键入口（T 暂停/继续、F 切换 1x/2x 倍速、R 重试本局），均转交导演唯一状态入口。 */
	void ToggleBattlePauseShortcut();

	/** 监测已开始的连发，界面占用、松键或遮挡主塔立刻取消。 */
	void UpdateTowerDispatchInput();
	void ReleaseTowerDispatchInput();
	void ToggleTowerAutoDispatchShortcut();
	bool TryGetDispatchAim(FVector& OutStart, FVector& OutEnd) const;
	bool IsMainTowerHit(const FVector& Start, const FVector& End) const;
	bool bHoldingTowerDispatch = false;
	FKey BoundTowerDispatchKey;
	void ToggleBattleSpeedShortcut();
	void RetryBattleShortcut();

	/** 商店状态改变后统一切换鼠标、移动、视角和准星，不依赖打开商店的触发来源。 */
	UFUNCTION()
	void HandleShopChanged();

	/** 从商店和导演界面最终状态计算鼠标、输入模式及忽略计数。 */
	void RefreshModalInput(bool bForceRefresh = false);

	/** 徽章弹窗监听者在 WBP 刷新可见性后更新控制器的唯一输入锁。 */
	UFUNCTION()
	void HandleItemBadgeChanged();
	void InteractWithSkyIsland();

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

	/** 当前选中的实际背包槽位，以及已经应用到输入系统的模态界面总状态。 */
	int32 SelectedBackpackIndex = 0;
	bool bModalInputActive = false;
	bool bRewardInputActive = false;
	bool bGameOverInputActive = false;
	/** 左 Alt 仅在经典模式临时释放 HUD 指针，模态结束后按真实按住状态恢复。 */
	bool bHoldingTemporaryHUDPointer = false;
	ECastleControlMode ControlMode = ECastleControlMode::Classic;
	/** 硬类引用使两个正式 WBP 与控制器一起进入烹煮产物。 */
	UPROPERTY() TSubclassOf<UCastleControlSettingsWidget> ControlSettingsClass;
	UPROPERTY() TSubclassOf<UCastleBuildingDemolishMenuWidget> BuildingDemolishMenuClass;
	UPROPERTY(Transient) TObjectPtr<UCastleControlSettingsWidget> ControlSettings;
	UPROPERTY(Transient) TObjectPtr<UCastleBuildingDemolishMenuWidget> BuildingDemolishMenu;
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> DirectorFocusWidget;
	UPROPERTY(Transient) TArray<TObjectPtr<UUserWidget>> DirectorItemWidgets;
	UPROPERTY(Transient) TObjectPtr<UCastleBadgeReplaceModalWidget> BadgeReplaceWidget;

	/** 当前关卡的棋盘实例。 */
	UPROPERTY(Transient)
	TObjectPtr<ACastleBoard> ActiveBoard;
	/** 由导演或验收入口持有空岛，控制器仅路由本地输入。 */
	TWeakObjectPtr<ACastleSkyIslandBase> ActiveSkyIsland;

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

	/** 命令生成器由当前控制器管理，结束 PIE 时明确清理，不创建逐敌人 Actor。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACastleMassEnemySpawner>> EnemyPreviewSpawners;
};
