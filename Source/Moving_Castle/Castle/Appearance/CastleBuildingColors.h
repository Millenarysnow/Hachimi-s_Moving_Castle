#pragma once

#include "Castle/Data/CastleGameplayTypes.h"

class UStaticMeshComponent;

namespace CastleBuildingColors
{
	/** 两个标量分别控制屋面专用材质和同装饰共用原料的结构件。0 表示资产编辑器的作者原色。 */
	inline constexpr int32 SurfaceIndex = 0;
	inline constexpr int32 StructureIndex = 1;

	/** 仅在通过描述校验的建筑上调用。作者分组、构件和槽材质共同决定着色范围。 */
	void Apply(UStaticMeshComponent& Component, ECastleTileColor Color, ECastleBuildingType Type, FName Node, FName Group);
}
