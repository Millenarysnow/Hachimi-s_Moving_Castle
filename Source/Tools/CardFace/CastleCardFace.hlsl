// 卡面以归一化 UV 构图。层的位移在卡窗内计算，边框、裁切轮廓与可读文字不随内容平移。
struct FCardShapes
{
    // 在等比坐标中绘制圆角牌面，保证横边和竖边的圆角半径一致。
    float RoundedRect(float2 p, float2 extent, float radius)
    {
        float2 q = abs(p) - extent + radius;
        return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
    }

    // 云和山脊都来自可分层的纹样；柔边只属于各自的景物，不会模糊建筑或 UI。
    float Ridge(float x)
    {
        return sin(x * 9.0) * 0.040 + sin(x * 17.0 + 1.4) * 0.025 + cos(x * 4.5) * 0.035;
    }

    // 固定的几何花纹。箔面变化只调它的光色和强度，绝不移动花纹本身。
    float Engraving(float2 p)
    {
        float2 cell = abs(frac(float2(p.x + p.y, p.x - p.y) * 29.0) - 0.5);
        return 1.0 - smoothstep(0.025, 0.095, min(cell.x, cell.y));
    }
};
FCardShapes Shape;
float2 uv = UV;
float2 tilt = CardState.xy;
float foil = CardState.z;
float2 p = (uv - 0.5) * float2(1.0, 226.0 / 162.0);
float edge = Shape.RoundedRect(p, float2(0.489, 0.682), 0.058);
float px = max(fwidth(edge), 0.001);
float opacity = 1.0 - smoothstep(-px * 0.6, px * 0.6, edge);
float2 cardUV = float2(uv.x, uv.y + 0.060 * (uv.x - 0.5));

// 玩法色是可见面积最大的外层。保留浅轮廓、暗槽及阶梯状亮边，让普通卡也有材质质感。
float2 lacquerUV = float2(uv.x - 0.30, uv.y - 0.10);
float lacquer = 0.66 + 0.46 * saturate(1.0 - dot(lacquerUV, lacquerUV));
float micrograin = frac(sin(dot(floor(uv * float2(210.0, 287.0)), float2(12.9898, 78.233))) * 43758.5453);
float3 color = lerp(float3(0.014, 0.019, 0.033), CardTint.rgb * lacquer, 0.84);
color *= 0.96 + micrograin * 0.08;
float outerBevel = 1.0 - smoothstep(0.002, 0.006, abs(edge + 0.013));
float innerRule = 1.0 - smoothstep(0.001, 0.004, abs(edge + 0.026));
color = lerp(color, float3(0.74, 0.85, 0.91), outerBevel * 0.68);
color = lerp(color, color * 0.43, innerRule * 0.83);

// 倾斜的堡垒窗沿固定在牌上。视差只改下面各层的取样坐标，因此不会从窗口边缘露底。
float2 windowP = (cardUV - float2(0.5, 0.451)) / float2(0.418, 0.268);
float windowEdge = Shape.RoundedRect(windowP, float2(1.0, 1.0), 0.52);
float windowAA = max(fwidth(windowEdge), 0.007);
float window = 1.0 - smoothstep(-windowAA * 0.5, windowAA, windowEdge);
float rim = (1.0 - smoothstep(0.015, 0.052, abs(windowEdge - 0.010))) * 0.82;
color = lerp(color, lerp(CardTint.rgb * 0.45, float3(0.65, 0.77, 0.82), 0.56), rim);

// 远处薄云、远山是第一张插画；云海与暖色地平线是第二张，移动幅度略大。
float2 far = uv + tilt * float2(-0.017, -0.013);
float2 clouds = uv + tilt * float2(-0.038, -0.025);
float skyMix = smoothstep(0.19, 0.69, far.y);
float halo = exp(-4.8 * length((far - float2(0.63, 0.46)) * float2(0.9, 1.35)));
float3 sky = lerp(float3(0.025, 0.097, 0.212), float3(0.27, 0.56, 0.72), skyMix);
sky += halo * float3(0.31, 0.24, 0.15);
float mountain = smoothstep(0.490, 0.514, far.y + Shape.Ridge(far.x));
float mountainNear = smoothstep(0.538, 0.560, far.y + Shape.Ridge(far.x * 1.3 + 0.38));
sky = lerp(sky, float3(0.21, 0.35, 0.46), mountain * 0.54);
sky = lerp(sky, float3(0.31, 0.50, 0.63), mountainNear * 0.63);
float cloudBend = Shape.Ridge(clouds.x * 0.75 + 0.21);
float cloud = smoothstep(0.52, 0.64, clouds.y + cloudBend) * (1.0 - smoothstep(0.67, 0.77, clouds.y));
float sun = 1.0 - smoothstep(0.048, 0.053, length((far - float2(0.70, 0.318)) * float2(1.0, 1.4)));
sky = lerp(sky, float3(0.94, 0.82, 0.61), sun * 0.94);
sky = lerp(sky, float3(0.57, 0.74, 0.79), cloud * 0.70);

