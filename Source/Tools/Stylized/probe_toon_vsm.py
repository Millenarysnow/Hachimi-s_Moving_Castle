"""在独立的 DX12 SM6 离屏编辑器验证 Toon 投影，图像写入 Saved/Stylized。"""

from pathlib import Path
import statistics
import sys

import unreal

# UE 的命令行执行器不会自动把脚本目录放入当前进程的模块搜索路径。
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from create_toon_assets import ASSETS, PROFILES, ROOT, TOOLS, build_shared_assets, compile_material
from create_toon_assets import connect_color_input, connect_front, create, function_call, load, require


OUTPUT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / "Stylized"
PROBE_ROOT = f"{ROOT}/Validation"


def spawn(kind, location, pitch=0.0, yaw=0.0):
    """探针只在当前未保存的空关卡中生成 Actor，俯仰和偏航分别写入。"""
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    rotation = unreal.Rotator()
    rotation.set_editor_property("pitch", pitch)
    rotation.set_editor_property("yaw", yaw)
    return require(actors.spawn_actor_from_class(kind, unreal.Vector(*location), rotation),
                   f"无法摆放探针 Actor {kind.__name__}")


def box(name, location, scale, mesh, material):
    """用同一网格和材质生成接收面、投影件，排除两者的材质差异。"""
    actor = spawn(unreal.StaticMeshActor, location)
    actor.set_actor_label(name)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    component = actor.get_editor_property("static_mesh_component")
    require(component.set_static_mesh(mesh), f"{name} 无法使用网格 {mesh.get_path_name()}")
    component.set_material(0, material)
    return component


def make_probe_material():
    """读取一份 V4 的正式线性固有色，建立临时表面材质并执行 SM6 编译。"""
    path = f"{PROBE_ROOT}/M_StylizedProbe"
    require(not ASSETS.does_asset_exist(path), f"验证要求使用新包，{path} 已存在")
    source = load("/Game/MovingCastle/Model/Buildings/HachimisV4/Materials/M_BeamOak", unreal.Material)
    base, emissive = require(TOOLS.read_v4_colors(source), f"{source.get_path_name()} 的色值不可读")

    functions = build_shared_assets(PROBE_ROOT)
    material = create(path, unreal.Material, unreal.MaterialFactoryNew)
    call = function_call(material, functions["Surface"])
    connect_color_input(material, call, "BaseColor", "BaseColor", base)
    connect_color_input(material, call, "Emissive", "Emissive", emissive)
    connect_front(material, call)
    compile_material(material)
    unreal.log(f"CASTLE_TOON_COMPILED material={material.get_path_name()} base={base}")
    return material


def main():
    """仅切换遮挡物投影，比较同机位、同一接收面像素的线性颜色。"""
    system = unreal.SystemLibrary
    # 只允许专门启动的编辑器实例运行。脚本会创建空关卡，且不会保存现有地图。
    command_line = system.get_command_line().lower()
    require("-executepythonscript=" in command_line and "-renderoffscreen" in command_line
            and "-d3d12" in command_line and "-sm6" in command_line,
            "需要以 -ExecutePythonScript、-RenderOffscreen、-d3d12、-sm6 启动独立编辑器")
    for name, expected in (("r.Substrate", 1), ("r.Substrate.ProjectGBufferFormat", 0),
                           ("r.Shadow.Virtual.Enable", 1)):
        value = system.get_console_variable_int_value(name)
        require(value == expected, f"{name}={value}，验证要求 {expected}")

    material = make_probe_material()
    world = require(unreal.EditorLoadingAndSavingUtils.new_blank_map(False), "无法建立 Toon 验证关卡")
    cube = load("/Engine/BasicShapes/Cube", unreal.StaticMesh)
    box("Toon Receiver", (0, 0, -5), (15, 15, 0.1), cube, material)
    occluder = box("Toon Occluder", (0, 0, 155), (2, 2, 3), cube, material)

    # 使用一盏定向光隔离直接光的贡献，避免环境补光掩盖 VSM 是否进入色阶的问题。
    sun = spawn(unreal.DirectionalLight, (0, 0, 600), pitch=-50)
    light = sun.get_editor_property("light_component")
    light.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.set_editor_property("intensity", 3.0)
    light.set_editor_property("light_source_angle", 0.1)
    light.set_editor_property("contact_shadow_length", 0.0)
    light.set_editor_property("cast_shadows", True)
    light.set_editor_property("cast_raytraced_shadow", unreal.CastRayTracedShadow.DISABLED)

    camera = spawn(unreal.SceneCapture2D, (0, -140, 1400), pitch=-84.2894, yaw=90.0)
    capture = camera.get_editor_property("capture_component2d")
    capture.set_editor_property("capture_every_frame", False)
    capture.set_editor_property("capture_on_movement", False)
    capture.set_editor_property("always_persist_rendering_state", True)
    capture.set_editor_property("fov_angle", 39.0)
    rendering = unreal.RenderingLibrary
    size = 160
    target = require(rendering.create_render_target2d(world, size, size,
                     unreal.TextureRenderTargetFormat.RTF_RGBA16F), "无法创建浮点渲染目标")
    capture.set_editor_property("texture_target", target)
    OUTPUT.mkdir(parents=True, exist_ok=True)

    def render(name, source):
        """先执行场景捕获，再读回原始像素，避免输出曝光或色调曲线影响比值。"""
        capture.set_editor_property("capture_source", source)
        capture.capture_scene()
        rendering.export_render_target(world, target, str(OUTPUT), f"{name}.hdr")
        pixels = rendering.read_render_target_raw_pixel_area(world, target, 0, 0, size, size, False)
        require(len(pixels) == size ** 2, f"{name} 没有读到完整的浮点图像")
        return pixels

    # 首先用几何面法线排除黑帧。这个检查不替代正式建筑卡牌子系统的验收。
    normal = render("probe-normal", unreal.SceneCaptureSource.SCS_NORMAL)
    top_faces = sum(pixel.b > 0.9 and abs(pixel.r) < 0.1 and abs(pixel.g) < 0.1 for pixel in normal)
    unreal.log(f"CASTLE_TOON_NORMAL top_faces={top_faces}")
    require(top_faces > 500, "探针未捕获到几何体的 +Z 世界法线")

    scene_color = unreal.SceneCaptureSource.SCS_SCENE_COLOR_HDR
    with_shadow = render("probe-shadow", scene_color)
    occluder.set_cast_shadow(False)
    without_shadow = render("probe-lit", scene_color)
    # 仅选取正面接收面且投影使其明显变暗的像素，排除遮挡件的侧壁和浮点除零。
    ratios = []
    for n, dark, lit in zip(normal, with_shadow, without_shadow):
        if n.b > 0.9 and min(lit.r, lit.g, lit.b) > 0.003 and dark.r < lit.r * 0.8:
            ratios.append((dark.r / lit.r, dark.g / lit.g, dark.b / lit.b))
    require(len(ratios) >= 100, f"未观察到足量 VSM 色阶样本，只有 {len(ratios)} 个")
    measured = tuple(statistics.median(channel) for channel in zip(*ratios))
    expected = PROFILES["Surface"][0]
    unreal.log(f"CASTLE_TOON_VSM samples={len(ratios)} ratios={measured} expected={expected}")
    require(all(abs(actual - target) < 0.10 for actual, target in zip(measured, expected)),
            f"VSM 暗部与 Toon Profile 色阶不符，实测 {measured}，预期 {expected}")
    unreal.log("CASTLE_TOON_VSM_PASS")


if __name__ == "__main__":
    main()
