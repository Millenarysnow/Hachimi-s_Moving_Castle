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

bool FCastleMiniPlacementBoard::CanPlace(const FCastleBuildingDescriptor& Card, const FIntVector& Cell, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (Cell.Z < 0 || Occupied.Contains(Cell))
	{
		OutReason = INVTEXT("该微缩格在底板下方或已被占用。");
		return false;
	}
	if (Placements.IsEmpty() && Cell == FIntVector::ZeroValue) { return true; }
	for (const FIntVector& Face : Faces)
	{
		FIntVector Neighbor;
		if (!TryAdjacentCell(Cell, Face, Neighbor)) { continue; }
		const int32* Index = Occupied.Find(Neighbor);
		if (Index && MatchesCard(Card, Placements[*Index].Card)) { return true; }
	}
	OutReason = Placements.IsEmpty() ? INVTEXT("第一张房间卡必须放在底板原点。") : INVTEXT("房间必须与一面相邻的房间同色或同数字。");
	return false;
}

TArray<FIntVector> FCastleMiniPlacementBoard::GetLegalCells(const FCastleBuildingDescriptor& Card) const
{
	if (Placements.IsEmpty()) { return {FIntVector::ZeroValue}; }
	TSet<FIntVector> Candidates;
	for (const FCastleMiniPlacement& Placement : Placements)
	{
		if (!MatchesCard(Card, Placement.Card)) { continue; }
		for (const FIntVector& Face : Faces)
		{
			FIntVector Cell;
			if (TryAdjacentCell(Placement.Cell, Face, Cell) && !Occupied.Contains(Cell)) { Candidates.Add(Cell); }
		}
	}
	TArray<FIntVector> Cells = Candidates.Array();
	Cells.Sort([](const FIntVector& A, const FIntVector& B)
	{
		if (A.Z != B.Z) { return A.Z < B.Z; }
		if (A.Y != B.Y) { return A.Y < B.Y; }
		return A.X < B.X;
	});
	return Cells;
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
