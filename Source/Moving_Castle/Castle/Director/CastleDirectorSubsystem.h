// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Castle/Director/CastleDirectorTypes.h"
#include "Castle/Combat/CastleCombatTypes.h"
#include "CastleDirectorSubsystem.generated.h"

class ACastleBoard;
class UCastleMenuRecorder;
class ACastleBuildingBase;
class ACastleDirectorNodeActor;
class ACastlePlayerController;
class ACastlePatrolRoute;
class UCastleDirectorConfigDataAsset;
class UCastleMainTowerComponent;
class UUserWidget;
class UCastleCombatSubsystem;
class UCastlePendingRoomReward;
class UCastleTowerUpgradeRewardWidget;
class UCastleWildcardRoomPickerWidget;
class ACastleSkyIslandBase;
enum class ECastleSkyIslandType : uint8;
class UCastleBadgeDataAsset;

/** 原生阶段事件不承担流程决策，界面和输入只监听最终状态。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCastleDirectorPhaseChanged, ECastleDirectorPhase);
/** 导演接受清场后发布已提交的战斗类型与关号。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCastleBattleClearedForItems, ECastleBattleKind, int32);

/** 整局导演：在白名单地图的首次游戏 Tick 启动，世界重载时自然销毁全部单局状态。 */
UCLASS()
class MOVING_CASTLE_API UCastleDirectorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;

	/** 导演未运行时保留旧行为；运行时严格按阶段门控。 */
	bool IsRunning() const { return bRunning; }
	bool IsEditingAllowed(FText& OutReason) const;
	/** 普通商店在主塔编辑阶段常驻可用；导演阶段门控仍限制战斗、领奖、空岛和结束阶段。 */
	bool IsShopAccessAllowed(FText& OutReason) const;
	ECastleDirectorPhase GetPhase() const { return Phase; }
	int32 GetLevel() const { return Level; }
	/** 返回已建造的实际占格数。棋盘失效时报告错误并返回 INDEX_NONE。 */
	int32 GetOwnedRoomCount() const;
	/** 查询当前已提交等级的房间容量；配置或等级无效时报告错误并返回 INDEX_NONE。 */
	int32 GetCurrentRoomLimit() const;
	/** 检查并提交可能的自动升级。无需升级或被模态延后时检查成功，事务失败时返回原因。 */
	bool TryCheckTowerUpgrade(FText& OutReason);
	/** 阶段门控保证上场战斗的受击增益不会泄漏到下一场。 */
	bool IsFirstTowerDamageTriggeredInBattle() const { return Phase == ECastleDirectorPhase::Battle && bFirstTowerDamageInBattle; }
	/** 本局已解锁的出牌数字上限，供功能塔领奖及万能卡共用。 */
	int32 GetCurrentMaxShopNumber() const;
	int32 GetRunNPCCount() const { return RunNPCCount; }
	bool AddRunNPCCount(int32 Delta, FText& OutReason);
	void ClaimTowerUpgradeReward(int32 Index);
	void DiscardTowerUpgradeRewards();
	/** 沿指定射线交互。设计模式的鼠标点击关闭空地提示，仍返回活动岛校验与物件行为的失败原因。 */
	bool TryInteractWithSkyIsland(const FVector& Start, const FVector& End, FText& OutReason, bool bShowMissPrompt = true);
	/** 额外空岛使用当前节点的进度；从离岛门返塔后仍停留在同一关。 */
	bool TryEnterBonusSkyIslandFromPortal(ACastleBuildingBase* PortalRoom, FText& OutReason);
	bool RequestIslandRoomShop(ACastleSkyIslandBase* Island, FText& OutReason);
	bool RequestIslandRoomReward(ACastleSkyIslandBase* Island, UCastlePendingRoomReward* Reward, FText& OutReason);
	/** 保留完整徽章资产到发放边界，便于在库存和界面更新前校验显示资源。 */
	bool RequestIslandBadgeReward(ACastleSkyIslandBase* Island, const UCastleBadgeDataAsset& Badge, FText& OutReason);
#if !UE_BUILD_SHIPPING
	/** 在编辑阶段从本局正式配置指定空岛，沿用节点入岛与离岛的事务。 */
	bool TryEnterSkyIslandByTypeForDebug(ECastleSkyIslandType Type, FText& OutReason);
	bool TryEnterSkyIslandFixture(FText& OutReason);
