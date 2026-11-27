// 描边只替换选中对象的外侧像素，未命中的场景颜色由后处理输入原样保留。
struct FCastleHoverOutline
{
	// CustomStencil 纹理以 8 位无符号值归一化存储，先还原整数再按 bit 掩码判断。
	float IsHoverStencil(float4 StencilSample, float StencilBit)
	{
		int StencilValue = (int)round(saturate(StencilSample.r) * 255.0f);
		int BitMask = (int)round(StencilBit);
		return (StencilValue & BitMask) != 0;
	}

	// 仅把确实可见的悬停目标样本贡献给轮廓，遮挡深度以厘米容差裁剪。
	float IsVisibleHoverTarget(float2 UV, float OutlinePixelSceneDepth,
		float StencilBit, float DepthToleranceCm)
	{
		float4 NeighborStencil = SceneTextureLookup(UV, PPI_CustomStencil, false);
		if (!IsHoverStencil(NeighborStencil, StencilBit))
		{
			return 0.0f;
		}

		float NeighborCustomDepth = SceneTextureLookup(UV, PPI_CustomDepth, false).r;
		float NeighborSceneDepth = SceneTextureLookup(UV, PPI_SceneDepth, false).r;
		if (NeighborCustomDepth > NeighborSceneDepth + DepthToleranceCm)
		{
			return 0.0f;
		}

		// 外侧像素的场景表面若更靠前，白边被该前景像素遮掉。
		return NeighborCustomDepth <= OutlinePixelSceneDepth + DepthToleranceCm ? 1.0f : 0.0f;
	}
};

FCastleHoverOutline HoverOutline;
float SceneDepthCm = SceneDepth.r;
float CustomDepthCm = CustomDepth.r;
bool bCenterIsSelected = HoverOutline.IsHoverStencil(CustomStencil, HoverStencilBit) > 0.5f;
bool bCenterIsVisible = CustomDepthCm <= SceneDepthCm + DepthOcclusionToleranceCm;

// 选中物体本体维持原色，后处理只在其外侧邻接像素写入白色。
if (bCenterIsSelected && bCenterIsVisible)
{
	return SceneColor;
}

// Before DOF 在 TSR 前以渲染分辨率运行。由当前视图高度缩放像素线宽，
// 后续 TSR 会对颜色和深度边缘共同完成时间抗锯齿与放大。
float ViewWidthPx = max(View.ViewSizeAndInvSize.x, 1.0f);
float ViewHeightPx = max(View.ViewSizeAndInvSize.y, 1.0f);
float ThicknessPx = max(1.0f, OutlineThicknessPx * ViewHeightPx / 1080.0f);
float2 PixelOffset = float2(ThicknessPx / ViewWidthPx, ThicknessPx / ViewHeightPx);
float OutlineMask = 0.0f;

OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2(-PixelOffset.x, 0.0f), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2( PixelOffset.x, 0.0f), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2(0.0f, -PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2(0.0f,  PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2(-PixelOffset.x, -PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2( PixelOffset.x, -PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2(-PixelOffset.x,  PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2( PixelOffset.x,  PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));

return lerp(SceneColor, OutlineColor, saturate(OutlineMask));
