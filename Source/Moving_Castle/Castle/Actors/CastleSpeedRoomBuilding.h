#pragma once

#include "CastleBuildingBase.h"
#include "CastleSpeedRoomBuilding.generated.h"

/** 一级加速房的原生身份。派生蓝图提供外观，原生组件执行 NPC 进入效果。 */
UCLASS(Abstract, Blueprintable)
class MOVING_CASTLE_API ACastleSpeedRoomBuilding : public ACastleBuildingBase
{
	GENERATED_BODY()

public:
	explicit ACastleSpeedRoomBuilding(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