#endif
	/** 战斗清场或空岛成功返塔后提交，节点与正式战斗都读取同一关卡进度。 */
	int32 GetClearedSteps() const { return ClearedSteps; }
	int32 GetNextStep() const { return ClearedSteps + 1; }
	const FText& GetStatusMessage() const { return StatusMessage; }
	UCastleDirectorConfigDataAsset* GetConfig() const { return Config; }
	FString GetHUDSummary() const;
	FText GetRewardLabel(ECastleDirectorReward Reward) const;
	const FText& GetGameOverFailure() const { return GameOverFailure; }

	/** 节点和三个 C++ 界面的唯一业务入口，不向蓝图暴露阶段写接口。 */
	void HandleNodeTouched(ACastleDirectorNodeActor* Node);
	bool TryChooseReward(ECastleDirectorReward Reward, FText& OutReason);
	void RestartCurrentMap();

	/** 战斗期暂停/继续与 1x/2x 倍速的唯一业务入口；退出战斗时自动恢复运行与 1x 速度。 */
	bool ToggleBattlePause();
	bool SetBattlePaused(bool bInPaused);
	bool IsBattlePaused() const { return bBattlePaused; }
	bool ToggleBattleSpeed();
	bool SetBattleDoubleSpeed(bool bInDoubleSpeed);
	bool IsBattleDoubleSpeed() const { return bBattleDoubleSpeed; }

	/** 无副作用的节点规则；自动化和实际生成共同使用。 */
	static int32 GetNodeCountForStep(const UCastleDirectorConfigDataAsset& InConfig, int32 Step);
	static ECastleDirectorNodeKind GetNodeKindForStep(const UCastleDirectorConfigDataAsset& InConfig, int32 Step);
	static TArray<ECastleDirectorNodeKind> GetNodeKindsForStep(const UCastleDirectorConfigDataAsset& InConfig, int32 Step);
	FCastleDirectorPhaseChanged OnDirectorPhaseChanged;
	/** 正式开局依赖已就绪，仅发送一次，先于第一次进入编辑阶段。 */
	FSimpleMulticastDelegate OnRunStarted;
	/** 成功进入编辑阶段后发送，含开局、战斗奖励提交及空岛返塔。 */
	FSimpleMulticastDelegate OnEditingPhaseEntered;
	/** 已接受并提交的清场，带战斗种类及关号；编辑阶段仍由后续奖励流程进入。 */
	FCastleBattleClearedForItems OnBattleClearedForItems;
	/** 战斗暂停或倍速状态变化通知，HUD 据此即时切换图标。 */
	FSimpleMulticastDelegate OnBattleControlStateChanged;

#if WITH_DEV_AUTOMATION_TESTS
	/** 测试专用接线：仅自动化可隔离验证奖励和输入，不暴露给游戏入口。 */
	void ConfigureForAutomation(UCastleDirectorConfigDataAsset* InConfig, ACastleBoard* InBoard,
		ACastlePlayerController* InController, UCastleMainTowerComponent* InTower);
	bool SetPhaseForAutomation(ECastleDirectorPhase NewPhase);
#endif

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FCastleDirectorBattleIntegrationTest;
	friend class FDirectorBattleScenario;
	friend class FCastleDirectorWave2Scenario;
	friend class FCastlePortalRoomTraitTest;
	friend class FCastleSelfOrbitingRoomTraitTest;
