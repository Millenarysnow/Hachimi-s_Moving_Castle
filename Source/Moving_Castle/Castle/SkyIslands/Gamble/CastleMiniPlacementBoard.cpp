#include "CastleMiniPlacementBoard.h"

#include "CastleBackpackComponent.h"

namespace
{
	const FIntVector Faces[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

	/** 有符号坐标边界处仍可检查存在的面，不执行溢出的加法。 */
	bool TryAdjacentCell(const FIntVector& Cell, const FIntVector& Face, FIntVector& OutCell)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int64 Value = static_cast<int64>(Cell[Axis]) + Face[Axis];
			if (Value < MIN_int32 || Value > MAX_int32) { return false; }
			OutCell[Axis] = static_cast<int32>(Value);
		}
		return OutCell.Z >= 0;
	}

	bool MatchesCard(const FCastleBuildingDescriptor& A, const FCastleBuildingDescriptor& B)
	{
		// 使用原始色号，徽章修改后的战斗数字不参与拼放。
		return A.Color == B.Color || A.Number == B.Number;
	}
}

bool FCastleMiniPlacementBoard::InitializeGround(TConstArrayView<FCastleGroundCell> Cells, FIntPoint GridSize, FText& OutReason)
{
	if (!GroundCells.IsEmpty() || GridSize.X <= 0 || GridSize.Y <= 0 || Cells.Num() != int64(GridSize.X) * GridSize.Y)
	{
		OutReason = INVTEXT("微缩地面格数量错误或已经初始化。");
		return false;
	}
	TMap<FIntVector, FCastleGroundCell> Generated;
	for (const FCastleGroundCell& Cell : Cells)
	{
		const FIntVector& Position = Cell.Coordinate;
		if (Position.Z != 0 || Position.X < 0 || Position.X >= GridSize.X || Position.Y < 0 || Position.Y >= GridSize.Y
			|| Cell.Color < ECastleTileColor::Red || Cell.Color > ECastleTileColor::Purple || Cell.Number < 1 || Cell.Number > 9
			|| Generated.Contains(Position))
		{
			OutReason = FText::Format(INVTEXT("微缩地面格 {0} 重复、越界或色号无效。"), FText::FromString(Position.ToString()));
			return false;
		}
		Generated.Add(Position, Cell);
	}
	GroundCells = MoveTemp(Generated);
	return true;
}

bool FCastleMiniPlacementBoard::CanPlace(const FCastleBuildingDescriptor& Card, const FIntVector& Cell, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (Cell.Z < 0 || Occupied.Contains(Cell))
	{
		OutReason = INVTEXT("该微缩格在底板下方或已被占用。");
		return false;
	}
	// 地面格只支持自身正上方的首层房间；高层和悬挑只查询房间的六面邻居。
	if (const FCastleGroundCell* Ground = GroundCells.Find(Cell))
	{
		if (Ground->Color == Card.Color || Ground->Number == Card.Number) { return true; }
	}
	for (const FIntVector& Face : Faces)
	{
		FIntVector Neighbor;
		if (!TryAdjacentCell(Cell, Face, Neighbor)) { continue; }
		const int32* Index = Occupied.Find(Neighbor);
		if (Index && MatchesCard(Card, Placements[*Index].Card)) { return true; }
	}
	OutReason = INVTEXT("房间必须与下方地面格或面相邻的已放房间同色或同数字。");
	return false;
}

bool FCastleMiniPlacementBoard::TryPlace(UCastleBackpackComponent& Backpack, const int32 BackpackIndex,
	const FIntVector& Cell, FText& OutReason)
{
	if (bMutating)
	{
		OutReason = INVTEXT("微缩区正在处理房间卡。");
		return false;
	}
	TGuardValue<bool> Mutation(bMutating, true);
	FCastleBuildingDescriptor Card;
	if (!Backpack.TryGetItemAt(BackpackIndex, Card))
	{
		OutReason = INVTEXT("选中的背包槽位没有房间卡。");
		return false;
	}
	if (!CanPlace(Card, Cell, OutReason)) { return false; }
	if (!Backpack.TryConsumeItemAt(BackpackIndex, Card, OutReason)) { return false; }
	Occupied.Add(Cell, Placements.Add(FCastleMiniPlacement{Cell, Card}));
	return true;
}

bool FCastleMiniPlacementBoard::TryReturnLast(UCastleBackpackComponent& Backpack, FText& OutReason)
{
	if (bMutating || Placements.IsEmpty())
	{
		OutReason = INVTEXT("微缩区没有可撤回的房间卡，或正在处理房间卡。");
		return false;
	}
	TGuardValue<bool> Mutation(bMutating, true);
	const FCastleMiniPlacement& Last = Placements.Last();
	if (!Backpack.TryAddItemsAllowOverflow(MakeArrayView(&Last.Card, 1), OutReason)) { return false; }
	Occupied.Remove(Last.Cell);
	Placements.Pop(EAllowShrinking::No);
	return true;
}

bool FCastleMiniPlacementBoard::CanSettle(const int32 Copies, FText& OutReason) const
{
	if (Copies < 0 || static_cast<int64>(Placements.Num()) * Copies > MAX_int32)
	{
		OutReason = INVTEXT("轮盘返还张数超出有效整数范围。");
		return false;
	}
	OutReason = FText::GetEmpty();
	return true;
}

bool FCastleMiniPlacementBoard::TrySettle(UCastleBackpackComponent& Backpack, const int32 Copies, FText& OutReason)
{
	if (bMutating)
	{
		OutReason = INVTEXT("微缩区正在处理房间卡。");
		return false;
	}
	if (!CanSettle(Copies, OutReason)) { return false; }
	TGuardValue<bool> Mutation(bMutating, true);
	const int32 Total = Placements.Num() * Copies;
	if (Total > 0)
	{
		TArray<FCastleBuildingDescriptor> Awards;
		Awards.Reserve(Total);
		// 第一轮返还原样卡，其余各轮保持相同顺序加入临时栏队尾。
		for (int32 Copy = 0; Copy < Copies; ++Copy)
		{
			for (const FCastleMiniPlacement& Placement : Placements) { Awards.Add(Placement.Card); }
		}
		if (!Backpack.TryAddItemsAllowOverflow(Awards, OutReason)) { return false; }
	}
	Placements.Reset();
	Occupied.Reset();
	return true;
}
