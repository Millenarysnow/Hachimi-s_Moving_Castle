#pragma once

#include "CastleGameplayTypes.h"
#include "CastleMiniPlacementBoard.generated.h"

class UCastleBackpackComponent;

/** 一格始终保存原始牌面，微缩比例和相邻匹配都不会改变实际卡牌。 */
USTRUCT()
struct FCastleMiniPlacement
{
	GENERATED_BODY()

	UPROPERTY() FIntVector Cell = FIntVector::ZeroValue;
	UPROPERTY() FCastleBuildingDescriptor Card;
};

/** 固定地面格和可向外拼接的三维放置区。顺序数组用于撤回和发奖。 */
USTRUCT()
struct FCastleMiniPlacementBoard
{
	GENERATED_BODY()

	TConstArrayView<FCastleMiniPlacement> GetPlacements() const { return Placements; }
	const TMap<FIntVector, FCastleGroundCell>& GetGroundCells() const { return GroundCells; }
	bool InitializeGround(TConstArrayView<FCastleGroundCell> Cells, FIntPoint GridSize, FText& OutReason);
	bool CanPlace(const FCastleBuildingDescriptor& Card, const FIntVector& Cell, FText& OutReason) const;

	/** 检查格位后消费实际背包卡，消费提交会触发现有的临时栏补位。 */
	bool TryPlace(UCastleBackpackComponent& Backpack, int32 BackpackIndex, const FIntVector& Cell, FText& OutReason);
	bool TryReturnLast(UCastleBackpackComponent& Backpack, FText& OutReason);
	/** 返还或派奖均批量入库，只有成功后才清空暂存；0 份表示押错销毁。 */
	bool TrySettle(UCastleBackpackComponent& Backpack, int32 Copies, FText& OutReason);
	bool CanSettle(int32 Copies, FText& OutReason) const;

private:
	// 由岛用 UPROPERTY 持有，保证卡在局部暂存期间建筑定义仍被 GC 跟踪。
	UPROPERTY() TArray<FCastleMiniPlacement> Placements;
	UPROPERTY() TMap<FIntVector, FCastleGroundCell> GroundCells;
	TMap<FIntVector, int32> Occupied;
	bool bMutating = false;
};
