// 在 UI 像素处反投影：相机在原点，牌中心位于 (0, 0, Distance)，X/Y 轴是牌的本地单位向量。
// 使用牌宽为单位，画面中心为原点；纵向以同一比例还原，避免宽高不同让牌变形。
float2 cardSize = float2(1.0, CardAspect);
float3 ray = float3((UV - 0.5) * cardSize, ProjectionY.w);
float3 normal = cross(ProjectionX.xyz, ProjectionY.xyz);
float distance = ProjectionX.w;
float3 hit = ray * (normal.z * distance / dot(normal, ray)) - float3(0.0, 0.0, distance);
float2 cardUV = float2(dot(hit, ProjectionX.xyz), dot(hit, ProjectionY.xyz)) / cardSize + 0.5;

// 四角外没有牌。钳住纹理取样坐标仅用于隔离 GPU 地址模式；牌外同时输出透明黑。
// Retainer 纹理的色彩已经乘过 Alpha，不能再乘一次，否则字缘与圆角会出现黑边。
float2 inside = step(0.0, cardUV) * step(cardUV, 1.0);
// 售罄的淡化只在这个出口施加一次，同等衰减预乘的 RGB 与 Alpha。
return Texture2DSample(ComposedCard, ComposedCardSampler, saturate(cardUV)) * (inside.x * inside.y * CardOpacity);
