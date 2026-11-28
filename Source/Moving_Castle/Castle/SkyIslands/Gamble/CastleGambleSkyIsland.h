#pragma once

#include "Castle/SkyIslands/CastleSkyIslandBase.h"
#include "CastleGambleSkyIslandDataAsset.h"
#include "CastleMiniPlacementBoard.h"
#include "CastleGambleSkyIsland.generated.h"

class ACastleBoard;
class ACastleGroundTileActor;
class UBoxComponent;
class UCastleBadgeSubsystem;
class UCastleEconomyComponent;
class UCastleGambleRouletteWidget;
class UCastleMainTowerComponent;
class UCastlePendingRoomReward;
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
	virtual bool CanHighlightInteract(const FCastleSkyIslandInteractContext& Context) const override;
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
	virtual bool TryResolveIslandGridCursorTarget(const FVector& Start, const FVector& End,
		FCastleGridCursorTarget& OutTarget) const override;
	virtual bool CanPlaceCardAtIslandCursor(const FCastleGridCursorTarget& Target, int32 BackpackIndex) const override;
	virtual bool TryPlaceBackpackAtIslandCursor(const FCastleGridCursorTarget& Target, int32 BackpackIndex,
		FText& OutReason) override;
	virtual bool TryRetractLastIslandPlacement(FText& OutReason) override;

	/** 验收和玩家原生输入共享正式背包及微缩格数据。 */
	UCastleBackpackComponent* GetBackpack() const { return Backpack; }
	const FCastleMiniPlacementBoard& GetMiniBoard() const { return MiniBoard; }
	const UCastleGambleSkyIslandDataAsset* GetGambleConfig() const { return Config; }
	bool TryPlaceCard(int32 BackpackIndex, const FIntVector& Cell, FText& OutReason);
	bool TryReturnLastCard(FText& OutReason);
	bool TryReturnAllCards(FText& OutReason);
	bool TrySpinRoulette(ECastleRouletteColor Bet, FText& OutReason);
	bool TryCloseRoulettePanel(FText& OutReason);
	bool IsRouletteSpinning() const { return SpinTimeRemaining > 0; }
	const FText& GetRouletteResult() const { return RouletteResult; }

protected:
	virtual bool OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutReason) override;
	/** 正式奖励由导演管理，验收载体复用奖励控件但独立托管其生命周期。 */
	virtual bool PresentRoomPrize(UCastlePendingRoomReward* Reward, FText& OutReason);
	/** 将正式资产交给徽章发放入口，替换面板订阅发放结果。 */
	virtual bool PresentBadgePrize(const UCastleBadgeDataAsset& Badge, FText& OutReason);

private:
	friend class UCastleGambleMachineComponent;
	friend class FCastleGambleIslandAutomationTest;
	friend class FCastleGambleAcceptanceScenario;

	/** 同步交易内的库存通知不得重入另一项机台操作。 */
	bool CanUseMachine(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const;
	bool OpenRoulettePanel(FText& OutReason);
	void RemoveRoulettePanel();
	bool TryPlaySlot(FText& OutReason);
	bool TrySettleSlot(ECastleSlotPrize Prize, FRandomStream& Candidate, FText& OutSummary, FText& OutReason);
	bool TryRecycleBalloon(FText& OutReason);
	bool Reject(const FText& Reason, FText& OutReason) const;
	void ShowResult(const FText& Result) const;

	/** 模型由静态网格模板构建；六面查询盒与房间的业务 Actor 完全独立。 */
	bool BuildMiniGround(const ACastleBoard& Board, int32 Seed, FText& OutReason);
	void ClearMiniGround();
	FVector GetMiniCellBase(const FIntVector& Cell) const;
	UBoxComponent* CreateMiniCard(const FCastleBuildingDescriptor& Card, const FIntVector& Cell, FText& OutReason);
	void RemoveMiniCard(const FIntVector& Cell);
	void ClearMiniCards();
	void BuildWheel();
	void SetTint(UStaticMeshComponent& Surface, const FLinearColor& Color);

	UPROPERTY(EditDefaultsOnly, Category = "赌博空岛")
	TSoftObjectPtr<UCastleGambleSkyIslandDataAsset> ConfigAsset;
	/** 持久化软引用让打包收集器能发现面板，进入轮盘交互时才实例化。 */
	UPROPERTY(EditDefaultsOnly, Category = "赌博空岛")
	TSoftClassPtr<UCastleGambleRouletteWidget> RoulettePanelClass;
	UPROPERTY(Transient) TObjectPtr<UCastleGambleSkyIslandDataAsset> Config;
	UPROPERTY(Transient) TObjectPtr<UCastleBackpackComponent> Backpack;
	/** 房间组按当前局棋盘准备，功能塔买断等前置条件与背包同属一个运行世界。 */
	UPROPERTY(Transient) TWeakObjectPtr<ACastleBoard> RewardBoard;
	UPROPERTY(Transient) TObjectPtr<UCastleEconomyComponent> Economy;
	UPROPERTY(Transient) TObjectPtr<UCastleMainTowerComponent> Tower;
	UPROPERTY(Transient) TObjectPtr<UCastleBadgeSubsystem> Badges;
	UPROPERTY(Transient) FCastleMiniPlacementBoard MiniBoard;

	UPROPERTY() TObjectPtr<UCastleGambleMachineComponent> Roulette;
	UPROPERTY() TObjectPtr<UCastleGambleMachineComponent> Slot;
	UPROPERTY() TObjectPtr<UCastleGambleMachineComponent> Recycler;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> RoulettePointer;
	UPROPERTY() TObjectPtr<USceneComponent> WheelCenter;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> WheelStripes;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> TintMaterial;
	UPROPERTY(Transient) TMap<FIntVector, TObjectPtr<ACastleGroundTileActor>> MiniTiles;
	UPROPERTY(Transient) TMap<FIntVector, TObjectPtr<UBoxComponent>> MiniRooms;
	UPROPERTY() TObjectPtr<UTextRenderComponent> SlotResultLabel;
	UPROPERTY() TObjectPtr<UTextRenderComponent> RecyclerResultLabel;
	UPROPERTY(Transient) TObjectPtr<UCastleGambleRouletteWidget> RoulettePanel;

	FRandomStream RouletteRandom;
	FRandomStream SlotRandom;
	float SpinTimeRemaining = 0;
	double SpinStartYaw = 0;
	double SpinTargetYaw = 0;
	double MiniCellSize = 0;
	double MiniTileSize = 0;
	double MiniLayerHeight = 0;
	FVector MiniOrigin = FVector::ZeroVector;
	bool bTransacting = false;
	FText RouletteResult;
};
