#pragma once

#include "Castle/Input/CastleControlMode.h"
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

/** 弹珠机的场景与本局招募接线。场地内所有物件都附在同一个倾斜坐标系。 */
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
	/** E 只在经典模式退出瞄准，设计模式将其保留为升降键并由 AimInput 阻止下传。 */
	void ExitAimModeFromE();
	/** 模式切换时立即刷新瞄准面板中的投币与退出按键说明。 */
	void HandleAimControlModeChanged(ECastleControlMode NewMode);
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
	/** 在同一个盘面局部坐标系内安装美术网格，碰撞和移动仍由二维模拟驱动。 */
	UStaticMeshComponent* AddPlayfieldMesh(UStaticMesh* Mesh, const FVector& Position, const FVector& Scale = FVector::OneVector);
	UTextRenderComponent* AddLabel(const FVector& Position, const FText& Text, float Size);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Playfield;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCastlePinballNPCSimulationComponent> Simulation;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCastlePinballNPCConsoleComponent> Console;
	UPROPERTY(EditDefaultsOnly, Category = "Pinball") TSoftObjectPtr<UCastlePinballNPCSkyIslandDataAsset> IslandConfig;
	UPROPERTY(EditDefaultsOnly, Category = "Pinball") TSoftClassPtr<UCastlePinballNPCStatusWidget> StatusWidgetClass;
	UPROPERTY(Transient) TObjectPtr<UCastlePinballNPCSkyIslandDataAsset> Config;
	UPROPERTY(Transient) TSubclassOf<UCastlePinballNPCStatusWidget> LoadedStatusWidgetClass;
	/** 固定盘面、活动机构、碰撞柱与金币分别保留作者的组件轴心。 */
	UPROPERTY() TObjectPtr<UStaticMesh> BoardMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> PegMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> GateMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CarriageMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> GuideMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CoinMesh;
	/** 六款棋子按出场顺序循环配色，人数由招募配置决定。 */
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> RecruitMeshes;
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
