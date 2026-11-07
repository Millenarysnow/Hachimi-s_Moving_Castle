// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBoard.h"

#include "CastleBackpackComponent.h"
#include "CastleBuildingBase.h"
#include "CastleBuildings.h"
#include "CastleGroundGenerator.h"
#include "Engine/World.h"
#include "Moving_Castle.h"

ACastleBoard::ACastleBoard()
{
	// 棋盘本身只维护逻辑状态，不需要 Tick，也不在这里创建任何美术组件。
	PrimaryActorTick.bCanEverTick = false;
	GroundGeneratorClass = UCastleRandomGroundGenerator::StaticClass();
}
void ACastleBoard::BeginPlay()
{
	Super::BeginPlay();

	if (bGenerateGroundOnBeginPlay)
	{
		FText FailureReason;
		if (!GenerateGround(FailureReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 BeginPlay 生成地面失败：%s"), *FailureReason.ToString());
		}
	}
}

bool ACastleBoard::GenerateGround(FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (GroundGeneratorClass == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("棋盘没有配置地面生成策略类。"));
		return false;
	}

	if (GridSize.X <= 0 || GridSize.Y <= 0 || GridSize.Z <= 0)
	{
		OutFailureReason = FText::FromString(TEXT("棋盘尺寸必须在 X、Y、Z 三个方向都大于 0。"));
		return false;
	}

	if (BuildingByCoordinate.Num() > 0)
	{
		OutFailureReason = FText::FromString(TEXT("场上已经存在建筑，不能直接重新生成地面。"));
		return false;
	}

	UCastleGroundGenerator* Generator = NewObject<UCastleGroundGenerator>(this, GroundGeneratorClass);
	if (Generator == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("无法创建地面生成策略对象。"));
		return false;
	}

	FCastleGroundGenerationRequest Request;
	Request.GridSize = GridSize;
	Request.Origin = GridOrigin;
	Request.Seed = GroundSeed;
	Request.bReserveRoadStartTile = bReserveRoadStartTile;

	const FCastleGroundGenerationResult Result = Generator->GenerateGround(Request);
	return ApplyGroundGenerationResult(Result, OutFailureReason);
}

bool ACastleBoard::ApplyGroundGenerationResult(const FCastleGroundGenerationResult& Result, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (BuildingByCoordinate.Num() > 0)
	{
		OutFailureReason = FText::FromString(TEXT("场上已经存在建筑，不能替换现有地面结果。"));
		return false;
	}

	if (GridSize.X <= 0 || GridSize.Y <= 0 || GridSize.Z <= 0)
	{
		OutFailureReason = FText::FromString(TEXT("棋盘尺寸无效，无法接收地面结果。"));
		return false;
	}

	const int64 ExpectedCellCount = static_cast<int64>(GridSize.X) * static_cast<int64>(GridSize.Y);
	if (Result.Cells.Num() != ExpectedCellCount)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("地面结果数量不完整：需要 %lld 个，实际收到 %d 个。"), ExpectedCellCount, Result.Cells.Num()));
		return false;
	}

	TSet<FIntVector> UniqueCoordinates;
	UniqueCoordinates.Reserve(Result.Cells.Num());

	for (const FCastleGroundCell& Cell : Result.Cells)
	{
		if (!IsCoordinateWithinBoard(Cell.Coordinate) || Cell.Coordinate.Z != GridOrigin.Z)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("地面结果包含越界或非地面层坐标 (%d, %d, %d)。"), Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z));
			return false;
		}

		if (UniqueCoordinates.Contains(Cell.Coordinate))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("地面结果包含重复坐标 (%d, %d, %d)。"), Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z));
			return false;
		}
		UniqueCoordinates.Add(Cell.Coordinate);

		if (Cell.Number < 0 || Cell.Number > 9)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("地面坐标 (%d, %d, %d) 的数字 %d 无效。"), Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z, Cell.Number));
			return false;
		}

		if ((Cell.Color == ECastleTileColor::Gray && Cell.Number != 0) || (Cell.Color != ECastleTileColor::Gray && Cell.Number == 0))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("地面坐标 (%d, %d, %d) 的颜色和数字组合无效。"), Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z));
			return false;
		}
	}

	// 所有检查完成后才替换旧数组，保证错误结果不会留下半份地面。
	GeneratedGroundCells = Result.Cells;
	return true;
}

