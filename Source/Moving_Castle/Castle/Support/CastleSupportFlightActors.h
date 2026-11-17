#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleSupportFlightActors.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USphereComponent;
class UStaticMeshComponent;

/** 每批一座表现 Actor，以实例渲染显示真实蜜蜂数量；不为每只蜜蜂创建 Actor。 */
UCLASS(NotPlaceable, Transient)
class ACastleBeeSwarmVisual : public AActor
{
	GENERATED_BODY()
public:
	ACastleBeeSwarmVisual();
	bool Configure(int32 Count, UMaterialInterface* Material);

private:
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Bees;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Wings;
};

/** 一个气球带一只货箱；破裂时移除命中和球体，仅货箱进入自由落点阶段。 */
UCLASS(NotPlaceable, Transient)
class ACastleSupplyBalloon : public AActor
{
	GENERATED_BODY()
public:
	ACastleSupplyBalloon();
	bool Configure(uint64 InId, UMaterialInterface* Material, FLinearColor Tint);
	void ReleaseCargo();
	uint64 GetTargetId() const { return TargetId; }

private:
	UPROPERTY() TObjectPtr<USphereComponent> ClickSphere;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Balloon;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Tether;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Cargo;
	uint64 TargetId = 0;
};
