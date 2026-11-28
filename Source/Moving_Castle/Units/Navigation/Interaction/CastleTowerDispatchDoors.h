// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastleTowerDispatchDoors.generated.h"

class ACastleBoard;
class UStaticMesh;
class UStaticMeshComponent;

/** 塔底一体双门标识、主塔一楼内部隐式导航地板和门内导航锚点。 */
UCLASS()
class UCastleTowerDispatchDoors final : public UActorComponent
{
	GENERATED_BODY()

public:
	UCastleTowerDispatchDoors();
	bool Initialize(const ACastleBoard& Board, FText& OutReason);

	/** 校验门外房间已就绪，并返回主塔内部一楼门 A、门 B 内侧的站立面锚点。 */
	bool TryGetDoorWaypoints(FVector& OutExit, FVector& OutEntrance, FText& OutReason) const;

private:
	/** Cube 仍是两块隐式导航地板的原网格；门牌另用作者双面套件。 */
	UPROPERTY() TObjectPtr<UStaticMesh> DoorFloorMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> DoorSignMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> DoorSigns;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	TWeakObjectPtr<UStaticMeshComponent> DoorFloors[2];
	TWeakObjectPtr<const ACastleBoard> Board;
};
