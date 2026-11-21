"""在 UE 编辑器中生成整牌透视材质，不改动原卡窗材质或其他资产。"""
from pathlib import Path

import unreal


FOLDER = "/Game/MovingCastle/UI/Cards"
NAME = "M_CastleCardPerspective"
ASSET_PATH = f"{FOLDER}/{NAME}"


def require(result, message):
    """接线或保存不成功就终止，避免交付不完整的材质。"""
    if not result:
        raise RuntimeError(message)
    return result


def create_perspective():
    """生成预乘透明的 Retainer 材质；投影姿态由 C++ 提供。"""
    editing = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(ASSET_PATH):
        material = require(assets.load_asset(ASSET_PATH), f"无法打开 {ASSET_PATH}")
        require(isinstance(material, unreal.Material), f"{ASSET_PATH} 不是 Material")
        editing.delete_all_material_expressions(material)
    else:
        material = require(unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew()), f"无法创建 {ASSET_PATH}")
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ALPHA_COMPOSITE)

    def node(kind, x, y):
        return require(editing.create_material_expression(material, kind, x, y), f"创建透视节点失败：{kind}")

    uv = node(unreal.MaterialExpressionTextureCoordinate, -690, -160)
    composed = node(unreal.MaterialExpressionTextureObjectParameter, -690, -45)
    composed.set_editor_property("parameter_name", "ComposedCard")
    # 只声明纹理采样类型；第一次绘制时实际输入由 Slate 的 Retainer 绑定。
    composed.set_editor_property("texture", require(assets.load_asset("/Engine/EngineResources/WhiteSquareTexture"),
                                                   "找不到 Retainer 材质的纹理类型样本"))
    composed.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    x_axis = node(unreal.MaterialExpressionVectorParameter, -690, 105)
    y_axis = node(unreal.MaterialExpressionVectorParameter, -690, 295)
    for parameter, name, value in ((x_axis, "ProjectionX", unreal.LinearColor(1, 0, 0, 1)),
                                   (y_axis, "ProjectionY", unreal.LinearColor(0, 1, 0, 1))):
        parameter.set_editor_property("parameter_name", name)
        parameter.set_editor_property("default_value", value)
    aspect = node(unreal.MaterialExpressionScalarParameter, -690, 485)
    aspect.set_editor_property("parameter_name", "CardAspect")
    aspect.set_editor_property("default_value", 1.0)
    opacity = node(unreal.MaterialExpressionScalarParameter, -690, 595)
    opacity.set_editor_property("parameter_name", "CardOpacity")
    opacity.set_editor_property("default_value", 1.0)
    project = node(unreal.MaterialExpressionCustom, -340, 0)
    project.set_editor_property("description", "对含文字的完整牌面做逆透视投影")
    # Custom 节点会把源码嵌进函数体，须去掉编辑器可能写在文件开头的 UTF-8 BOM。
    project.set_editor_property("code", Path(__file__).with_name("CastleCardPerspective.hlsl").read_text(encoding="utf-8-sig"))
    project.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    bindings = (("UV", uv, ""), ("ComposedCard", composed, ""), ("ProjectionX", x_axis, "RGBA"),
                ("ProjectionY", y_axis, "RGBA"), ("CardAspect", aspect, ""), ("CardOpacity", opacity, ""))
    inputs = []
    for name, _, _ in bindings:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        inputs.append(pin)
    project.set_editor_property("inputs", inputs)
    for name, expression, output in bindings:
        require(editing.connect_material_expressions(expression, output, project, name), f"透视参数接线失败：{name}")

    # RGB 与 Alpha 原样送到预乘混合材质，透明区域不填背景色。
    for index, (channels, prop) in enumerate((("rgb", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                                              ("a", unreal.MaterialProperty.MP_OPACITY))):
        mask = node(unreal.MaterialExpressionComponentMask, -140, 140 * index)
        for channel in "rgba":
            mask.set_editor_property(channel, channel in channels)
        require(editing.connect_material_expressions(project, "", mask, ""), "透视输出遮罩接线失败")
        require(editing.connect_material_property(mask, "", prop), f"透视输出接线失败：{prop}")
    editing.recompile_material(material)
    require(assets.save_loaded_asset(material, False), f"无法保存 {ASSET_PATH}")
    unreal.log(f"CASTLE_CARD_PERSPECTIVE_SAVED {ASSET_PATH}")


create_perspective()
