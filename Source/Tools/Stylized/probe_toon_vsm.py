"""在独立的 DX12 SM6 离屏编辑器验证 Toon 投影，图像写入 Saved/Stylized。"""

import math
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
EXPECTED_SHADOW = PROFILES["Surface"][0]
# 距离沿光线从接收面量起。远处使用较宽的遮挡件，让取样区远离随机软阴影的边缘。
CASTERS = ((0.5, 0.6), (3.0, 1.5), (45.0, 5.0))  # 距离与沿世界 X 的宽度，单位 m。
EXTINCTIONS = (10.0, 1.0, 0.5, 0.25, 0.10, 0.05, 0.02, 0.0)  # 单位 m⁻¹。


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


def set_extinction(profile, coefficient):
    """用编辑器属性写回整份设置，触发 Toon Profile 图集刷新并保留原有色阶。"""
    settings = profile.get_editor_property("settings")
    settings.set_editor_property("shadow_extinction_coefficient", coefficient)
    profile.set_editor_property("settings", settings)
    observed = profile.get_editor_property("settings").get_editor_property("shadow_extinction_coefficient")
    require(math.isclose(observed, coefficient, abs_tol=1e-6),
            f"{profile.get_path_name()} 消光系数为 {observed}，要求 {coefficient}")


def measure_ratios(shadowed, unshadowed, samples):
    """对预先固定的接收面像素逐通道求比值，把完全黑和完全亮都纳入统计。"""
    ratios = []
    for pixel in samples:
        dark, lit = shadowed[pixel], unshadowed[pixel]
        ratios.append((dark.r / lit.r, dark.g / lit.g, dark.b / lit.b))
    return ratios


def main():
    """扫描独立的灯光透射开关和材质消光系数，检验各尺度是否共用一组参数。"""
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
    profile = load(f"{PROBE_ROOT}/Profiles/TP_Surface", unreal.ToonProfile)
    world = require(unreal.EditorLoadingAndSavingUtils.new_blank_map(False), "无法建立 Toon 验证关卡")
    cube = load("/Engine/BasicShapes/Cube", unreal.StaticMesh)
    box("Toon Receiver", (0, 0, -5), (15, 15, 0.1), cube, material)
    occluder = box("Toon Occluder", (0, 0, 155), (0.6, 2.2, 0.1), cube, material)
    occluder.set_mobility(unreal.ComponentMobility.MOVABLE)
    caster = occluder.get_owner()

    # 使用一盏定向光隔离直接光的贡献，避免环境补光掩盖 VSM 是否进入色阶的问题。
    sun = spawn(unreal.DirectionalLight, (0, 0, 600), pitch=-50)
    light = sun.get_editor_property("light_component")
    light.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.set_editor_property("intensity", 3.0)
    light.set_editor_property("light_source_angle", 0.1)
    light.set_editor_property("contact_shadow_length", 0.0)
    light.set_editor_property("cast_shadows", True)
    light.set_editor_property("cast_raytraced_shadow", unreal.CastRayTracedShadow.DISABLED)

    # 从太阳的对侧俯视原点。遮挡件沿光线移动时，投影中心固定在原点，遮挡件本身离开中央取样区。
    camera = spawn(unreal.SceneCapture2D, (700, 0, 700 * math.sqrt(3)), pitch=-60, yaw=180.0)
    capture = camera.get_editor_property("capture_component2d")
    capture.set_editor_property("capture_every_frame", False)
    capture.set_editor_property("capture_on_movement", False)
    capture.set_editor_property("always_persist_rendering_state", True)
    capture.set_editor_property("fov_angle", 39.0)
    rendering = unreal.RenderingLibrary
    size = 256
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

    # 屏幕纵轴对应世界 X 的变化，横轴对应 Y。中心区位于投影内部，旁边的一列位于投影外。
    center = size // 2
    shadow_samples = [y * size + x for y in range(center - 2, center + 2)
                      for x in range(center - 10, center + 10)]
    lit_samples = [sample + 90 for sample in shadow_samples]
    scene_color = unreal.SceneCaptureSource.SCS_SCENE_COLOR_HDR
    toward_sun = -sun.get_actor_forward_vector()
    results = {(transmission, extinction): [] for transmission in (False, True) for extinction in EXTINCTIONS}
    for distance, width in CASTERS:
        expected_location = toward_sun * (distance * 100.0)
        # 基础立方体的边长为 1 m，缩放数值即为遮挡件的实测尺寸。
        caster.set_actor_scale3d(unreal.Vector(width, 2.2, 0.1))
        caster.set_actor_location(expected_location, False, False)
        require((caster.get_actor_location() - expected_location).length() < 0.1,
                f"{caster.get_path_name()} 无法移动到 {distance:g} m 的试验位置")
        distance_label = f"vsm-{distance:g}m"
        normal = render(f"{distance_label}-normal", unreal.SceneCaptureSource.SCS_NORMAL)
        for pixel in shadow_samples + lit_samples:
            n = normal[pixel]
            require(n.b > 0.9 and abs(n.r) < 0.1 and abs(n.g) < 0.1,
                    f"{distance_label} 的固定样本 {pixel} 不是接收面的 +Z 世界法线")

        for transmission in (False, True):
            light.set_transmission(transmission)
            require(light.get_editor_property("transmission") == transmission,
                    f"{sun.get_path_name()} 的灯光透射未设为 {transmission}")
            label = f"{distance_label}-transmission-{int(transmission)}"
            occluder.set_cast_shadow(False)
            without_shadow = render(f"{label}-lit", scene_color)
            require(all(min(without_shadow[i].r, without_shadow[i].g, without_shadow[i].b) > 0.003
                        for i in shadow_samples + lit_samples), f"{label} 的无投影样本没有收到太阳直射光")
            occluder.set_cast_shadow(True)

            for extinction in EXTINCTIONS:
                set_extinction(profile, extinction)
                label_with_extinction = f"{label}-extinction-{extinction:g}"
                with_shadow = render(label_with_extinction, scene_color)
                guard_ratios = measure_ratios(with_shadow, without_shadow, lit_samples)
                require(all(abs(channel - 1.0) < 0.02 for ratio in guard_ratios for channel in ratio),
                        f"{label_with_extinction} 的投影外样本发生亮度变化")

                ratios = measure_ratios(with_shadow, without_shadow, shadow_samples)
                median = tuple(round(statistics.median(channel), 4) for channel in zip(*ratios))
                matched = sum(all(abs(value - expected) < 0.04 for value, expected in zip(ratio, EXPECTED_SHADOW))
                              for ratio in ratios)
                black = sum(max(ratio) < 0.01 for ratio in ratios)
                lit = sum(all(abs(channel - 1.0) < 0.02 for channel in ratio) for ratio in ratios)
                results[transmission, extinction].append(matched / len(ratios) >= 0.95)
                unreal.log(f"CASTLE_TOON_VSM distance_m={distance:g} transmission={transmission} "
                           f"extinction={extinction:g} median={median} dark_band={matched}/{len(ratios)} "
                           f"black={black} lit={lit}")

    passed = [(transmission, extinction) for (transmission, extinction), distances in results.items()
              if all(distances)]
    distances = tuple(distance for distance, _ in CASTERS)
    require(passed, f"没有灯光透射和消光组合在 {distances} m 均达到暗阶 {EXPECTED_SHADOW}")
    unreal.log(f"CASTLE_TOON_VSM_SETTINGS transmission_extinction={passed}")
    unreal.log("CASTLE_TOON_VSM_PASS")


if __name__ == "__main__":
    main()
