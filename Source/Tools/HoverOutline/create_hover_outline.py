"""从同目录 HLSL 可重复生成悬停描边后处理材质。"""

from pathlib import Path
import unreal


MATERIAL_FOLDER = "/Game/MovingCastle/Rendering/PostProcess"
MATERIAL_NAME = "M_PP_HoverOutline"
MATERIAL_PATH = f"{MATERIAL_FOLDER}/{MATERIAL_NAME}"
HOVER_STENCIL_BIT = 1 << 1
OUTLINE_COLOR = (1.0, 1.0, 1.0, 1.0)
OUTLINE_THICKNESS_PX = 2.5
DEPTH_OCCLUSION_TOLERANCE_CM = 2.0
BLENDABLE_LOCATION_NAME = "BL_SCENE_COLOR_BEFORE_DOF"


def require(result, message):
    """引擎 API 返回失败时立即抛错，防止保存缺节点或缺接线的材质。"""
    if not result:
        raise RuntimeError(message)
    return result


def create_hover_outline():
    """完全重建脚本拥有的节点图，并拒绝越界的设计参数。"""
    if HOVER_STENCIL_BIT != 2:
        raise RuntimeError(f"悬停 Stencil 位必须与 C++ 常量保持一致，当前为 {HOVER_STENCIL_BIT}")
    if not 1.0 <= OUTLINE_THICKNESS_PX <= 8.0:
        raise RuntimeError(f"描边宽度超出 1～8 像素范围：{OUTLINE_THICKNESS_PX}")
    if not 0.5 <= DEPTH_OCCLUSION_TOLERANCE_CM <= 10.0:
        raise RuntimeError(f"深度遮挡容差超出 0.5～10 厘米范围：{DEPTH_OCCLUSION_TOLERANCE_CM}")
    if any(value < 0.0 or value > 10.0 for value in OUTLINE_COLOR):
        raise RuntimeError(f"描边颜色分量必须在 0～10 范围：{OUTLINE_COLOR}")

    assets = unreal.EditorAssetLibrary
    editing = unreal.MaterialEditingLibrary
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset_path = f"{MATERIAL_PATH}.{MATERIAL_NAME}"

    if assets.does_asset_exist(MATERIAL_PATH):
        material = require(assets.load_asset(MATERIAL_PATH), f"无法加载悬停描边材质 {MATERIAL_PATH}")
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"悬停描边资产 {MATERIAL_PATH} 不是 Material")
        editing.delete_all_material_expressions(material)
    else:
        require(assets.make_directory(MATERIAL_FOLDER), f"无法创建材质目录 {MATERIAL_FOLDER}")
        material = require(
            asset_tools.create_asset(
                MATERIAL_NAME,
                MATERIAL_FOLDER,
                unreal.Material,
                unreal.MaterialFactoryNew(),
            ),
            f"无法创建悬停描边材质 {MATERIAL_PATH}",
        )

    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    # Before DOF 位于 TSR 前的渲染分辨率阶段，深度采样坐标与像素线宽使用同一分辨率。
    # TSR 随后处理描边颜色和场景深度边缘，Before Bloom 则处于显示分辨率阶段。
    blendable_location = getattr(unreal.BlendableLocation, BLENDABLE_LOCATION_NAME)
    material.set_editor_property("blendable_location", blendable_location)

    def node(expression_type, x, y):
        """创建材质表达式并立即校验，避免 API 失败后留下不完整图表。"""
        return require(
            editing.create_material_expression(material, expression_type, x, y),
            f"创建材质表达式失败：{expression_type}",
        )

    def scene_texture(texture_id, x, y):
        """建立场景纹理依赖，使自定义 HLSL 可调用相同的 SceneTextureLookup。"""
        expression = node(unreal.MaterialExpressionSceneTexture, x, y)
        expression.set_editor_property("scene_texture_id", texture_id)
        return expression

    scene_color = scene_texture(
        unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, -1000, -180
    )
    scene_depth = scene_texture(
        unreal.SceneTextureId.PPI_SCENE_DEPTH, -1000, 0
    )
    custom_depth = scene_texture(
        unreal.SceneTextureId.PPI_CUSTOM_DEPTH, -1000, 180
    )
    custom_stencil = scene_texture(
        unreal.SceneTextureId.PPI_CUSTOM_STENCIL, -1000, 360
    )
    uv = node(unreal.MaterialExpressionTextureCoordinate, -1000, 540)

    color = node(unreal.MaterialExpressionVectorParameter, -1000, 700)
    color.set_editor_property("parameter_name", "OutlineColor")
    color.set_editor_property("default_value", unreal.LinearColor(*OUTLINE_COLOR))

    thickness = node(unreal.MaterialExpressionScalarParameter, -1000, 860)
    thickness.set_editor_property("parameter_name", "OutlineThicknessPx")
    thickness.set_editor_property("default_value", OUTLINE_THICKNESS_PX)

    depth_tolerance = node(unreal.MaterialExpressionScalarParameter, -1000, 1020)
    depth_tolerance.set_editor_property("parameter_name", "DepthOcclusionToleranceCm")
    depth_tolerance.set_editor_property("default_value", DEPTH_OCCLUSION_TOLERANCE_CM)

    stencil_bit = node(unreal.MaterialExpressionConstant, -1000, 1180)
    stencil_bit.set_editor_property("r", float(HOVER_STENCIL_BIT))

    custom = node(unreal.MaterialExpressionCustom, -360, 300)
    custom.set_editor_property("description", "按悬停 Stencil 位绘制深度裁剪的白色外轮廓")
    custom.set_editor_property(
        "code", Path(__file__).with_name("M_PP_HoverOutline.hlsl").read_text(encoding="utf-8-sig")
    )
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    bindings = (
        ("UV", uv, ""),
        ("SceneColor", scene_color, ""),
        ("SceneDepth", scene_depth, ""),
        ("CustomDepth", custom_depth, ""),
        ("CustomStencil", custom_stencil, ""),
        ("OutlineColor", color, ""),
        ("OutlineThicknessPx", thickness, ""),
        ("DepthOcclusionToleranceCm", depth_tolerance, ""),
        ("HoverStencilBit", stencil_bit, ""),
    )
    pins = []
    for input_name, _, _ in bindings:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", input_name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)

    for input_name, expression, output_name in bindings:
        require(
            editing.connect_material_expressions(expression, output_name, custom, input_name),
            f"悬停材质输入 {input_name} 接线失败",
        )
    require(
        editing.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
        "悬停描边材质无法连接到 Emissive Color",
    )

    editing.recompile_material(material)
    if material.get_editor_property("material_domain") != unreal.MaterialDomain.MD_POST_PROCESS:
        raise RuntimeError(f"悬停描边材质 {asset_path} 的材质域校验失败")
    require(assets.save_loaded_asset(material, False), f"无法保存悬停描边材质 {MATERIAL_PATH}")
    unreal.log(f"CASTLE_HOVER_OUTLINE_SAVED {asset_path}")


create_hover_outline()
