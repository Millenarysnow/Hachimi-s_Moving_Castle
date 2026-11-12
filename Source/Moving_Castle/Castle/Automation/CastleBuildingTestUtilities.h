// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CastleBuildingDefinition.h"
#include "CastleTowerAttackDataAsset.h"
#include "AssemblyFlightComponent.h"
#include "CastleBuildings.h"
#include "CastleBoard.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MassEntityConfigAsset.h"
#include "Units/Mass/Projectiles/CastleMassHomingProjectileTrait.h"

namespace CastleBuildingTests
{
	/**
	 * 为独立逻辑测试显式提供固定美术模型，避免测试依赖正式蓝图资产的编辑状态。
	 * 只有主动创建此夹具的世界才会安装生成回调；模型总是作为测试输入添加，
	 * 不判断是否缺模型，也不向生产建筑类或默认对象写入任何替代资源。
	 */
	class FScopedNativeBuildingModels
	{
	public:
		explicit FScopedNativeBuildingModels(UWorld* InWorld) : World(InWorld)
		{
			SpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([](AActor* Actor)
			{
				ACastleBuildingBase* Building = Cast<ACastleBuildingBase>(Actor);
				if (!Building || Building->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)) { return; }
				UStaticMeshComponent* Model = NewObject<UStaticMeshComponent>(Building, TEXT("TestBuildingModel"));
				Model->SetupAttachment(Building->GetRootComponent());
				Model->SetCanEverAffectNavigation(false);
				Model->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Model->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				// 固定模型底面在 Actor 局部 Z=0，便于独立验证棋盘锚点而不复制生产尺寸计算。
				Model->SetRelativeLocation(FVector(0.0, 0.0, 50.0));
				Building->AddInstanceComponent(Model);
				Model->RegisterComponent();
			}));
		}

		~FScopedNativeBuildingModels()
		{
			if (IsValid(World)) { World->RemoveOnActorSpawnedHandler(SpawnHandle); }
		}

	private:
		UWorld* World;
		FDelegateHandle SpawnHandle;
	};

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
		if (Type == ECastleBuildingType::ArcherTower)
		{
			// 箭塔测试定义显式创建完整的攻击数据输入，不借助生产默认对象补齐配置。
			UCastleTowerAttackDataAsset* AttackData = NewObject<UCastleTowerAttackDataAsset>(Definition);
			AttackData->ProjectileConfiguration = NewObject<UMassEntityConfigAsset>(AttackData);
			AttackData->ProjectileConfiguration->GetMutableConfig().AddTrait(
				*NewObject<UCastleMassHomingProjectileTrait>(AttackData->ProjectileConfiguration));
			for (int32 Level = 1; Level <= 5; ++Level)
			{
				FCastleTowerAttackLevelData Entry;
				Entry.Level = Level;
				Entry.AttacksPerSecond = 1.0f;
				Entry.ProjectileDamage = 10.0f;
				Entry.AttackRadius = 1000.0f;
				AttackData->Levels.Add(Entry);
			}
			Definition->AttackData = AttackData;
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
