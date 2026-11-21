"""在 UE 编辑器中生成商店卡牌的 UMG 材质；不会重写其它资产。"""
from pathlib import Path

import unreal


MATERIAL_FOLDER = "/Game/MovingCastle/UI/Cards"
MATERIAL_NAME = "M_CastleCardFace"
MATERIAL_PATH = f"{MATERIAL_FOLDER}/{MATERIAL_NAME}"


def require(result, message):
    """引擎接口返回失败时停止，不留下一张可保存但接线不完整的材质。"""
    if not result:
        raise RuntimeError(message)
    return result


def create_face():
    """把可审查的 HLSL 源码、纹理口和透明确立成单个 UI 材质资产。"""
    editing = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    # 节点图由本工具完整拥有。已有资产时重建同一张材质，保留对象路径和运行时引用。
    if assets.does_asset_exist(MATERIAL_PATH):
        material = require(assets.load_asset(MATERIAL_PATH), f"无法加载卡面材质 {MATERIAL_PATH}")
        require(isinstance(material, unreal.Material), f"{MATERIAL_PATH} 不是 Material")
        editing.delete_all_material_expressions(material)
    else:
        material = require(
            unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                MATERIAL_NAME, MATERIAL_FOLDER, unreal.Material, unreal.MaterialFactoryNew()),
            f"无法创建卡面材质 {MATERIAL_PATH}")
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    def node(kind, x, y):
        return require(editing.create_material_expression(material, kind, x, y), f"创建节点失败：{kind}")

    uv = node(unreal.MaterialExpressionTextureCoordinate, -720, -150)
    tint = node(unreal.MaterialExpressionVectorParameter, -720, -40)
    tint.set_editor_property("parameter_name", "CardTint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.02, 0.19, 0.50, 1.0))
    state = node(unreal.MaterialExpressionVectorParameter, -720, 140)
    state.set_editor_property("parameter_name", "CardState")
    state.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    portrait = node(unreal.MaterialExpressionTextureObjectParameter, -720, 320)
    portrait.set_editor_property("parameter_name", "Portrait")
    # 仅供材质编译的纹理类型样本；运行时未提交成像前明确关闭建筑层，不展示此纹理。
    sampler_type_template = require(assets.load_asset("/Engine/EngineResources/WhiteSquareTexture"),
                                    "无法加载卡面材质所需的引擎纹理类型样本")
    portrait.set_editor_property("texture", sampler_type_template)
    portrait.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    face = node(unreal.MaterialExpressionCustom, -360, 0)
    face.set_editor_property("description", "独立卡窗：远景、透明建筑、前景与受遮罩约束的箔光")
    face.set_editor_property("code", Path(__file__).with_name("CastleCardFace.hlsl").read_text(encoding="utf-8-sig"))
    face.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    bindings = (("UV", uv, ""), ("CardTint", tint, ""), ("CardState", state, "RGBA"), ("Portrait", portrait, ""))
    pins = []
    for name, _, _ in bindings:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    face.set_editor_property("inputs", pins)
    for name, expression, output in bindings:
        require(editing.connect_material_expressions(expression, output, face, name), f"卡面参数接线失败：{name}")

    rgb = node(unreal.MaterialExpressionComponentMask, -130, -60)
    alpha = node(unreal.MaterialExpressionComponentMask, -130, 100)
    for mask, channels, prop in ((rgb, "rgb", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                                 (alpha, "a", unreal.MaterialProperty.MP_OPACITY)):
        for channel in "rgba":
            mask.set_editor_property(channel, channel in channels)
        require(editing.connect_material_expressions(face, "", mask, ""), "卡面输出遮罩接线失败")
        require(editing.connect_material_property(mask, "", prop), f"卡面输出接线失败：{prop}")
    editing.recompile_material(material)
    require(assets.save_loaded_asset(material, False), f"无法保存卡面材质 {MATERIAL_PATH}")
    unreal.log(f"CASTLE_CARD_FACE_SAVED {MATERIAL_PATH}")


create_face()
