"""在 UE 5.8 Python 环境创建本框架的示例配置和独立测试关卡。只保存列出的新资产。"""
import unreal

ROOT = "/Game/MovingCastle/Mass"
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary
skeletal = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)


def data_asset(name, asset_class):
    path = f"{ROOT}/{name}"
    asset = assets.load_asset(path) if assets.does_asset_exist(path) else None
    if asset:
        return asset
    if asset_class == unreal.AnimSequenceTransformProviderData:
        factory = unreal.TransformProviderDataFactory()
        factory.set_editor_property("provider_data_class", asset_class)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", asset_class)
    asset = tools.create_asset(name, ROOT, None, factory)
    if not asset:
        raise RuntimeError(f"无法创建 {path}")
    return asset


def trait(config, trait_class):
    for item in config.get_editor_property("config").get_editor_property("traits"):
        if isinstance(item, trait_class):
            return item
    raise RuntimeError(f"{config.get_path_name()} 缺少 {trait_class}")


# 专用副本只改变 Mass 使用的网格构建设置，不修改模板人物的原资产。
mesh_path = f"{ROOT}/SKM_MassHumanoid"
mesh = assets.load_asset(mesh_path) if assets.does_asset_exist(mesh_path) else None
if not mesh:
    mesh = assets.duplicate_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple", mesh_path)
if not mesh:
    raise RuntimeError("无法复制模板骨骼网格")
for lod in range(skeletal.get_lod_count(mesh)):
    settings = skeletal.get_lod_build_settings(mesh, lod)
    if not settings.get_editor_property("optimize_for_instancing"):
        settings.set_editor_property("optimize_for_instancing", True)
        skeletal.set_lod_build_settings(mesh, lod, settings)
assets.save_loaded_asset(mesh)

provider = data_asset("ATP_MassHumanoid", unreal.AnimSequenceTransformProviderData)
provider.set_editor_property("skinned_asset", mesh)
sequences = []
for path in ["/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle",
             "/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd"]:
    sequence = assets.load_asset(path)
    if not sequence:
        raise RuntimeError(f"动画不存在：{path}")
    entry = unreal.AnimSequenceTransformProviderSequence()
    entry.set_editor_property("sequence", sequence)
    sequences.append(entry)
provider.set_editor_property("sequences", sequences)
assets.save_loaded_asset(provider)

enemy = data_asset("DA_MassEnemyBase", unreal.CastleMassEnemyConfig)
npc = data_asset("DA_MassNPCBase", unreal.CastleMassNPCConfig)
for config, rate in [(enemy, 1.5), (npc, 1.0)]:
    visual = trait(config, unreal.CastleMassHumanoidVisualizationTrait)
    visual.set_editor_property("animation_provider", provider)
    visual.set_editor_property("move_play_rate", rate)
    # 示例持续显示远处动画，正式大量单位可改回 0 使用引擎屏幕尺寸阈值。
    visual.set_editor_property("animation_min_screen_size", -1.0)
    assets.save_loaded_asset(config)

projectile = data_asset("DA_MassProjectileBase", unreal.CastleMassProjectileConfig)
visual = trait(projectile, unreal.MassMovableVisualizationTrait)
description = unreal.StaticMeshInstanceVisualizationDesc()
mesh_description = unreal.MassStaticMeshInstanceVisualizationMeshDesc()
mesh_description.set_editor_property("mesh", assets.load_asset("/Engine/BasicShapes/Sphere"))
mesh_description.set_editor_property("local_transform", unreal.Transform(scale=[0.12, 0.12, 0.12]))
description.set_editor_property("meshes", [mesh_description])
visual.set_editor_property("static_mesh_instance_desc", description)
assets.save_loaded_asset(projectile)

# 已存在时保留用户对示例关卡的编辑，不重复创建场景。
map_path = f"{ROOT}/L_MassUnitsDemo"
if not assets.does_asset_exist(map_path):
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not level.new_level(map_path):
        raise RuntimeError("无法创建 Mass 示例关卡")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.GameModeBase)
    floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, [0, 0, -50])
    floor.static_mesh_component.set_static_mesh(assets.load_asset("/Engine/BasicShapes/Cube"))
    floor.set_actor_scale3d([180, 140, 1])
    floor.set_actor_label("MassDemo_Floor")
    actors.spawn_actor_from_class(unreal.DirectionalLight, [0, 0, 1000], unreal.Rotator(pitch=-50, yaw=-25, roll=0))
    actors.spawn_actor_from_class(unreal.SkyLight, [0, 0, 1000])
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, [0, 0, 0])
    actors.spawn_actor_from_class(unreal.PlayerStart, [-3000, -3400, 2600], unreal.Rotator(pitch=-25, yaw=40, roll=0))
    for label, config, location, moving in [
        ("Enemies_256", enemy, [-1500, -1800, 0], True),
        ("NPCs_256", npc, [-1500, 800, 0], True),
        ("Projectiles_64", projectile, [-2500, -200, 150], False),
    ]:
        actor = actors.spawn_actor_from_class(unreal.CastleMassSpawner, location)
        actor.set_actor_label(label)
        actor.set_editor_property("configuration", config)
        actor.set_editor_property("count", 64 if config == projectile else 256)
        actor.set_editor_property("columns", 16)
        actor.set_editor_property("spacing", 130.0)
        actor.set_editor_property("start_moving", moving)
    if not level.save_current_level():
        raise RuntimeError("无法保存 Mass 示例关卡")

unreal.log("MASS_EXAMPLES_READY: 三个基础配置、共享 GPU 骨骼动画提供器、专用网格和独立示例关卡。")
