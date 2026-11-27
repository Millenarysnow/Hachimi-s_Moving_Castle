// 放置盒使用六个独立 0-1 面 UV 生成线框、角标、填充与警示纹理。
// LocalNormal 和 SupportNormal 均处于立方体局部空间，后者指向来源贴面。

// UV 到各面边界的距离用于生成连续的四边框，也用于让内部透明填充避开高亮线条。
float2 EdgeDistance = min(UV, 1.0 - UV);
float EdgeDistanceToBorder = min(EdgeDistance.x, EdgeDistance.y);
float EdgeMask = 1.0 - smoothstep(EdgeWidth * 0.75, EdgeWidth * 1.25, EdgeDistanceToBorder);

// 每面四角各叠加两条短线，六个面合成为八个加粗的三维 L 形角标。
float NearVerticalEdge = 1.0 - smoothstep(EdgeWidth * 1.25, EdgeWidth * 1.85, EdgeDistance.x);
float NearHorizontalEdge = 1.0 - smoothstep(EdgeWidth * 1.25, EdgeWidth * 1.85, EdgeDistance.y);
float CornerAlongVertical = 1.0 - smoothstep(CornerLength, CornerLength + EdgeWidth, EdgeDistance.y);
float CornerAlongHorizontal = 1.0 - smoothstep(CornerLength, CornerLength + EdgeWidth, EdgeDistance.x);
float CornerMask = max(NearVerticalEdge * CornerAlongVertical, NearHorizontalEdge * CornerAlongHorizontal);

// 来源法线先转入指示盒局部坐标，再与每个面的局部法线比较。
float3 FaceNormal = normalize(LocalNormal);
float3 TowardSupport = normalize(SupportNormal.xyz);
float SupportFaceMask = pow(saturate(dot(FaceNormal, TowardSupport)), 12.0);

// 扫描亮带沿两条 UV 轴缓慢移动，避免特定朝向的盒面没有可见扫描。
float Pulse = 0.88 + 0.12 * sin(Time * 2.1);
float ScanPosition = frac(Time * 0.32);
float ScanMask = max(
    1.0 - smoothstep(0.012, 0.038, abs(UV.y - ScanPosition)),
    1.0 - smoothstep(0.012, 0.038, abs(UV.x - frac(ScanPosition + 0.37))));

// 斜纹仅由 InvalidMode 启用，不可放盒体仍保留同一占位尺寸和坐标。
float HatchPhase = frac((UV.x + UV.y) * 14.0 + Time * 0.08);
float InvalidHatchMask = step(HatchPhase, 0.12) * saturate(InvalidMode);

float3 Emissive = HoloColor.rgb * Pulse * (
    0.12
    + EdgeMask * 2.6
    + CornerMask * 2.4
    + SupportFaceMask * 0.45
    + ScanMask * 0.10
    + InvalidHatchMask * 0.55);
float Opacity = saturate(
    FaceFillOpacity * (1.0 - max(EdgeMask, CornerMask)) * (0.85 + 0.15 * Pulse)
    + EdgeMask * 0.78
    + CornerMask * 0.34
    + SupportFaceMask * 0.025
    + ScanMask * 0.018
    + InvalidHatchMask * 0.18);

return float4(Emissive, Opacity);
