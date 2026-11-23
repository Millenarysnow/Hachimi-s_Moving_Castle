#pragma once

#include "Castle/SkyIslands/CastleSkyIslandBase.h"
#include "CastlePinballNPCSkyIsland.generated.h"

class UCastlePinballNPCSkyIslandDataAsset;
class UCastlePinballNPCSimulationComponent;
class UCastlePinballNPCConsoleComponent;
class UCastlePinballNPCStatusWidget;
class UInputComponent;
class UMaterialInterface;
class UTextRenderComponent;
class APawn;

/** 占位弹珠机的场景与本局招募接线。场地内所有物件都附在同一个倾斜坐标系。 */
UCLASS()
class MOVING_CASTLE_API ACastlePinballNPCSkyIsland : public ACastleSkyIslandBase
{
	GENERATED_BODY()

public:
	ACastlePinballNPCSkyIsland();
	virtual void Tick(float DeltaSeconds) override;
	bool EnterAimMode(ACastlePlayerController& Controller, FText& OutReason);
#if !UE_BUILD_SHIPPING
	/** 单机 PIE 验收入口，独立保存玩家返回位置并复用场内交互组件。 */
	static void StartAcceptanceVisit(UWorld* World);
#endif

protected:
	virtual bool OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutFailureReason) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FCastlePinballIslandTest;
#endif
	bool CommitRecruit(int32 NPCIndex, FText& OutReason);
	bool TryLaunchCoin(FText& OutReason);
	void LaunchFromInput();
	void ExitAimMode();
	void UpdateLauncher(float DeltaSeconds);
	void RefreshStatus();
#if !UE_BUILD_SHIPPING
	void InteractForAcceptance();
	void QueueAcceptanceReturn();
	void ReturnFromAcceptance();
	TWeakObjectPtr<APawn> AcceptancePawn;
	FTransform AcceptanceReturnTransform;
	FRotator AcceptanceReturnView;
	bool bAcceptanceReturnQueued = false;
#endif
	void SyncScene();
	UStaticMeshComponent* AddShape(UStaticMesh* Mesh, const FVector& Position, const FVector& Size, const FLinearColor& Color);
	UTextRenderComponent* AddLabel(const FVector& Position, const FText& Text, float Size);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Playfield;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCastlePinballNPCSimulationComponent> Simulation;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCastlePinballNPCConsoleComponent> Console;
	UPROPERTY(EditDefaultsOnly, Category = "Pinball") TSoftObjectPtr<UCastlePinballNPCSkyIslandDataAsset> IslandConfig;
	UPROPERTY(EditDefaultsOnly, Category = "Pinball") TSoftClassPtr<UCastlePinballNPCStatusWidget> StatusWidgetClass;
	UPROPERTY(Transient) TObjectPtr<UCastlePinballNPCSkyIslandDataAsset> Config;
	UPROPERTY(Transient) TSubclassOf<UCastlePinballNPCStatusWidget> LoadedStatusWidgetClass;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> ShapeMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> NPCBodies;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextRenderComponent>> NPCLabels;
	UPROPERTY(Transient) TMap<uint64, TObjectPtr<UStaticMeshComponent>> CoinBodies;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LauncherCarriage;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LauncherBarrel;
	UPROPERTY(Transient) TObjectPtr<UCastlePinballNPCStatusWidget> StatusWidget;
	UPROPERTY(Transient) TObjectPtr<UInputComponent> AimInput;
	UPROPERTY(Transient) TObjectPtr<UInputComponent> AcceptanceInput;
	TWeakObjectPtr<ACastlePlayerController> AimController;
	double LauncherPosition = 0.0;
	double LauncherAngle = 0.0;
	bool bLaunching = false;
};
