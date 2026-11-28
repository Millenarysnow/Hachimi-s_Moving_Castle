#pragma once

#include "CoreMinimal.h"

/**
 * 城堡项目共享的渲染标记约定。
 *
 * Stencil 使用位掩码而非互斥枚举，使多个渲染功能可以在同一个 CustomDepth
 * 像素上同时保存各自的标记。玩法代码只能读取或修改自己声明的位。
 */
namespace CastleRenderingConstants
{
	/** 塔侧数字等信息面的风格描边排除位，悬停高亮只写自己的位。 */
	inline constexpr int32 StylizedNoOutlineStencilBit = 1 << 0;

	/** 世界几何和不受光信息面共同保留 Color 参数，供现有动态材质着色。 */
	inline constexpr TCHAR ToonBasicColorMaterialPath[] =
		TEXT("/Game/MovingCastle/Rendering/Materials/M_Toon_BasicColor.M_Toon_BasicColor");
	inline constexpr TCHAR InfoUnlitColorMaterialPath[] =
		TEXT("/Game/MovingCastle/Rendering/Materials/M_Info_UnlitColor.M_Info_UnlitColor");

	/** 悬停白色外轮廓独占的 Stencil 位掩码。 */
	inline constexpr int32 HoverOutlineStencilBit = 1 << 1;

	/** 离线工具生成的悬停白边后处理材质完整对象路径。 */
	inline constexpr TCHAR HoverOutlineMaterialPath[] =
		TEXT("/Game/MovingCastle/Rendering/PostProcess/M_PP_HoverOutline.M_PP_HoverOutline");

	static_assert((StylizedNoOutlineStencilBit & HoverOutlineStencilBit) == 0,
		"风格化排除位与悬停白边位必须互不重叠。");
	static_assert((HoverOutlineStencilBit & (HoverOutlineStencilBit - 1)) == 0,
		"悬停白边必须使用单一 Stencil 位。");
}
