// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CastleMassTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleTowerDispatchSubsystem.generated.h"

class UCastleTowerDispatchConfigDataAsset;
class UMassEntityConfigAsset;

/** 只拥有这次战斗派出的 NPC。寻路与实际运动由导航快照和 Mass 处理器负责。 */
UCLASS()
class MOVING_CASTLE_API UCastleTowerDispatchSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	bool Configure(UCastleTowerDispatchConfigDataAsset* Configuration, FText& OutReason);
	const UCastleTowerDispatchConfigDataAsset* GetConfiguration() const { return Config; }
	bool IsSessionActive() const { return bSessionActive; }

	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔出兵")
	bool StartBattleDispatchSession(UMassEntityConfigAsset* NPCConfig, int32 InitialPoolCount, FText& OutReason);

	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔出兵")
	bool EndBattleDispatchSession(FText& OutReason);

	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔出兵")
	bool TryDispatchSingleNPC(FText& OutReason);

	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔出兵")
	bool SetAutoDispatchEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "移动城堡|主塔出兵")
	bool IsAutoDispatchEnabled() const { return bAutoDispatchEnabled; }

	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔出兵")
	bool ToggleAutoDispatch() { return SetAutoDispatchEnabled(!bAutoDispatchEnabled); }

	UFUNCTION(BlueprintCallable, Category = "移动城堡|主塔出兵")
	bool AddReserveNPCs(int32 Delta);

	UFUNCTION(BlueprintPure, Category = "移动城堡|主塔出兵")
	void GetDispatchCounts(int32& OutReserveCount, int32& OutActiveCount) const;

	/** 仅准星的首次按下开始连续派兵；松开或移开后需重新按下。 */
	bool BeginManualDispatch(FText& OutReason);
	/** 真实派兵提交后提供 Mass 实体句柄，教程气泡据此跟随出塔单位。 */
	TMulticastDelegate<void(const FCastleMassUnitHandle&)> OnNPCDispatched;
	void StopManualDispatch();

	bool BeginBattleDispatch(int32 TotalPoolCount);
	bool EndBattleDispatch();
	bool AddReserveNPCCount(int32 Delta) { return AddReserveNPCs(Delta); }

	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual bool IsTickable() const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;

private:
	UPROPERTY(Transient) TObjectPtr<UCastleTowerDispatchConfigDataAsset> Config;
	UPROPERTY(Transient) TObjectPtr<UMassEntityConfigAsset> SessionNPCConfig;

	/** 等待实际销毁的实体仍计入活跃数，避免池和场上同时计入同一名 NPC。 */
	struct FActiveNPC
	{
		FCastleMassUnitHandle Handle;
		bool bDespawnRequested = false;
	};
	TArray<FActiveNPC> ActiveNPCs;
	int32 ReserveCount = 0;
	int32 SessionTotalNPCs = 0;
	bool bSessionActive = false;
	bool bEndingSession = false;
	bool bAutoDispatchEnabled = false;
	bool bManualDispatchHeld = false;
	double AutoDispatchElapsed = 0.0;
	double HoldDispatchElapsed = 0.0;

	void ReclaimFinishedNPCs();
	bool Reject(FText& OutReason, const FString& Detail, bool bConfigurationError) const;
	void ShowMessage(const FText& Message, bool bSuccess) const;
	friend class FCastleMainTowerDispatchTest;
};
