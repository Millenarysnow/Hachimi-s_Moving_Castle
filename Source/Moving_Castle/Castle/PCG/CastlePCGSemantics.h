#pragma once

#include "CoreMinimal.h"
#include "CastlePCGTypes.h"

class ACastleBoard;
class UCastleHachimisV4Layout;
struct FCastleV4Part;

/** 无状态的几何语义提取器。只读取原始美术，错误时不提交部分标注。 */
namespace CastlePCGSemantics
{
	MOVING_CASTLE_API bool Extract(ACastleBuildingBase* Building, const ACastleBoard* Board,
		TArray<FCastlePCGPartAnnotation>& OutParts, FString& OutError);
	/** 将格面转成六邻单位偏移，None 返回零向量。 */
	MOVING_CASTLE_API FIntVector FaceOffset(ECastlePCGFace Face);
	/** 坐标字典序用于确定性选择，不能以对象地址或 TMap 迭代顺序选择梯子。 */
	MOVING_CASTLE_API bool CellLess(const FIntVector& A, const FIntVector& B);
	/** 按作者样例坐标确定完整木构零件所属格，供家具净空与装配共同使用。 */
	FIntVector RoomFramePartOwner(const UCastleHachimisV4Layout& Layout, const FCastleV4Part& Part);
}
