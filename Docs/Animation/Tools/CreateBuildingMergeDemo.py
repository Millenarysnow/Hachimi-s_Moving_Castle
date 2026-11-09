"""用 UE 原生 Python API 建立独立的零件合并演示关卡；不修改现有游戏关卡或建筑资产。"""

import unreal


MAP_PATH = "/Game/MovingCastle/Maps/BuildingMergeDemo"
STAGE_MATERIAL = "/Game/MovingCastle/FX/BuildingMerge/MI_DemoStage"
DEFINITION_PATH = "/Game/MovingCastle/Data/Buildings/DA_ArcherTower"


def spawn(actor_class, name, location, rotation=unreal.Rotator()):
    """只在本脚本刚创建的新关卡中生成 Actor，命名便于用户在 Outliner 识别。"""
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
        actor_class, unreal.Vector(*location), rotation)
    assert actor, "生成 Actor 失败：" + name
    actor.set_actor_label(name)
    return actor


assert not unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH), "演示关卡已存在，拒绝覆盖。"
definition = unreal.load_asset(DEFINITION_PATH)
assert definition, "找不到正式箭塔建筑定义。"
demo_class = unreal.load_class(None, "/Script/Moving_Castle.BuildingMergeDemo")
assert demo_class, "请先编译原生 BuildingMergeDemo 类。"

# 新建空白关卡，绝不保存或覆盖用户原来打开的游戏关卡。
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
assert world, "无法建立空白演示关卡。"
world.get_world_settings().set_editor_property("default_game_mode", unreal.GameModeBase.static_class())

# 展台只使用引擎方块与基础材质实例，不给合并效果引入新的美术依赖。
if not unreal.EditorAssetLibrary.does_asset_exist(STAGE_MATERIAL):
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MI_DemoStage", "/Game/MovingCastle/FX/BuildingMerge",
        unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    assert material, "无法创建展台材质。"
    parent = unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
    assert parent, "无法加载引擎基础材质。"
    unreal.MaterialEditingLibrary.set_material_instance_parent(material, parent)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        material, "Color", unreal.LinearColor(0.055, 0.073, 0.09, 1.0))
    assert unreal.EditorAssetLibrary.save_loaded_asset(material), "展台材质保存失败。"
else:
    material = unreal.load_asset(STAGE_MATERIAL)

floor = spawn(unreal.StaticMeshActor, "MergeDemo_Stage", (350, 0, -35))
floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube"))
floor.static_mesh_component.set_material(0, material)
floor.set_actor_scale3d(unreal.Vector(180, 180, 0.5))

# 太阳、天空和实时天光共同照亮真实建筑材质，固定相机曝光由原生演示 Actor 控制。
sun = spawn(unreal.DirectionalLight, "MergeDemo_KeyLight", (0, 0, 3000), unreal.Rotator(-48, -35, 0))
sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
sun.light_component.set_editor_property("intensity", 4.0)
sun.light_component.set_editor_property("atmosphere_sun_light", True)
sky = spawn(unreal.SkyLight, "MergeDemo_SkyLight", (0, 0, 2000))
sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
sky.light_component.set_editor_property("real_time_capture", True)
sky.light_component.set_editor_property("intensity", 1.0)
spawn(unreal.SkyAtmosphere, "MergeDemo_Sky", (0, 0, 0))

demo = spawn(demo_class, "MergeDemo_PlayToPreview", (0, 0, 0))
demo.set_editor_property("building_definition", definition)
demo.set_editor_property("start_delay", 2.0)
demo.set_editor_property("loop", True)

unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
    unreal.Vector(2100, -2800, 1800), unreal.Rotator(-23, 122, 0))
assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH), "演示关卡保存失败。"
unreal.log("BUILDING_MERGE_DEMO_CREATED: " + MAP_PATH)
