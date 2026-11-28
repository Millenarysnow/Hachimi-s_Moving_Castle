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

	/** 从一间已建房向六个面延伸，真实登记占格，用于容量和主塔升级的回归。 */
	inline bool FillRoomCells(ACastleBoard& Board, ACastleBuildingBase& Seed, const FCastleBuildingDescriptor& Card,
		const int32 TargetCount, FText& OutReason, const TOptional<FIntVector>& Reserved = {})
	{
		const FIntVector Offsets[] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
		const ECastleGridFace Faces[] = {ECastleGridFace::PositiveX, ECastleGridFace::NegativeX,
			ECastleGridFace::PositiveY, ECastleGridFace::NegativeY, ECastleGridFace::Up, ECastleGridFace::Down};
		// 放置允许建筑从棋盘边缘伸出，测试布局使用与正式入口相同的坐标边界。
		FIntPoint Min, Max;
		Board.GetBoardExtent(Min, Max);
		const FIntVector TowerBase = Board.GetBoardCenterCoordinate();
		TArray<FIntVector> Pending = Seed.GetOccupiedCoordinates();
		TSet<FIntVector> Visited;
		for (int32 Index = 0; Index < Pending.Num() && Board.GetOccupiedRoomCellCount() < TargetCount; ++Index)
		{
			const FIntVector Cell = Pending[Index];
			for (int32 Side = 0; Side < UE_ARRAY_COUNT(Offsets) && Board.GetOccupiedRoomCellCount() < TargetCount; ++Side)
			{
				const FIntVector Adjacent = Cell + Offsets[Side];
				if ((Reserved.IsSet() && Reserved.GetValue() == Adjacent) || Visited.Contains(Adjacent)
					|| Adjacent.X < Min.X - Board.MaxOverhangCells || Adjacent.X > Max.X + Board.MaxOverhangCells
					|| Adjacent.Y < Min.Y - Board.MaxOverhangCells || Adjacent.Y > Max.Y + Board.MaxOverhangCells
					|| Adjacent.Z < Board.GridOrigin.Z || Adjacent.Z >= Board.GridOrigin.Z + Board.GetUnlockedLayerCount()
					|| (Adjacent.X == TowerBase.X && Adjacent.Y == TowerBase.Y)) { continue; }
				Visited.Add(Adjacent);
				if (!Board.GetBuildingAt(Adjacent))
				{
					FCastleGridCursorTarget Cursor;
					Cursor.bValid = true;
					Cursor.Surface = ECastleGridCursorSurface::Building;
					Cursor.Face = Faces[Side];
					Cursor.Cell = Cell;
					Cursor.AdjacentCell = Adjacent;
					if (!CompletePlacement(Board.PlaceBuildingAtCursor(Cursor, Card, false, OutReason))) { return false; }
				}
				Pending.Add(Adjacent);
			}
		}
		if (Board.GetOccupiedRoomCellCount() == TargetCount) { return true; }
		OutReason = FText::Format(INVTEXT("测试布局填入 {0} 格，目标为 {1} 格。"), Board.GetOccupiedRoomCellCount(), TargetCount);
		return false;
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
		case ECastleBuildingType::NormalRoom:
			Classes = { ACastleNormalRoomBuilding::StaticClass() };
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
		case ECastleBuildingType::ShieldTower:
			// 显式创建盾塔五级输入，使其参加已有放置与合成回归，不使用其他塔替代。
			Classes = { ACastleShieldTowerLevel1::StaticClass(), ACastleShieldTowerLevel2::StaticClass(),
				ACastleShieldTowerLevel3::StaticClass(), ACastleShieldTowerLevel4::StaticClass(), ACastleShieldTowerLevel5::StaticClass() };
			break;
		}
		for (int32 Index = 0; Index < Classes.Num(); ++Index)
		{
			FCastleBuildingLevelDefinition Entry;
			Entry.Level = Index + 1;
			Entry.BuildingClass = Classes[Index];
			// 测试定义也显式提供二级、三级垂直类，覆盖正式定义的强校验；
			// 原生测试使用同一等级 C++ 类作为模型夹具，不引入未存在的测试蓝图。
			if (Index == 1 || Index == 2)
			{
				Entry.VerticalBuildingClass = Classes[Index];
			}
			Definition->Levels.Add(Entry);
		}
		if (Type == ECastleBuildingType::ArcherTower || Type == ECastleBuildingType::CannonTower)
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

	/** 测试中的商店同样从棋盘读池，显式绑定五种定义后才允许初始化。 */
	inline void ConfigureBoard(ACastleBoard* Board)
	{
		Board->BuildingDefinitions = { MakeDefinition(ECastleBuildingType::NormalRoom), MakeDefinition(ECastleBuildingType::ArcherTower),
			MakeDefinition(ECastleBuildingType::CannonTower), MakeDefinition(ECastleBuildingType::Farm),
			MakeDefinition(ECastleBuildingType::ShieldTower) };
	}
}

#endif