bool ACastleBoard::TryGetGroundCell(const FIntVector& Coordinate, FCastleGroundCell& OutCell) const
{
	for (const FCastleGroundCell& Cell : GeneratedGroundCells)
	{
		if (Cell.Coordinate == Coordinate)
		{
			OutCell = Cell;
			return true;
		}
	}

	return false;
}

bool ACastleBoard::IsCoordinateWithinBoard(const FIntVector& Coordinate) const
{
	const FIntVector RelativeCoordinate = Coordinate - GridOrigin;
	return RelativeCoordinate.X >= 0 && RelativeCoordinate.X < GridSize.X
		&& RelativeCoordinate.Y >= 0 && RelativeCoordinate.Y < GridSize.Y
		&& RelativeCoordinate.Z >= 0 && RelativeCoordinate.Z < GridSize.Z;
}

bool ACastleBoard::CanPlaceBuilding(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();

	if (!ValidateBuildingDescriptor(Descriptor, OutFailureReason))
	{
		return false;
	}

	if (!IsCoordinateWithinBoard(Coordinate))
	{
		OutFailureReason = FText::FromString(TEXT("目标坐标超出棋盘边界。"));
		return false;
	}

	if (GetBuildingAt(Coordinate) != nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("目标坐标已经被建筑占用。"));
		return false;
	}

	FCastleGroundCell GroundCell;
	if (!TryGetGroundCellAtPlane(Coordinate, GroundCell) && Coordinate.Z == GridOrigin.Z)
	{
		OutFailureReason = FText::FromString(TEXT("目标地面格子尚未生成。"));
		return false;
	}

	if (Coordinate.Z > GridOrigin.Z)
	{
		FIntVector SupportCoordinate = Coordinate;
		--SupportCoordinate.Z;
		if (GetBuildingAt(SupportCoordinate) == nullptr)
		{
			OutFailureReason = FText::FromString(TEXT("上层建筑必须由紧贴下方的建筑支撑。"));
			return false;
		}
	}

	if (!HasPlacementMatch(Coordinate, Descriptor))
	{
		OutFailureReason = FText::FromString(TEXT("目标格或六面相邻格没有满足颜色/数字匹配规则的对象。"));
		return false;
	}

	return true;
}

ACastleBuildingBase* ACastleBoard::PlaceBuilding(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, bool bTryMerge, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (!CanPlaceBuilding(Coordinate, Descriptor, OutFailureReason))
	{
		return nullptr;
	}

	const TArray<FIntVector> SingleCellFootprint = { Coordinate };
	ACastleBuildingBase* NewBuilding = SpawnBuildingActor(Descriptor, SingleCellFootprint, OutFailureReason);
	if (NewBuilding == nullptr)
	{
		return nullptr;
	}

	if (!RegisterBuilding(NewBuilding, OutFailureReason))
	{
		NewBuilding->Destroy();
		return nullptr;
	}

	if (!bTryMerge)
	{
		return NewBuilding;
	}

	// 合成失败不会撤销已经合法放置的建筑；只有在替换流程确实出错时记录原因。
	return TryMergePlacedBuilding(NewBuilding, OutFailureReason);
}

ACastleBuildingBase* ACastleBoard::PlaceBuildingFromBackpack(const FIntVector& Coordinate, UCastleBackpackComponent* Backpack, int32 BackpackIndex, bool bTryMerge, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (Backpack == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("放置建筑时没有提供背包组件。"));
		return nullptr;
	}

	FCastleBuildingDescriptor Descriptor;
	if (!Backpack->TryGetItemAt(BackpackIndex, Descriptor))
	{
		OutFailureReason = FText::FromString(TEXT("指定的背包槽位不存在。"));
		return nullptr;
	}

	// 先做完整校验，再取出卡牌；这样失败时背包不会发生任何变化。
	if (!CanPlaceBuilding(Coordinate, Descriptor, OutFailureReason))
	{
		return nullptr;
	}

	FCastleBuildingDescriptor RemovedDescriptor;
	FText RemoveReason;
	if (!Backpack->TryRemoveItemAt(BackpackIndex, RemovedDescriptor, RemoveReason))
	{
		OutFailureReason = RemoveReason;
		return nullptr;
	}

	ACastleBuildingBase* PlacedBuilding = PlaceBuilding(Coordinate, RemovedDescriptor, bTryMerge, OutFailureReason);
	if (PlacedBuilding != nullptr)
	{
		return PlacedBuilding;
	}

	// 生成失败时恢复卡牌；恢复失败属于严重状态错误，必须显式记录而不是静默丢牌。
	FText RestoreReason;
	if (!Backpack->TryInsertItemAt(BackpackIndex, RemovedDescriptor, RestoreReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("从背包放置失败后的卡牌恢复失败：%s"), *RestoreReason.ToString());
	}

	return nullptr;
}

