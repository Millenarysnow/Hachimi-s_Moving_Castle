// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Castle/Director/CastleDirectorTypes.h"
#include "CastleDirectorNodeActor.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UCastleDirectorConfigDataAsset;

/** 浮空节点只处理碰撞及标签，不持有关数、奖励或战斗流程。 */
UCLASS()
class MOVING_CASTLE_API ACastleDirectorNodeActor : public AActor
{
	GENERATED_BODY()

public:
	ACastleDirectorNodeActor();
	/** 延迟生成期间写入视觉配置；完成生成后再显式激活碰撞。 */
	void Configure(const UCastleDirectorConfigDataAsset& Config, int32 Step, ECastleDirectorNodeKind Kind);
	void ActivateNode();
	ECastleDirectorNodeKind GetKind() const { return NodeKind; }

private:
	/** 组件拥有关系固定在 C++，资产只提供网格。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Trigger;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
	TWeakObjectPtr<APawn> BlockedInitialPawn;
	ECastleDirectorNodeKind NodeKind = ECastleDirectorNodeKind::Normal;
	bool bActive = false;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
