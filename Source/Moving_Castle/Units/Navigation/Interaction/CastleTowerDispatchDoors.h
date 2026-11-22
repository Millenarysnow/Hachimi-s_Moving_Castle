// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastleTowerDispatchDoors.generated.h"

class ACastleBoard;
class UStaticMesh;

/** 塔底两个门的占位标识和导航锚点，共用棋盘底层的 ±X 邻格。 */
UCLASS()
class UCastleTowerDispatchDoors final : public UActorComponent
{
	GENERATED_BODY()

public:
	UCastleTowerDispatchDoors();
	bool Initialize(const ACastleBoard& Board, FText& OutReason);

	/** 返回门外实际房间的站立面格心；缺少任一房间时拒绝建立有效路线。 */
	bool TryGetDoorWaypoints(FVector& OutExit, FVector& OutEntrance, FText& OutReason) const;

private:
	UPROPERTY() TObjectPtr<UStaticMesh> DoorMarkerMesh;
	TWeakObjectPtr<const ACastleBoard> Board;
};
