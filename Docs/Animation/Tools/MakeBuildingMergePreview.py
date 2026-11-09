"""把引擎真实阶段截图整理为便于聊天预览的 GIF，不改变截图中的建筑或动画内容。"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


project = Path(__file__).resolve().parents[3]
folder = project / "Saved/Tests/BuildingMergeVisual"
paths = [folder / f"Frame_{index:02d}.png" for index in range(25)]
assert all(path.is_file() for path in paths), "需要先运行原生演示关卡的 MergeDemoCapture 验收。"
font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 21)
small_font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 15)
frames = []

for index, path in enumerate(paths):
    # 等比缩小到聊天可读尺寸，仅加说明字幕与播放进度，不修改原始渲染帧。
    frame = Image.open(path).convert("RGB").resize((960, 540), Image.Resampling.LANCZOS)
    draw = ImageDraw.Draw(frame)
    draw.rounded_rectangle((22, 20, 380, 91), radius=12, fill=(18, 24, 31))
    draw.text((38, 30), "建筑零件合并 · 引擎实拍", font=font, fill=(239, 242, 245))
    phase = "两座一级建筑" if index < 3 else "零件松开" if index < 8 else "旋流汇聚" if index < 14 else "展开与咬合" if index < 24 else "二级建筑归位"
    draw.text((39, 61), phase, font=small_font, fill=(181, 204, 220))
    draw.rectangle((0, 535, int(960 * index / 24), 539), fill=(217, 181, 111))
    frames.append(frame)

# 首尾停留用于辨认合并前后；中间帧对应引擎中均匀分布的 3.2 秒真实播放时刻。
durations = [800] + [130] * 23 + [1200]
output = folder / "BuildingMergePreview.gif"
frames[0].save(output, save_all=True, append_images=frames[1:], duration=durations,
               loop=0, optimize=True, disposal=2)
print(output)
