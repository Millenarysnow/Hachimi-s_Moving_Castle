// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CastleBuildingDefinition.h"
#include "AssemblyFlightComponent.h"
#include "CastleBuildings.h"
#include "CastleBoard.h"

namespace CastleBuildingTests
{
	/** 自动化用完整推进原有放置飞入；测试仍走正式游戏世界与放置状态机。 */
	inline bool CompletePlacement(ACastleBuildingBase* Building)
	{
		if (!IsValid(Building)) { return false; }
		if (Building->AssemblyFlight->bIsPlaying)
		{
			Building->AssemblyFlight->TickComponent(100.0f, LEVELTICK_All, nullptr);
		}
		// 完成回调可能已同步替换并销毁放置 Actor；只有 Actor 仍存活时才读取其状态。
		return !IsValid(Building) || Building->bHasCompletedPlacement;
	}

	/** 异步动画替换 Actor 后按最终等级重新查询棋盘，避免继续使用已销毁的放置指针。 */
	inline ACastleBuildingBase* FindPlacedLevel(ACastleBoard* Board, const int32 Level)
	{
		for (ACastleBuildingBase* Building : Board->GetPlacedBuildings())
		{
			if (Building->GetBuildingLevel() == Level) { return Building; }
		}
		return nullptr;
	}

	/** 测试显式创建纯数据定义，不依赖用户对正式美术资产的编辑。 */
	inline UCastleBuildingDefinition* MakeDefinition(ECastleBuildingType Type)
	{
		UCastleBuildingDefinition* Definition = NewObject<UCastleBuildingDefinition>();
		Definition->DisplayName = FText::FromString(TEXT("测试建筑"));
		Definition->BuildingType = Type;
		TArray<TSubclassOf<ACastleBuildingBase>> Classes;
		switch (Type)
		{
		case ECastleBuildingType::Road:
			Classes = { ACastleRoadBuilding::StaticClass() };
			break;
		case ECastleBuildingType::ArcherTower:
			Classes = { ACastleArcherTowerLevel1::StaticClass(), ACastleArcherTowerLevel2::StaticClass(),
				ACastleArcherTowerLevel3::StaticClass(), ACastleArcherTowerLevel4::StaticClass(), ACastleArcherTowerLevel5::StaticClass() };
			break;
		case ECastleBuildingType::CannonTower:
			Classes = { ACastleCannonTowerLevel1::StaticClass(), ACastleCannonTowerLevel2::StaticClass(),
				ACastleCannonTowerLevel3::StaticClass(), ACastleCannonTowerLevel4::StaticClass(), ACastleCannonTowerLevel5::StaticClass() };
			break;
		case ECastleBuildingType::Farm:
			Classes = { ACastleFarmLevel1::StaticClass(), ACastleFarmLevel2::StaticClass(),
				ACastleFarmLevel3::StaticClass(), ACastleFarmLevel4::StaticClass(), ACastleFarmLevel5::StaticClass() };
			break;
		}
		for (int32 Index = 0; Index < Classes.Num(); ++Index)
		{
			FCastleBuildingLevelDefinition Entry;
			Entry.Level = Index + 1;
			Entry.BuildingClass = Classes[Index];
			Definition->Levels.Add(Entry);
		}
		return Definition;
	}

	/** 测试中的商店同样从棋盘读池，显式绑定四种定义后才允许初始化。 */
	inline void ConfigureBoard(ACastleBoard* Board)
	{
		Board->BuildingDefinitions = { MakeDefinition(ECastleBuildingType::Road), MakeDefinition(ECastleBuildingType::ArcherTower),
			MakeDefinition(ECastleBuildingType::CannonTower), MakeDefinition(ECastleBuildingType::Farm) };
	}
}

#endif
