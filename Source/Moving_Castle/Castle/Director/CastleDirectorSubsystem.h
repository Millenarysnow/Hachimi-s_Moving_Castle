// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Castle/Director/CastleDirectorTypes.h"
#include "Castle/Combat/CastleCombatTypes.h"
#include "CastleDirectorSubsystem.generated.h"

class ACastleBoard;
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
struct FCastleBadgeDefinition;

/** 原生阶段事件不承担流程决策，界面和输入只监听最终状态。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCastleDirectorPhaseChanged, ECastleDirectorPhase);

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
	bool IsShopAccessAllowed(FText& OutReason) const;
	ECastleDirectorPhase GetPhase() const { return Phase; }
	int32 GetLevel() const { return Level; }
	/** 阶段门控保证上场战斗的受击增益不会泄漏到下一场。 */
	bool IsFirstTowerDamageTriggeredInBattle() const { return Phase == ECastleDirectorPhase::Battle && bFirstTowerDamageInBattle; }
	/** 本局已解锁的出牌数字上限，供功能塔领奖及万能卡共用。 */
	int32 GetCurrentMaxShopNumber() const;
	int32 GetRunNPCCount() const { return RunNPCCount; }
	bool AddRunNPCCount(int32 Delta, FText& OutReason);
	void ClaimTowerUpgradeReward(int32 Index);
	void DiscardTowerUpgradeRewards();
	bool TryInteractWithSkyIsland(const FVector& Start, const FVector& End, FText& OutReason);
	bool RequestIslandRoomShop(ACastleSkyIslandBase* Island, FText& OutReason);
	bool RequestIslandRoomReward(ACastleSkyIslandBase* Island, UCastlePendingRoomReward* Reward, FText& OutReason);
	bool RequestIslandBadgeReward(ACastleSkyIslandBase* Island, const FCastleBadgeDefinition& Badge, FText& OutReason);
#if !UE_BUILD_SHIPPING
	bool TryEnterSkyIslandFixture(FText& OutReason);
#endif
	int32 GetExperience() const { return Experience; }
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
	/** 节点与验收命令共用同一工厂和传送事务。 */
	bool TryEnterSkyIsland(ACastleDirectorNodeActor& Node, const UCastleDirectorConfigDataAsset& IslandConfig, FText& OutReason);
	bool TryDepartSkyIsland(FText& OutReason);
	bool ValidateIslandAction(const ACastleSkyIslandBase* Island, FText& OutReason) const;
	bool RecallBattleNPCs(FText& OutReason);
	bool TryCommitRewards(bool bExperience, bool bHealing, bool bShop, FText& OutReason);
	bool OpenWildcardPicker(UCastlePendingRoomReward* Reward, FText& OutReason);
	UFUNCTION() void HandleWildcardClosed(bool bClaimed);
	void HandleBattleCleared(ECastleBattleKind Kind, int32 Ordinal);
	void HandleBattleFailed(ECastleBattleKind Kind, int32 Ordinal, const FText& Reason);
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
	UPROPERTY(Transient) TObjectPtr<UCastleTowerUpgradeRewardWidget> TowerUpgradeWidget;
	UPROPERTY(Transient) TObjectPtr<UCastleWildcardRoomPickerWidget> WildcardPicker;
	UPROPERTY(Transient) TObjectPtr<ACastlePatrolRoute> PatrolRoute;
	UPROPERTY(Transient) TObjectPtr<ACastleSkyIslandBase> ActiveSkyIsland;
	FTransform IslandReturnTransform;
	FRotator IslandReturnControlRotation;
	bool bDepartureRequested = false;
	bool bIslandShopGranted = false;
	UPROPERTY(Transient) TArray<TObjectPtr<UCastlePendingRoomReward>> UpgradeRoomRewards;
	TArray<FCastleTowerUpgradeRewardEntry> UpgradeRewards;
	int32 ActiveUpgradeRewardIndex = INDEX_NONE;
	/** 一次选择只保存弱引用，原节点销毁或游戏结束后不能重新触发开战。 */
	TWeakObjectPtr<ACastleDirectorNodeActor> PendingNode;
	UPROPERTY(Transient) TObjectPtr<UCastleCombatSubsystem> Combat;
	FDelegateHandle BattleClearedHandle;
	FDelegateHandle BattleFailedHandle;
	/** 普通、精英分别累计；只有整场开战提交成功后增加。 */
	int32 NormalBattles = 0;
	int32 EliteBattles = 0;
	ECastleBattleKind ActiveBattleKind = ECastleBattleKind::Normal;
	int32 ActiveBattleOrdinal = 0;
	ECastleDirectorPhase Phase = ECastleDirectorPhase::Inactive;
	FText StatusMessage;
	FText GameOverFailure;
	int32 Level = 1;
	int32 Experience = 0;
	int32 ClearedSteps = 0;
	int32 RunNPCCount = 0;
	bool bRunning = false;
	bool bStartAttempted = false;
	bool bShopPermission = false;
	bool bApplyingReward = false;
	bool bBattlePaused = false;
	bool bBattleDoubleSpeed = false;
	bool bFirstTowerDamageInBattle = false;
	bool bAutomation = false;
	uint64 LastNodeSelectionFrame = MAX_uint64;
};