// 只在捕获完成后采纳建筑颜色。捕获的 A 为反向覆盖率，RGB 已预乘；先还原颜色做非线性显示转换，再乘覆盖率。
// 采样坐标不加 tilt：真实视角变化由镜头完成，卡窗倾斜也不会剪切建筑透视。
float2 portraitUV = (uv - float2(0.5, 0.454)) / float2(0.89, 0.554) + 0.5;
float4 shot = Texture2DSample(Portrait, PortraitSampler, saturate(portraitUV));
float coverage = saturate(1.0 - shot.a) * CardState.w;
float3 lit = max(shot.rgb, 0.0) / max(1.0 - shot.a, 0.0001);
// 单独处理捕获颜色，UI 的玩法色与文字不会被高光压缩。保持暗边与窗洞，而不是按亮度抠图。
lit = lit * 1.38 / (1.0 + lit * 0.62);
float3 stage = lerp(sky, saturate(lit), coverage);

// 贴近窗下沿的雾比云海移得更多，并能跨过建筑底边；对建筑大面积挡脸的雾不是景深。
float2 near = uv + tilt * float2(-0.069, -0.047);
float fogLine = near.y + Shape.Ridge(near.x * 1.53 + 1.63) * 0.55;
float fog = smoothstep(0.608, 0.708, fogLine) * (0.40 + 0.24 * cos(near.x * 8.9));
stage = lerp(stage, float3(0.60, 0.77, 0.82), fog);
stage *= 1.0 - 0.14 * pow(saturate(length(windowP) * 0.73), 2.0);
color = lerp(color, stage, window);

// 底部铭牌和窗外的刻纹都是牌身的一部分。排除对角数字与名称区，箔光不会洗掉玩法信息。
float plaque = smoothstep(0.746, 0.759, cardUV.y) * (1.0 - smoothstep(0.954, 0.969, cardUV.y));
color = lerp(color, lerp(CardTint.rgb * 0.34, float3(0.010, 0.019, 0.034), 0.55), plaque * 0.86);
float stripe = 1.0 - smoothstep(0.002, 0.007, abs(cardUV.y - 0.752));
color = lerp(color, float3(0.51, 0.70, 0.76), stripe * 0.46);
float pattern = Shape.Engraving(p);
// 箔带只覆盖外圈和窗沿；上、下两处信息区仍由漆面底色提供对比。
float wideRail = (1.0 - smoothstep(0.004, 0.013, abs(edge + 0.016))) * 0.62;
float windowMetal = (1.0 - smoothstep(0.026, 0.085, abs(windowEdge - 0.034))) * 0.86;
float foilMask = max(wideRail, windowMetal) * (1.0 - window);
float shoulder = smoothstep(0.13, 0.19, uv.y) * (1.0 - smoothstep(0.70, 0.75, uv.y));
float rail = smoothstep(0.34, 0.46, abs(uv.x - 0.5));
foilMask = max(foilMask, shoulder * rail * pattern * 0.71 * (1.0 - window));
float bandCoord = uv.x + uv.y * 0.58 - (0.66 + tilt.x * 0.50 - tilt.y * 0.26);
float band = exp(-abs(bandCoord) * 18.0);
float glint = exp(-abs(bandCoord) * 58.0);
float3 iridescent = 0.54 + 0.42 * cos((bandCoord * 2.3 + tilt.x * 0.19 + tilt.y * 0.20 + float3(0.03, 0.35, 0.64)) * 6.283185);
float3 foilColor = lerp(lerp(float3(0.91, 0.58, 0.24), iridescent, 0.71), float3(1.0, 0.98, 0.83), 0.46 * glint);
color = lerp(color, foilColor, saturate(foilMask * foil * (0.29 + 0.95 * band + 0.18 * glint)));
return float4(saturate(color), opacity);
