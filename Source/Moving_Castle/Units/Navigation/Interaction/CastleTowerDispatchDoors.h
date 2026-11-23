// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastleTowerDispatchDoors.generated.h"

class ACastleBoard;
class UStaticMesh;

/** 塔底两个门的占位标识、主塔一楼内部隐式导航地板和门内导航锚点。 */
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
	UPROPERTY() TObjectPtr<UStaticMesh> DoorMarkerMesh;
	TWeakObjectPtr<UStaticMeshComponent> DoorFloors[2];
	TWeakObjectPtr<const ACastleBoard> Board;
};