bool ACastleBoard::RemoveBuilding(ACastleBuildingBase* Building, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (Building == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("要移除的建筑为空。"));
		return false;
	}

	if (!UnregisterBuilding(Building, OutFailureReason))
	{
		return false;
	}

	if (!Building->Destroy())
	{
		OutFailureReason = FText::FromString(TEXT("建筑已从棋盘索引移除，但 Actor 销毁失败。"));
		return false;
	}

	return true;
}

bool ACastleBoard::RemoveBuildingToBackpack(ACastleBuildingBase* Building, UCastleBackpackComponent* Backpack, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (Building == nullptr || Backpack == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("移除建筑并放回背包时缺少建筑或背包。"));
		return false;
	}

	const FCastleBuildingDescriptor Descriptor = Building->GetBuildingDescriptor();
	int32 AddedIndex = INDEX_NONE;
	FText AddReason;
	if (!Backpack->TryAddItem(Descriptor, AddedIndex, AddReason))
	{
		OutFailureReason = AddReason;
		return false;
	}

	FText RemoveReason;
	if (RemoveBuilding(Building, RemoveReason))
	{
		return true;
	}

	// 棋盘移除失败时回滚刚加入的卡牌，避免出现 Actor 和背包状态不一致。
	FCastleBuildingDescriptor RolledBackDescriptor;
	FText RollbackReason;
	if (!Backpack->TryRemoveItemAt(AddedIndex, RolledBackDescriptor, RollbackReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("移除建筑回滚背包失败：%s"), *RollbackReason.ToString());
	}

	OutFailureReason = RemoveReason;
	return false;
}

int32 ACastleBoard::GetPlacedBuildingCount() const
{
	TSet<ACastleBuildingBase*> UniqueBuildings;
	for (const TPair<FIntVector, TObjectPtr<ACastleBuildingBase>>& Pair : BuildingByCoordinate)
	{
		if (Pair.Value != nullptr)
		{
			UniqueBuildings.Add(Pair.Value.Get());
		}
	}

	return UniqueBuildings.Num();
}

TArray<ACastleBuildingBase*> ACastleBoard::GetPlacedBuildings() const
{
	TSet<ACastleBuildingBase*> UniqueBuildings;
	for (const TPair<FIntVector, TObjectPtr<ACastleBuildingBase>>& Pair : BuildingByCoordinate)
	{
		if (Pair.Value != nullptr)
		{
			UniqueBuildings.Add(Pair.Value.Get());
		}
	}

	TArray<ACastleBuildingBase*> Result;
	Result.Reserve(UniqueBuildings.Num());
	for (ACastleBuildingBase* Building : UniqueBuildings)
	{
		Result.Add(Building);
	}

	return Result;
}

FVector ACastleBoard::GetGridWorldLocation(const FIntVector& Coordinate) const
{
	const FIntVector RelativeCoordinate = Coordinate - GridOrigin;
	return GetActorLocation() + FVector(
		static_cast<float>(RelativeCoordinate.X) * CellSize,
		static_cast<float>(RelativeCoordinate.Y) * CellSize,
		static_cast<float>(RelativeCoordinate.Z) * LayerHeight);
}

void ACastleBoard::GetFaceNeighborCoordinates(const FIntVector& Coordinate, TArray<FIntVector>& OutNeighbors) const
{
	OutNeighbors.Reset(6);
	OutNeighbors.Add(Coordinate + FIntVector(1, 0, 0));
	OutNeighbors.Add(Coordinate + FIntVector(-1, 0, 0));
	OutNeighbors.Add(Coordinate + FIntVector(0, 1, 0));
	OutNeighbors.Add(Coordinate + FIntVector(0, -1, 0));
	OutNeighbors.Add(Coordinate + FIntVector(0, 0, 1));
	OutNeighbors.Add(Coordinate + FIntVector(0, 0, -1));
}

