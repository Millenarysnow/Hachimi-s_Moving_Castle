"""在 UE 编辑器中从项目源图发布开始界面的纹理、纸面材质与独立关卡。"""
from pathlib import Path

import unreal


UI = "/Game/MovingCastle/UI/MainMenu"
MAP = "/Game/MovingCastle/Maps/StartMenu"
SOURCE = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "RawContent/MainMenu"


def require(value, message):
    """资产操作的失败必须带具体名称中止发布，防止持久化未连接的材质图。"""
    if not value:
        raise RuntimeError(message)
    return value


def import_texture(filename, name, for_ui):
    """纸纹使用线性光照采样，透明字标使用 UI 压缩保留笔画边缘。"""
    task = unreal.AssetImportTask()
    task.filename = str(SOURCE / filename)
    task.destination_path = UI
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    # 显式指定 TextureFactory，避免不同编辑器的 Interchange 设置改变目标文件名。
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = require(unreal.load_asset(UI + "/" + name), "导入纹理失败 " + name)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("never_stream", True)
    if for_ui:
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    require(unreal.EditorAssetLibrary.save_loaded_asset(texture, False), "保存纹理失败 " + name)
    return texture


def paper_material(paper):
    """使用实际场景深度区分城堡和留白，纸面始终按视口坐标采样。"""
    path = UI + "/M_StartMenuPaper"
    mat = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else require(
        unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_StartMenuPaper", UI, unreal.Material, unreal.MaterialFactoryNew()),
        "创建材质失败 " + path)
    lib = unreal.MaterialEditingLibrary
    lib.delete_all_material_expressions(mat)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    uv = lib.create_material_expression(mat, unreal.MaterialExpressionScreenPosition, -960, 180)
    sample = lib.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -660, 180)
    sample.set_editor_property("texture", paper)
    require(lib.connect_material_expressions(uv, "", sample, ""), "连接纸纹视口 UV 失败")
    scene = lib.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -660, -200)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    depth = lib.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -660, -20)
    depth.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_SCENE_DEPTH)
    mix = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, -240, 0)
    mix.set_editor_property("description", "战斗原色建筑与暖灰皱纹纸")
    mix.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    # 空场景的无限远深度与本舞台城堡有充分间距。采用前作暖纸色和褶皱强度。
    mix.set_editor_property("code",
        "float3 paper = float3(0.965, 0.958, 0.94) * lerp(1.0, Paper.r, 0.48);\n"
        "return Depth.r > 10000000.0 ? paper : Scene.rgb;")
    inputs = []
    for name in ("Scene", "Depth", "Paper"):
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        inputs.append(item)
    mix.set_editor_property("inputs", inputs)
    for node, pin, name in ((scene, "Color", "Scene"), (depth, "Color", "Depth"), (sample, "RGB", "Paper")):
        require(lib.connect_material_expressions(node, pin, mix, name), "连接纸面材质失败 " + name)
    require(lib.connect_material_property(mix, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR), "连接纸面输出失败")
    lib.recompile_material(mat)
    require(unreal.EditorAssetLibrary.save_loaded_asset(mat, False), "保存纸面材质失败 " + path)
    return mat


def publish():
    """仅更新指定的菜单舞台。已有菜单地图保持原世界，重复发布不会增加重复 Actor。"""
    title = import_texture("Logo_Title.png", "T_StartMenuLogo", True)
    paper = paper_material(import_texture("Paper_Crumpled.png", "T_StartMenuPaper", False))
    font = require(unreal.load_asset("/Game/MovingCastle/Fonts/MicrosoftYaHei_Font"), "缺少菜单中文字体")
    stage_class = require(unreal.load_class(None, "/Script/Moving_Castle.CastleMenuStage"), "缺少菜单舞台 C++ 类")
    mode = require(unreal.load_class(None, "/Script/Moving_Castle.CastleMenuGameMode"), "缺少菜单模式 C++ 类")
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        require(level.load_level(MAP), "打开开始地图失败 " + MAP)
        stages = [a for a in editor.get_all_level_actors() if a.get_class() == stage_class]
        require(len(stages) == 1, "已存在的开始地图必须包含一个菜单舞台")
        stage = stages[0]
    else:
        require(unreal.EditorLoadingAndSavingUtils.new_blank_map(False), "创建菜单空关卡失败")
        stage = require(editor.spawn_actor_from_class(stage_class, unreal.Vector()), "创建菜单舞台失败")
    stage.set_actor_label("开始界面")
    stage.set_editor_property("title_texture", title)
    stage.set_editor_property("menu_font", font)
    stage.set_editor_property("paper_material", paper)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", mode)
    require(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP), "保存开始地图失败 " + MAP)
    unreal.log("开始界面资产已发布 " + MAP)


if __name__ == "__main__":
    publish()
