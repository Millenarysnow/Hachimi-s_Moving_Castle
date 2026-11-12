// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildings.h"
#include "Components/Building/Effects/Attack/CastleTowerAttackEffectComponent.h"

ACastleRoadBuilding::ACastleRoadBuilding()
{
	// 道路不参与功能建筑合成，固定使用道路类别和 1 级类定义。
	BuildingType = ECastleBuildingType::Road;
	BuildingLevel = 1;
}
ACastleArcherTowerBuilding::ACastleArcherTowerBuilding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCastleTowerAttackEffectComponent>(TEXT("BuildingEffect")))
{
	// 箭塔等级子类只设置等级；共同建筑效果子对象在构造时已替换为通用塔攻击组件。
	BuildingType = ECastleBuildingType::ArcherTower;
}


ACastleArcherTowerLevel1::ACastleArcherTowerLevel1()
{
	BuildingLevel = 1;
}

ACastleArcherTowerLevel2::ACastleArcherTowerLevel2()
{
	BuildingLevel = 2;
}

ACastleArcherTowerLevel3::ACastleArcherTowerLevel3()
{
	BuildingLevel = 3;
}

ACastleArcherTowerLevel4::ACastleArcherTowerLevel4()
{
	BuildingLevel = 4;
}

ACastleArcherTowerLevel5::ACastleArcherTowerLevel5()
{
	BuildingLevel = 5;
}

ACastleCannonTowerBuilding::ACastleCannonTowerBuilding()
{
	// 炮塔族的具体等级由等级子类构造函数设置，此处只设置类别。
	BuildingType = ECastleBuildingType::CannonTower;
}


ACastleCannonTowerLevel1::ACastleCannonTowerLevel1()
{
	BuildingLevel = 1;
}

ACastleCannonTowerLevel2::ACastleCannonTowerLevel2()
{
	BuildingLevel = 2;
}

ACastleCannonTowerLevel3::ACastleCannonTowerLevel3()
{
	BuildingLevel = 3;
}

ACastleCannonTowerLevel4::ACastleCannonTowerLevel4()
{
	BuildingLevel = 4;
}

ACastleCannonTowerLevel5::ACastleCannonTowerLevel5()
{
	BuildingLevel = 5;
}

ACastleFarmBuilding::ACastleFarmBuilding()
{
	// 农田族的具体等级由等级子类构造函数设置，此处只设置类别。
	BuildingType = ECastleBuildingType::Farm;
}


ACastleFarmLevel1::ACastleFarmLevel1()
{
	BuildingLevel = 1;
}

ACastleFarmLevel2::ACastleFarmLevel2()
{
	BuildingLevel = 2;
}

ACastleFarmLevel3::ACastleFarmLevel3()
{
	BuildingLevel = 3;
}

ACastleFarmLevel4::ACastleFarmLevel4()
{
	BuildingLevel = 4;
}

ACastleFarmLevel5::ACastleFarmLevel5()
{
	BuildingLevel = 5;
}

