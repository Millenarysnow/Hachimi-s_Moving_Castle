#pragma once

#include "Castle/SkyIslands/CastleSkyIslandBase.h"
#include "CastleGambleSkyIslandDataAsset.h"
#include "CastleMiniPlacementBoard.h"
#include "CastleGambleSkyIsland.generated.h"

class UCastleBadgeSubsystem;
class UCastleEconomyComponent;
class UCastleMainTowerComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;

enum class ECastleGambleMachine : uint8 { Roulette, Slot, Recycler };

/** 每个机台各自阻挡准星射线，玩法和单次结算由所属空岛管理。 */
UCLASS()
class UCastleGambleMachineComponent : public UStaticMeshComponent, public ICastleSkyIslandInteractable
{
	GENERATED_BODY()

public:
	ECastleGambleMachine Machine = ECastleGambleMachine::Roulette;
	virtual FText GetInteractPrompt() const override;
	virtual bool CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const override;
	virtual bool ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) override;
};

/** 三台机台共享本局账户与徽章源，轮盘房间只在这座岛存续期间托管。 */
UCLASS()
class MOVING_CASTLE_API ACastleGambleSkyIsland : public ACastleSkyIslandBase
{
	GENERATED_BODY()

public:
	ACastleGambleSkyIsland();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/** 面板使用正式背包槽位和微缩格坐标，变动时重新枚举。 */
	UCastleBackpackComponent* GetBackpack() const { return Backpack; }
	const FCastleMiniPlacementBoard& GetMiniBoard() const { return MiniBoard; }
	const UCastleGambleSkyIslandDataAsset* GetGambleConfig() const { return Config; }
	bool TryPlaceCard(int32 BackpackIndex, const FIntVector& Cell, FText& OutReason);
	bool TryReturnLastCard(FText& OutReason);
	bool TrySpinRoulette(ECastleRouletteColor Bet, FText& OutReason);
	bool IsRouletteSpinning() const { return SpinTimeRemaining > 0; }
	const FText& GetRouletteResult() const { return RouletteResult; }

protected:
	virtual bool OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutReason) override;

private:
	friend class UCastleGambleMachineComponent;
	friend class FCastleGambleIslandAutomationTest;

	/** 同步交易内的库存通知不得重入另一项机台操作。 */
	bool CanUseMachine(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const;
	bool OpenRoulettePanel(FText& OutReason);
	bool TryReturnAllCards(FText& OutReason);
	bool TryPlaySlot(FText& OutReason);
	bool TrySettleSlot(ECastleSlotPrize Prize, FRandomStream& Candidate, FText& OutSummary, FText& OutReason);
	bool TryRecycleBalloon(FText& OutReason);
	bool Reject(const FText& Reason, FText& OutReason) const;
	void ShowResult(const FText& Result) const;

	/** 微缩块和条纹仅为展示；只允许主机台被空岛交互射线命中。 */
	void DrawMiniCard(const FCastleMiniPlacement& Placement);
	void RemoveLastMiniCard();
	void ClearMiniCards();
	void BuildWheel();
	void SetTint(UStaticMeshComponent& Surface, const FLinearColor& Color);

	UPROPERTY(EditDefaultsOnly, Category = "赌博空岛")
	TSoftObjectPtr<UCastleGambleSkyIslandDataAsset> ConfigAsset;
	UPROPERTY(Transient) TObjectPtr<UCastleGambleSkyIslandDataAsset> Config;
	UPROPERTY(Transient) TObjectPtr<UCastleBackpackComponent> Backpack;
	UPROPERTY(Transient) TObjectPtr<UCastleEconomyComponent> Economy;
	UPROPERTY(Transient) TObjectPtr<UCastleMainTowerComponent> Tower;
	UPROPERTY(Transient) TObjectPtr<UCastleBadgeSubsystem> Badges;
	UPROPERTY(Transient) FCastleMiniPlacementBoard MiniBoard;

	UPROPERTY() TObjectPtr<UCastleGambleMachineComponent> Roulette;
	UPROPERTY() TObjectPtr<UCastleGambleMachineComponent> Slot;
	UPROPERTY() TObjectPtr<UCastleGambleMachineComponent> Recycler;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> MiniBoardSurface;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> RoulettePointer;
	UPROPERTY() TObjectPtr<USceneComponent> WheelCenter;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> WheelStripes;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> TintMaterial;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> MiniBlocks;
	UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> MiniLabels;
	UPROPERTY() TObjectPtr<UTextRenderComponent> SlotResultLabel;
	UPROPERTY() TObjectPtr<UTextRenderComponent> RecyclerResultLabel;

	FRandomStream RouletteRandom;
	FRandomStream SlotRandom;
	float SpinTimeRemaining = 0;
	double SpinStartYaw = 0;
	double SpinTargetYaw = 0;
	double MiniCellSize = 0;
	bool bTransacting = false;
	FText RouletteResult;
};