ACastleBuildingBase* ACastleBoard::GetBuildingAt(const FIntVector& Coordinate) const
{
	if (const TObjectPtr<ACastleBuildingBase>* FoundBuilding = BuildingByCoordinate.Find(Coordinate))
	{
		return FoundBuilding->Get();
	}

	return nullptr;
}

bool ACastleBoard::RegisterBuilding(ACastleBuildingBase* Building, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (Building == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("不能注册空建筑。"));
		return false;
	}

	const TArray<FIntVector> OccupiedCoordinates = Building->GetOccupiedCoordinates();
	if (OccupiedCoordinates.Num() == 0)
	{
		OutFailureReason = FText::FromString(TEXT("建筑没有占用坐标，不能写入棋盘索引。"));
		return false;
	}

	TSet<FIntVector> UniqueCoordinates;
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		if (!IsCoordinateWithinBoard(Coordinate))
		{
			OutFailureReason = FText::FromString(TEXT("建筑占用坐标超出棋盘边界。"));
			return false;
		}

		if (UniqueCoordinates.Contains(Coordinate))
		{
			OutFailureReason = FText::FromString(TEXT("建筑占用坐标存在重复值。"));
			return false;
		}
		UniqueCoordinates.Add(Coordinate);

		if (GetBuildingAt(Coordinate) != nullptr)
		{
			OutFailureReason = FText::FromString(TEXT("建筑占用坐标与现有建筑重叠。"));
			return false;
		}
	}

	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		BuildingByCoordinate.Add(Coordinate, Building);
	}

	return true;
}

bool ACastleBoard::UnregisterBuilding(ACastleBuildingBase* Building, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (Building == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("不能注销空建筑。"));
		return false;
	}

	const TArray<FIntVector> OccupiedCoordinates = Building->GetOccupiedCoordinates();
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		if (GetBuildingAt(Coordinate) != Building)
		{
			OutFailureReason = FText::FromString(TEXT("建筑坐标索引不完整，拒绝注销以避免破坏棋盘状态。"));
			return false;
		}
	}

	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		BuildingByCoordinate.Remove(Coordinate);
	}

	return true;
}

ACastleBuildingBase* ACastleBoard::SpawnBuildingActor(const FCastleBuildingDescriptor& Descriptor, const TArray<FIntVector>& OccupiedCoordinates, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (Descriptor.BuildingClass == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("生成建筑 Actor 时没有提供具体类。"));
		return nullptr;
	}

	if (OccupiedCoordinates.Num() == 0)
	{
		OutFailureReason = FText::FromString(TEXT("生成建筑 Actor 时没有提供占用坐标。"));
		return nullptr;
	}

	FVector SpawnLocation = FVector::ZeroVector;
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		SpawnLocation += GetGridWorldLocation(Coordinate);
	}
	SpawnLocation /= static_cast<float>(OccupiedCoordinates.Num());

	if (GetWorld() == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("棋盘不在有效 World 中，无法生成建筑 Actor。"));
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACastleBuildingBase* NewBuilding = GetWorld()->SpawnActor<ACastleBuildingBase>(Descriptor.BuildingClass, SpawnLocation, FRotator::ZeroRotator, SpawnParameters);
	if (NewBuilding == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("World 生成建筑 Actor 失败。"));
		return nullptr;
	}

	if (!NewBuilding->InitializeBuilding(Descriptor, OccupiedCoordinates))
	{
		NewBuilding->Destroy();
		OutFailureReason = FText::FromString(TEXT("建筑 Actor 初始化数据失败。"));
		return nullptr;
	}

	return NewBuilding;
}

ACastleBuildingBase* ACastleBoard::TryMergePlacedBuilding(ACastleBuildingBase* Building, FText& OutFailureReason)
{
	if (Building == nullptr)
	{
		return nullptr;
	}

	ACastleBuildingBase* CurrentBuilding = Building;
	while (CurrentBuilding != nullptr && CurrentBuilding->CanMergeToNextLevel())
	{
		ACastleBuildingBase* MergePartner = FindMergePartner(CurrentBuilding);
		if (MergePartner == nullptr)
		{
			break;
		}

		TArray<FIntVector> MergedFootprint;
		if (!BuildMergedFootprint(CurrentBuilding, MergePartner, MergedFootprint))
		{
			// FindMergePartner 已经做过同样检查，走到这里说明索引状态被外部破坏。
			OutFailureReason = FText::FromString(TEXT("合成占用形状校验失败，棋盘索引可能已被外部修改。"));
			break;
		}

		FText MergeReason;
		ACastleBuildingBase* MergedBuilding = MergeBuildings(CurrentBuilding, MergePartner, MergedFootprint, MergeReason);
		if (MergedBuilding == nullptr)
		{
			OutFailureReason = MergeReason;
			// 合成替换失败时保留合法的原有建筑，避免静默丢失场上对象。
			return CurrentBuilding;
		}

		CurrentBuilding = MergedBuilding;
	}

	return CurrentBuilding;
}

