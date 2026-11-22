// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBoard.h"

#include "CastleGroundTileActor.h"
#include "CastleTowerSideGridComponent.h"
#include "Components/StaticMeshComponent.h"

bool ACastleBoard::ValidatePlacementSurface(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides)
	{
		// 除了整个侧格集合，核对它确实属于本棋盘的主塔；旧地面不参与此状态判断。
		if (!IsValid(MainTowerActor) || !IsValid(TowerSideGrid) || TowerSideGrid->GetOwner() != MainTowerActor.Get()
			|| !TowerSideGrid->IsRegistered() || TowerSideGrid->GetCells().Num() != GetUnlockedLayerCount() * 4)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("棋盘 %s 的主塔 %s 或侧格配置未完整就绪。"),
				*GetPathName(), *GetPathNameSafe(MainTowerActor.Get())));
			return false;
		}
		return TowerSideGrid->ValidatePresentation(OutFailureReason);
	}
	if (PlacementSurfaceMode != ECastlePlacementSurfaceMode::GroundBoard
		|| GeneratedGroundCells.IsEmpty() || GroundTiles.Num() != GeneratedGroundCells.Num())
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("棋盘 %s 的建造面模式无效或地面未完整生成。"), *GetPathName()));
		return false;
	}
	for (const FCastleGroundCell& Cell : GeneratedGroundCells)
	{
		const TObjectPtr<ACastleGroundTileActor>* Tile = GroundTiles.Find(Cell.Coordinate);
		if (Tile == nullptr || !IsValid(Tile->Get()))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("棋盘 %s 的地面格 %s 未登记或已失效。"),
				*GetPathName(), *Cell.Coordinate.ToString()));
			return false;
		}
	}
	return true;
}

bool ACastleBoard::SetPlacementSurfacePresentationHidden(const bool bInHidden, FText& OutFailureReason)
{
	// 导演期望整批显隐：损坏时拒绝阶段转换，不能只更改可用的格子。
	if (!ValidatePlacementSurface(OutFailureReason)) { return false; }
	if (PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides)
	{
		return TowerSideGrid->SetPresentationHidden(bInHidden, OutFailureReason);
	}
	SetGroundPresentationHidden(bInHidden);
	return true;
}

bool ACastleBoard::IsPlacementSurfacePresentationHidden() const
{
	return PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides
		? IsValid(TowerSideGrid) && TowerSideGrid->IsPresentationHidden() : IsGroundPresentationHidden();
}

const FCastleTowerSideCell* ACastleBoard::FindTowerSideCell(const FIntVector& TowerCell, const ECastleGridFace Face) const
{
	if (PlacementSurfaceMode != ECastlePlacementSurfaceMode::MainTowerSides || !IsValid(MainTowerActor)
		|| !IsValid(TowerSideGrid) || !TowerSideGrid->IsRegistered())
	{
		return nullptr;
	}
	const FCastleTowerSideCell* Cell = TowerSideGrid->FindCell(TowerCell, Face);
	return Cell && IsValid(Cell->Panel) && Cell->Panel->IsRegistered() ? Cell : nullptr;
}

bool ACastleBoard::IsMainTowerCellReserved(const FIntVector& Coordinate) const
{
	if (PlacementSurfaceMode != ECastlePlacementSurfaceMode::MainTowerSides) { return false; }
	const FIntVector BaseCell = GetBoardCenterCoordinate();
	return Coordinate.X == BaseCell.X && Coordinate.Y == BaseCell.Y;
}
