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

ACastleShieldTowerBuilding::ACastleShieldTowerBuilding()
{
	// 盾塔使用建筑基类原有的唯一效果组件，仅接入通用触发入口与计数。
	BuildingType = ECastleBuildingType::ShieldTower;
}

ACastleShieldTowerLevel1::ACastleShieldTowerLevel1()
{
	// 一级卡可直接购买和放置，单格占用沿用建筑基类规则。
	BuildingLevel = 1;
}

ACastleShieldTowerLevel2::ACastleShieldTowerLevel2()
{
	// 二级只改变等级身份，两个实际占用格由棋盘在合成时传入。
	BuildingLevel = 2;
}

ACastleShieldTowerLevel3::ACastleShieldTowerLevel3()
{
	// 三级继续使用统一四格合成和回收描述，不复制占用算法。
	BuildingLevel = 3;
}

ACastleShieldTowerLevel4::ACastleShieldTowerLevel4()
{
	// 四级是现有八格自动合成结果，不在盾塔族内扩大升级上限。
	BuildingLevel = 4;
}

ACastleShieldTowerLevel5::ACastleShieldTowerLevel5()
{
	// 五级与其他功能塔对齐预留类和资产，具体防御效果后续另行实现。
	BuildingLevel = 5;
}