ACastleBuildingBase* ACastleBoard::FindMergePartner(ACastleBuildingBase* Building) const
{
	if (Building == nullptr || !Building->CanMergeToNextLevel())
	{
		return nullptr;
	}

	TSet<ACastleBuildingBase*> Candidates;
	const TArray<FIntVector> OccupiedCoordinates = Building->GetOccupiedCoordinates();
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		TArray<FIntVector> Neighbors;
		GetFaceNeighborCoordinates(Coordinate, Neighbors);
		for (const FIntVector& NeighborCoordinate : Neighbors)
		{
			ACastleBuildingBase* Candidate = GetBuildingAt(NeighborCoordinate);
			if (Candidate != nullptr && Candidate != Building)
			{
				Candidates.Add(Candidate);
			}
		}
	}

	for (ACastleBuildingBase* Candidate : Candidates)
	{
		TArray<FIntVector> MergedFootprint;
		if (Building->CanMergeWith(Candidate) && BuildMergedFootprint(Building, Candidate, MergedFootprint))
		{
			return Candidate;
		}
	}

	return nullptr;
}

bool ACastleBoard::BuildMergedFootprint(const ACastleBuildingBase* First, const ACastleBuildingBase* Second, TArray<FIntVector>& OutFootprint) const
{
	OutFootprint.Reset();

	if (First == nullptr || Second == nullptr || !First->CanMergeWith(Second))
	{
		return false;
	}

	const int32 CurrentLevel = First->GetBuildingLevel();
	if (CurrentLevel < 1 || CurrentLevel > 3)
	{
		return false;
	}

	const int32 ExpectedSingleCount = ACastleBuildingBase::GetExpectedFootprintCellCount(CurrentLevel);
	const TArray<FIntVector> FirstCoordinates = First->GetOccupiedCoordinates();
	const TArray<FIntVector> SecondCoordinates = Second->GetOccupiedCoordinates();
	if (FirstCoordinates.Num() != ExpectedSingleCount || SecondCoordinates.Num() != ExpectedSingleCount)
	{
		return false;
	}

	TSet<FIntVector> UniqueCoordinates;
	for (const FIntVector& Coordinate : FirstCoordinates)
	{
		UniqueCoordinates.Add(Coordinate);
	}
	for (const FIntVector& Coordinate : SecondCoordinates)
	{
		if (UniqueCoordinates.Contains(Coordinate))
		{
			return false;
		}
		UniqueCoordinates.Add(Coordinate);
	}

	const int32 ExpectedMergedCount = ExpectedSingleCount * 2;
	if (UniqueCoordinates.Num() != ExpectedMergedCount)
	{
		return false;
	}

	FIntVector Minimum = *UniqueCoordinates.CreateConstIterator();
	FIntVector Maximum = Minimum;
	for (const FIntVector& Coordinate : UniqueCoordinates)
	{
		Minimum.X = FMath::Min(Minimum.X, Coordinate.X);
		Minimum.Y = FMath::Min(Minimum.Y, Coordinate.Y);
		Minimum.Z = FMath::Min(Minimum.Z, Coordinate.Z);
		Maximum.X = FMath::Max(Maximum.X, Coordinate.X);
		Maximum.Y = FMath::Max(Maximum.Y, Coordinate.Y);
		Maximum.Z = FMath::Max(Maximum.Z, Coordinate.Z);
	}

	const FIntVector Dimensions = Maximum - Minimum + FIntVector(1, 1, 1);
	bool bValidShape = false;
	switch (CurrentLevel)
	{
	case 1:
		// 一级合成二级只能在水平 X/Y 方向形成 2x1，不允许用上下堆叠完成合成。
		bValidShape = Dimensions.Z == 1 && ((Dimensions.X == 2 && Dimensions.Y == 1) || (Dimensions.X == 1 && Dimensions.Y == 2));
		break;
	case 2:
		// 二级合成三级固定为同一平面 2x2 方形。
		bValidShape = Dimensions == FIntVector(2, 2, 1);
		break;
	case 3:
		// 三级合成四级固定为 2x2x2 立方体。
		bValidShape = Dimensions == FIntVector(2, 2, 2);
		break;
	default:
		break;
	}

	if (!bValidShape)
	{
		return false;
	}

	for (int32 X = Minimum.X; X <= Maximum.X; ++X)
	{
		for (int32 Y = Minimum.Y; Y <= Maximum.Y; ++Y)
		{
			for (int32 Z = Minimum.Z; Z <= Maximum.Z; ++Z)
			{
				if (!UniqueCoordinates.Contains(FIntVector(X, Y, Z)))
				{
					return false;
				}
			}
		}
	}

	OutFootprint = UniqueCoordinates.Array();
	OutFootprint.Sort([](const FIntVector& Left, const FIntVector& Right)
	{
		if (Left.X != Right.X)
		{
			return Left.X < Right.X;
		}
		if (Left.Y != Right.Y)
		{
			return Left.Y < Right.Y;
		}
		return Left.Z < Right.Z;
	});
	return true;
}

