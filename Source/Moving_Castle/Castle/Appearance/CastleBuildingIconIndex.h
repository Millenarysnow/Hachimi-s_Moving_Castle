#pragma once

#include "CastleGameplayTypes.h"

namespace CastleBuildingIcons
{
	/** 地板材质的功能图集和建造栏独立贴图共用作者编号，不能使用建筑枚举的序号。 */
	inline int32 FindIndex(const ECastleBuildingType Type)
	{
		switch (Type)
		{
		case ECastleBuildingType::NormalRoom: return 0;
		case ECastleBuildingType::Farm: return 1;
		case ECastleBuildingType::CannonTower: return 2;
		case ECastleBuildingType::ArcherTower: return 3;
		case ECastleBuildingType::ShieldTower: return 4;
		case ECastleBuildingType::BalloonTower: return 5;
		case ECastleBuildingType::BeeHive: return 6;
		}
		// 未知建筑没有有效的图集索引，调用方须记录资产与类型并拒绝发布表现。
		return INDEX_NONE;
	}
}
