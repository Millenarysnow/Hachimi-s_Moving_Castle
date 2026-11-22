#include "CastleSpeedRoomBuilding.h"

#include "Components/Building/Effects/CastleSpeedRoomEffectComponent.h"

ACastleSpeedRoomBuilding::ACastleSpeedRoomBuilding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCastleSpeedRoomEffectComponent>(TEXT("BuildingEffect")))
{
	// 与蓝图造型无关。类型及等级与定义资产严格相符，默认效果子对象沿用统一触发入口。
	BuildingType = ECastleBuildingType::SpeedRoom;
	BuildingLevel = 1;
}