ACastleBuildingBase* ACastleBoard::MergeBuildings(ACastleBuildingBase* First, ACastleBuildingBase* Second, const TArray<FIntVector>& MergedFootprint, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	TArray<FIntVector> VerifiedFootprint;
	if (First == nullptr || Second == nullptr || !BuildMergedFootprint(First, Second, VerifiedFootprint) || VerifiedFootprint != MergedFootprint)
	{
		OutFailureReason = FText::FromString(TEXT("合成替换收到无效的建筑或占用形状。"));
		return nullptr;
	}

	const int32 NextLevel = First->GetBuildingLevel() + 1;
	const TSubclassOf<ACastleBuildingBase> NextBuildingClass = First->GetClassForLevel(NextLevel);
	if (NextBuildingClass == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("当前建筑族没有提供下一级具体 Actor 类。"));
		return nullptr;
	}

	for (const FIntVector& Coordinate : First->GetOccupiedCoordinates())
	{
		if (GetBuildingAt(Coordinate) != First)
		{
			OutFailureReason = FText::FromString(TEXT("合成前第一个建筑的坐标索引不完整。"));
			return nullptr;
		}
	}
	for (const FIntVector& Coordinate : Second->GetOccupiedCoordinates())
	{
		if (GetBuildingAt(Coordinate) != Second)
		{
			OutFailureReason = FText::FromString(TEXT("合成前第二个建筑的坐标索引不完整。"));
			return nullptr;
		}
	}

	FCastleBuildingDescriptor MergedDescriptor;
	MergedDescriptor.BuildingClass = NextBuildingClass;
	MergedDescriptor.BuildingType = First->GetBuildingType();
	MergedDescriptor.Color = First->GetBuildingColor();
	MergedDescriptor.Number = First->GetBuildingNumber();
	MergedDescriptor.Level = NextLevel;

	// 先生成新 Actor，成功后才注销和销毁旧 Actor，避免生成失败时丢失原建筑。
	ACastleBuildingBase* MergedBuilding = SpawnBuildingActor(MergedDescriptor, MergedFootprint, OutFailureReason);
	if (MergedBuilding == nullptr)
	{
		return nullptr;
	}

	FText UnregisterReason;
	if (!UnregisterBuilding(First, UnregisterReason))
	{
		MergedBuilding->Destroy();
		OutFailureReason = UnregisterReason;
		return nullptr;
	}
	if (!UnregisterBuilding(Second, UnregisterReason))
	{
		// 第一个建筑已经注销时恢复它的索引；旧 Actor 仍然存活，因此可以安全恢复。
		FText RestoreReason;
		RegisterBuilding(First, RestoreReason);
		MergedBuilding->Destroy();
		OutFailureReason = UnregisterReason;
		return nullptr;
	}

	FText RegisterReason;
	if (!RegisterBuilding(MergedBuilding, RegisterReason))
	{
		FText RestoreFirstReason;
		FText RestoreSecondReason;
		RegisterBuilding(First, RestoreFirstReason);
		RegisterBuilding(Second, RestoreSecondReason);
		MergedBuilding->Destroy();
		OutFailureReason = RegisterReason;
		return nullptr;
	}

	First->Destroy();
	Second->Destroy();
	return MergedBuilding;
}

