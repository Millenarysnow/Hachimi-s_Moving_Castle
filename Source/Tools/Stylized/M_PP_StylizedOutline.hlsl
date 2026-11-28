// 着色前的无界体积使用厘米深度，背景在 400 m 完全退出描边。
const float Center = SceneDepth.r;
const float Fade = 1.0f - smoothstep(FadeStartMeters, FadeEndMeters, Center * 0.01f);
const bool InfoSurface = (((uint)CustomStencil.r) & ((uint)NoOutlineBit)) != 0u
    && CustomDepth.r <= Center + 2.0f;
if (Fade <= 0.0f || InfoSurface || (OutlineStrength <= 0.0f && NormalEdgeStrength <= 0.0f))
{
    return SceneColor.rgb;
}

// 视口可以只占渲染缓冲的一部分，以引擎转换后的 UV 及渲染分辨率定位邻居。
const float2 UV = GetDefaultSceneTextureUV(Parameters, PPI_SceneDepth);
const float2 Step = OutlineThicknessPx * (View.ViewSizeAndInvSize.y / 1080.0f)
    * GetSceneTextureBufferSize(PPI_SceneDepth).zw;
const float2 Neighbors[4] = {float2(Step.x, 0), float2(-Step.x, 0), float2(0, Step.y), float2(0, -Step.y)};
const float3 N = WorldNormal.xyz;
float Outer = 0.0f;
float Inner = 0.0f;
for (int Index = 0; Index < 4; ++Index)
{
    const float2 SampleUV = UV + Neighbors[Index];
    const float Far = SceneTextureLookup(SampleUV, PPI_SceneDepth, false).r;
    const float Opposite = SceneTextureLookup(UV - Neighbors[Index], PPI_SceneDepth, false).r;
    const float Difference = Far - Center;
    const float Threshold = max(2.5f, Center * 0.005f);
    // 两侧深度连续倾斜时用反向斜率补偿，避免掠射的同一面被整片涂黑。
    const float Slope = min(max(Center - Opposite, 0.0f), 0.8f * Threshold);
    const float Silhouette = smoothstep(Threshold, Threshold * 2.1f, Difference - Slope);
    Outer = max(Outer, Silhouette);
    const float3 NextNormal = SceneTextureLookup(SampleUV, PPI_WorldNormal, false).xyz;
    const float SameSurface = 1.0f - smoothstep(Threshold, Threshold * 2.0f, abs(Difference));
    const float Fold = smoothstep(0.27f, 0.66f, 1.0f - dot(N, NextNormal));
    Inner = max(Inner, Fold * SameSurface);
}

// 暖棕色乘数保留原物体的色相，不用固定黑线盖住低模表面的固有色。
const float Ink = Fade * saturate(max(Outer * OutlineStrength, Inner * NormalEdgeStrength));
return lerp(SceneColor.rgb, SceneColor.rgb * InkTint, Ink);
