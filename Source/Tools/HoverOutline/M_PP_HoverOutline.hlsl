// 描边只替换选中对象的外侧像素，未命中的场景颜色由后处理输入原样保留。
struct FCastleHoverOutline
{
	// UE5.8 的 SceneTextureLookup 直接返回 0～255 的 Stencil 整数值，保留其他位独立含义。
	float IsHoverStencil(float4 StencilSample, float StencilBit)
	{
		int StencilValue = (int)StencilSample.r;
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
float ViewHeightPx = max(View.ViewSizeAndInvSize.y, 1.0f);
float ThicknessPx = max(1.0f, OutlineThicknessPx * ViewHeightPx / 1080.0f);
// 视图可只占场景缓冲区的一部分。以引擎转换后的中心 UV 和纹理像素尺寸采样，避免缩放或视口偏移造成重影。
float2 UV = GetDefaultSceneTextureUV(Parameters, PPI_CustomStencil);
float2 PixelOffset = ThicknessPx * GetSceneTextureBufferSize(PPI_CustomStencil).zw;
float OutlineMask = 0.0f;

OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2(-PixelOffset.x, 0.0f), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2( PixelOffset.x, 0.0f), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2(0.0f, -PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2(0.0f,  PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2(-PixelOffset.x, -PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2( PixelOffset.x, -PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2(-PixelOffset.x,  PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));
OutlineMask = max(OutlineMask, HoverOutline.IsVisibleHoverTarget(UV + float2( PixelOffset.x,  PixelOffset.y), SceneDepthCm, HoverStencilBit, DepthOcclusionToleranceCm));

// UE 5.8 将场景颜色作为 float4、向量参数的 RGB 作为 float3 传给 Custom 节点。
// 保留原场景 alpha，并为描边 RGB 明确补充不透明 alpha，使 SM6 的 Lerp 两侧同为 float4。
return lerp(SceneColor, float4(OutlineColor, 1.0f), saturate(OutlineMask));