bool ACastleBoard::ValidateBuildingDescriptor(const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();

	if (!Descriptor.HasBuildingClass())
	{
		OutFailureReason = FText::FromString(TEXT("建筑描述没有具体建筑类。"));
		return false;
	}

	if (!Descriptor.BuildingClass->IsChildOf(ACastleBuildingBase::StaticClass()) || Descriptor.BuildingClass->HasAnyClassFlags(CLASS_Abstract))
	{
		OutFailureReason = FText::FromString(TEXT("建筑描述必须使用非抽象的 ACastleBuildingBase 子类。"));
		return false;
	}

	const ACastleBuildingBase* DefaultBuilding = Descriptor.BuildingClass->GetDefaultObject<ACastleBuildingBase>();
	if (DefaultBuilding == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("建筑类默认对象无效。"));
		return false;
	}

	if (DefaultBuilding->GetBuildingType() != Descriptor.BuildingType || DefaultBuilding->GetBuildingLevel() != Descriptor.Level)
	{
		OutFailureReason = FText::FromString(TEXT("建筑描述的类别或等级与具体 Actor 类不一致。"));
		return false;
	}

	if (Descriptor.Number < 0 || Descriptor.Number > 9)
	{
		OutFailureReason = FText::FromString(TEXT("建筑数字必须在 0 到 9 之间。"));
		return false;
	}

	if (Descriptor.BuildingType == ECastleBuildingType::Road)
	{
		if (Descriptor.Color != ECastleTileColor::Gray || Descriptor.Number != 0 || Descriptor.Level != 1)
		{
			OutFailureReason = FText::FromString(TEXT("道路描述必须是灰色 0 号 1 级。"));
			return false;
		}
	}
	else if (Descriptor.Color == ECastleTileColor::Gray || Descriptor.Number == 0)
	{
		OutFailureReason = FText::FromString(TEXT("功能建筑描述不能使用灰色或 0 号属性。"));
		return false;
	}

	return true;
}

bool ACastleBoard::TryGetGroundCellAtPlane(const FIntVector& Coordinate, FCastleGroundCell& OutCell) const
{
	if (Coordinate.Z != GridOrigin.Z)
	{
		return false;
	}

	return TryGetGroundCell(Coordinate, OutCell);
}

bool ACastleBoard::HasPlacementMatch(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor) const
{
	const bool bIsRoad = Descriptor.BuildingType == ECastleBuildingType::Road;

	// 地面只存在于 Z=0 平面，只有在该平面放置时才把目标地面本身作为匹配来源。
	if (Coordinate.Z == GridOrigin.Z)
	{
		FCastleGroundCell TargetGroundCell;
		if (TryGetGroundCellAtPlane(Coordinate, TargetGroundCell))
		{
			if (bIsRoad ? TargetGroundCell.IsRoadTile() : (TargetGroundCell.Color == Descriptor.Color || TargetGroundCell.Number == Descriptor.Number))
			{
				return true;
			}
		}
	}

	TArray<FIntVector> Neighbors;
	GetFaceNeighborCoordinates(Coordinate, Neighbors);
	for (const FIntVector& NeighborCoordinate : Neighbors)
	{
		if (!IsCoordinateWithinBoard(NeighborCoordinate))
		{
			continue;
		}

		if (ACastleBuildingBase* NeighborBuilding = GetBuildingAt(NeighborCoordinate))
		{
			if (bIsRoad)
			{
				if (NeighborBuilding->IsRoad())
				{
					return true;
				}
			}
			else if (!NeighborBuilding->IsRoad() && (NeighborBuilding->GetBuildingColor() == Descriptor.Color || NeighborBuilding->GetBuildingNumber() == Descriptor.Number))
			{
				return true;
			}

			continue;
		}

		if (NeighborCoordinate.Z == GridOrigin.Z)
		{
			FCastleGroundCell NeighborGroundCell;
			if (TryGetGroundCellAtPlane(NeighborCoordinate, NeighborGroundCell))
			{
				if (bIsRoad ? NeighborGroundCell.IsRoadTile() : (NeighborGroundCell.Color == Descriptor.Color || NeighborGroundCell.Number == Descriptor.Number))
				{
					return true;
				}
			}
		}
	}

	return false;
}

