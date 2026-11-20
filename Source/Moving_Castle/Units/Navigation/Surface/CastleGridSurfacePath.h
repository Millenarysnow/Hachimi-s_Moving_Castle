#pragma once

#include "CoreMinimal.h"

class ARecastNavMesh;
class UWorld;

namespace CastleGridSurfacePath
{
	/** 只在游戏线程构建共享路线时使用；棋盘提供坐标，不提供通行性。 */
	struct FGrid
	{
		FString BoardName;
		FTransform Transform;
		FIntVector Origin;
		FIntPoint Min;
		FIntPoint Max;
		int32 Layers;
		double CellSize;
		double LayerHeight;
		double FirstFloor;
	};

	/** 没有棋盘的关卡没有格心语义；存在棋盘但无法建立唯一有效坐标系时拒绝求解。 */
	bool ReadGrid(UWorld& World, TOptional<FGrid>& OutGrid, FText& OutFailure);

	/** 棋盘上的硬路径点可以贴楼面高度，但不能挪到障碍侧边或相邻楼层。 */
	bool IsExactAnchor(const FGrid& Grid, FVector Requested, FVector Projected, double MaxHeightError);

	/** 保持原生格子访问顺序及首尾锚点；只输出已验证的实际行走折线，不保存导航引用。 */
	bool Build(const FGrid& Grid, const ARecastNavMesh& Mesh, const UObject& Owner,
		TConstArrayView<FVector> Surface, double Spacing, TArray<FVector>& OutPoints, FText& OutFailure);
}
