#pragma once

#include "Castle/SkyIslands/CastleSkyIslandBase.h"
#include "CastlePinballNPCSkyIsland.generated.h"

class UCastlePinballNPCSkyIslandDataAsset;
class UCastlePinballNPCSimulationComponent;
class UMaterialInterface;
class UTextRenderComponent;

/** 占位弹珠机的场景与本局招募接线。场地内所有物件都附在同一个倾斜坐标系。 */
UCLASS()
class MOVING_CASTLE_API ACastlePinballNPCSkyIsland : public ACastleSkyIslandBase
{
	GENERATED_BODY()

public:
	ACastlePinballNPCSkyIsland();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual bool OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutFailureReason) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool CommitRecruit(int32 NPCIndex, FText& OutReason);
	void SyncScene();
	UStaticMeshComponent* AddShape(UStaticMesh* Mesh, const FVector& Position, const FVector& Size, const FLinearColor& Color);
	UTextRenderComponent* AddLabel(const FVector& Position, const FText& Text, float Size);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Playfield;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCastlePinballNPCSimulationComponent> Simulation;
	UPROPERTY(EditDefaultsOnly, Category = "Pinball") TSoftObjectPtr<UCastlePinballNPCSkyIslandDataAsset> IslandConfig;
	UPROPERTY(Transient) TObjectPtr<UCastlePinballNPCSkyIslandDataAsset> Config;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> ShapeMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> NPCBodies;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextRenderComponent>> NPCLabels;
	UPROPERTY(Transient) TMap<uint64, TObjectPtr<UStaticMeshComponent>> CoinBodies;
};