#endif
	/** 启动只尝试一次；配置、场景或资产错误通过日志和屏幕明确失败。 */
	void TryAutoStart();
	bool StartDirector(FText& OutReason);
	bool PrepareNodes(int32 Step, TArray<TObjectPtr<ACastleDirectorNodeActor>>& OutNodes, FText& OutReason);
	void DestroyNodes();
	void SetPhase(ECastleDirectorPhase NewPhase);
	void EnterGameOver(const FText& Failure);
	void ShowMessage(const FText& Message, bool bError);
	bool EnsurePresentation(FText& OutReason);
	void UpdatePresentation();
	/** 节点重叠只入队；实际开战、NPC 回收都在 Mass 未处理的游戏线程边界执行。 */
	bool TryStartSelectedBattle(ACastleDirectorNodeActor* Node, FText& OutReason);
	/** 真实节点先根据关数和节点序号确定类型及玩法种子。 */
	bool TryEnterSkyIsland(ACastleDirectorNodeActor& Node, const UCastleDirectorConfigDataAsset& IslandConfig, FText& OutReason);
	/** 统一提交生成与传送；传送门入口保留当前节点，且在传送成功后才消费门体。 */
	bool TryEnterConfiguredSkyIsland(const UCastleDirectorConfigDataAsset& IslandConfig, ECastleSkyIslandType Type,
		int32 Seed, const ACastleDirectorNodeActor* Node, FText& OutReason, ACastleBuildingBase* PortalRoom = nullptr);
	bool TryDepartSkyIsland(FText& OutReason);
	/** 在更改战斗或空岛状态前，核对玩家并按已开放塔层计算同一俯瞰位。 */
	bool PrepareTowerOverview(FTransform& OutView, FText& OutReason) const;
	/** 在阶段对应的提交点传送玩家，并同步停止飞行、更新相机方向。 */
	bool TeleportPlayerToTowerOverview(const FTransform& View, FText& OutReason);
	bool ValidateIslandAction(const ACastleSkyIslandBase* Island, FText& OutReason) const;
	bool RecallBattleNPCs(FText& OutReason);
	/** 普通战由三选一触发，精英战在清场边界联合提交三笔效果。 */
	bool TryCommitRewards(bool bGold, bool bHealing, bool bShop, FText& OutReason, int32 EliteClearedStage = INDEX_NONE);
	bool OpenWildcardPicker(UCastlePendingRoomReward* Reward, FText& OutReason);
	UFUNCTION() void HandleWildcardClosed(bool bClaimed);
	void HandleBattleCleared(ECastleBattleKind Kind, int32 Stage);
	void HandleBattleFailed(ECastleBattleKind Kind, int32 BattleNumber, const FText& Reason);
	void UnbindCombatEvents();
	/** 退出战斗、结束或重载关卡前重置战斗倍速与暂停标志；bClearWorldPause 控制是否同时解除引擎暂停。 */
	void ResetBattleControls(bool bClearWorldPause);

	UFUNCTION()
	void HandleTowerHealthChanged(float PreviousHealth, float CurrentHealth, float Damage);
	UFUNCTION()
	void HandleBadgesChanged();

	/** 引用只属于当前世界，不允许跨地图继续使用。 */
	UPROPERTY(Transient) TObjectPtr<UCastleDirectorConfigDataAsset> Config;
	UPROPERTY(Transient) TObjectPtr<ACastleBoard> Board;
	UPROPERTY(Transient) TObjectPtr<ACastlePlayerController> Controller;
	UPROPERTY(Transient) TObjectPtr<UCastleMainTowerComponent> Tower;
	UPROPERTY(Transient) TArray<TObjectPtr<ACastleDirectorNodeActor>> Nodes;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> HUDWidget;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> RewardWidget;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> GameOverWidget;
	/** 仅正式 MainMap 创建，结算和世界结束时停止监听外观提交。 */
	UPROPERTY(Transient) TObjectPtr<UCastleMenuRecorder> MenuRecorder;
	UPROPERTY(Transient) TObjectPtr<UCastleTowerUpgradeRewardWidget> TowerUpgradeWidget;
	UPROPERTY(Transient) TObjectPtr<UCastleWildcardRoomPickerWidget> WildcardPicker;
	UPROPERTY(Transient) TObjectPtr<ACastlePatrolRoute> PatrolRoute;
	UPROPERTY(Transient) TObjectPtr<ACastleSkyIslandBase> ActiveSkyIsland;
	bool bDepartureRequested = false;
	bool bIslandShopGranted = false;
	bool bEnteringSkyIsland = false;
	bool bCurrentIslandIsBonus = false;
	/** 只在传送成功时递增；同一关使用第二张门牌会独立抽取。 */
	uint32 BonusSkyIslandVisitCount = 0;
	UPROPERTY(Transient) TArray<TObjectPtr<UCastlePendingRoomReward>> UpgradeRoomRewards;
	TArray<FCastleTowerUpgradeRewardEntry> UpgradeRewards;
	int32 ActiveUpgradeRewardIndex = INDEX_NONE;
	/** 一次选择只保存弱引用，原节点销毁或游戏结束后不能重新触发开战。 */
	TWeakObjectPtr<ACastleDirectorNodeActor> PendingNode;
	UPROPERTY(Transient) TObjectPtr<UCastleCombatSubsystem> Combat;
	FDelegateHandle BattleClearedHandle;
	FDelegateHandle BattleFailedHandle;
	ECastleDirectorPhase Phase = ECastleDirectorPhase::Inactive;
	FText StatusMessage;
	FText GameOverFailure;
	int32 Level = 1;
	/** 同一轮无模态编辑期间，同一占格数下只尝试一次失败的升级。 */
	TOptional<int32> FailedTowerUpgradeRoomCount;
	FText TowerUpgradeFailure;
	int32 ClearedSteps = 0;
	int32 RunNPCCount = 0;
	bool bRunning = false;
	bool bStartAttempted = false;
	bool bApplyingReward = false;
	/** 只由实际受理的清场置位。空岛返回、开局与重复按领奖键都不能多结算一轮。 */
	bool bSelfOrbitingRoundPending = false;
	bool bBattlePaused = false;
	bool bBattleDoubleSpeed = false;
	bool bFirstTowerDamageInBattle = false;
	bool bAutomation = false;
	uint64 LastNodeSelectionFrame = MAX_uint64;
};
