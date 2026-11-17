#include "CastleSupportBuildings.h"

#include "Components/Building/Effects/Support/CastleSupportTowerEffectComponent.h"

ACastleBeeHiveBuilding::ACastleBeeHiveBuilding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCastleSupportTowerEffectComponent>(TEXT("BuildingEffect")))
{
	// 与借用的箭塔美术无关：购买、合成及触发均以蜂巢定义为身份。
	BuildingType = ECastleBuildingType::BeeHive;
}

// 等级类只固定定义校验使用的身份，水平和竖向占格由棋盘传入。
ACastleBeeHiveLevel1::ACastleBeeHiveLevel1() { BuildingLevel = 1; }
ACastleBeeHiveLevel2::ACastleBeeHiveLevel2() { BuildingLevel = 2; }
ACastleBeeHiveLevel3::ACastleBeeHiveLevel3() { BuildingLevel = 3; }
ACastleBeeHiveLevel4::ACastleBeeHiveLevel4() { BuildingLevel = 4; }

ACastleBalloonTowerBuilding::ACastleBalloonTowerBuilding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCastleSupportTowerEffectComponent>(TEXT("BuildingEffect")))
{
	// 气球与货箱的运行状态属于世界子系统，塔实例只发出一次人员经过请求。
	BuildingType = ECastleBuildingType::BalloonTower;
}

ACastleBalloonTowerLevel1::ACastleBalloonTowerLevel1() { BuildingLevel = 1; }
ACastleBalloonTowerLevel2::ACastleBalloonTowerLevel2() { BuildingLevel = 2; }
ACastleBalloonTowerLevel3::ACastleBalloonTowerLevel3() { BuildingLevel = 3; }
ACastleBalloonTowerLevel4::ACastleBalloonTowerLevel4() { BuildingLevel = 4; }
