// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildings.h"

ACastleRoadBuilding::ACastleRoadBuilding()
{
	// 道路不参与功能建筑合成，固定使用道路类别和 1 级类定义。
	BuildingType = ECastleBuildingType::Road;
	BuildingLevel = 1;
}
ACastleArcherTowerBuilding::ACastleArcherTowerBuilding()
{
	// 箭塔族的具体等级由等级子类构造函数设置，此处只设置类别。
	BuildingType = ECastleBuildingType::ArcherTower;
}

TSubclassOf<ACastleBuildingBase> ACastleArcherTowerBuilding::GetClassForLevel(int32 InLevel) const
{
	switch (InLevel)
	{
	case 1:
		return ACastleArcherTowerLevel1::StaticClass();
	case 2:
		return ACastleArcherTowerLevel2::StaticClass();
	case 3:
		return ACastleArcherTowerLevel3::StaticClass();
	case 4:
		return ACastleArcherTowerLevel4::StaticClass();
	case 5:
		return ACastleArcherTowerLevel5::StaticClass();
	default:
		return nullptr;
	}
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

TSubclassOf<ACastleBuildingBase> ACastleCannonTowerBuilding::GetClassForLevel(int32 InLevel) const
{
	switch (InLevel)
	{
	case 1:
		return ACastleCannonTowerLevel1::StaticClass();
	case 2:
		return ACastleCannonTowerLevel2::StaticClass();
	case 3:
		return ACastleCannonTowerLevel3::StaticClass();
	case 4:
		return ACastleCannonTowerLevel4::StaticClass();
	case 5:
		return ACastleCannonTowerLevel5::StaticClass();
	default:
		return nullptr;
	}
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

TSubclassOf<ACastleBuildingBase> ACastleFarmBuilding::GetClassForLevel(int32 InLevel) const
{
	switch (InLevel)
	{
	case 1:
		return ACastleFarmLevel1::StaticClass();
	case 2:
		return ACastleFarmLevel2::StaticClass();
	case 3:
		return ACastleFarmLevel3::StaticClass();
	case 4:
		return ACastleFarmLevel4::StaticClass();
	case 5:
		return ACastleFarmLevel5::StaticClass();
	default:
		return nullptr;
	}
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

