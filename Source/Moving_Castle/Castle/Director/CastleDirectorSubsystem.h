// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Castle/Director/CastleDirectorTypes.h"
#include "CastleDirectorSubsystem.generated.h"

class ACastleBoard;
class ACastleDirectorNodeActor;
class ACastlePlayerController;
class UCastleDirectorConfigDataAsset;
class UCastleMainTowerComponent;
class UUserWidget;

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

	/** 导演未运行时保留旧行为；运行时严格按阶段门控。 */
	bool IsRunning() const { return bRunning; }
	bool IsEditingAllowed(FText& OutReason) const;
	bool IsShopAccessAllowed(FText& OutReason) const;
	ECastleDirectorPhase GetPhase() const { return Phase; }
	int32 GetLevel() const { return Level; }
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

	/** 无副作用的节点规则；自动化和实际生成共同使用。 */
	static int32 GetNodeCountForStep(const UCastleDirectorConfigDataAsset& InConfig, int32 Step);
	static ECastleDirectorNodeKind GetNodeKindForStep(const UCastleDirectorConfigDataAsset& InConfig, int32 Step);
	FCastleDirectorPhaseChanged OnDirectorPhaseChanged;

#if WITH_DEV_AUTOMATION_TESTS
	/** 测试专用接线：仅自动化可驱动尚未接入战斗系统的阶段。 */
	void ConfigureForAutomation(UCastleDirectorConfigDataAsset* InConfig, ACastleBoard* InBoard,
		ACastlePlayerController* InController, UCastleMainTowerComponent* InTower);
	bool SetPhaseForAutomation(ECastleDirectorPhase NewPhase);
#endif

private:
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

	UFUNCTION()
	void HandleTowerHealthChanged(float PreviousHealth, float CurrentHealth, float Damage);

	/** 引用只属于当前世界，不允许跨地图继续使用。 */
	UPROPERTY(Transient) TObjectPtr<UCastleDirectorConfigDataAsset> Config;
	UPROPERTY(Transient) TObjectPtr<ACastleBoard> Board;
	UPROPERTY(Transient) TObjectPtr<ACastlePlayerController> Controller;
	UPROPERTY(Transient) TObjectPtr<UCastleMainTowerComponent> Tower;
	UPROPERTY(Transient) TArray<TObjectPtr<ACastleDirectorNodeActor>> Nodes;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> HUDWidget;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> RewardWidget;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> GameOverWidget;
	ECastleDirectorPhase Phase = ECastleDirectorPhase::Inactive;
	FText StatusMessage;
	FText GameOverFailure;
	int32 Level = 1;
	int32 Experience = 0;
	int32 ClearedSteps = 0;
	bool bRunning = false;
	bool bStartAttempted = false;
	bool bShopPermission = false;
	bool bApplyingReward = false;
	bool bAutomation = false;
	bool bModalInputActive = false;
	uint64 LastNodeSelectionFrame = MAX_uint64;
};
