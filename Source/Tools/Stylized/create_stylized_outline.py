"""重建暖棕描边及实例，将它挂在 MainMap 的晴空后处理体积。"""

from pathlib import Path
import sys

import unreal as ue


sys.path.insert(0, str(Path(__file__).resolve().parent))
from create_toon_assets import EDITING, ROOT, compile_material, connect, get_or_create, node, require, save


FOLDER = f'{ROOT}/PostProcess'
MAP = '/Game/MovingCastle/Maps/MainMap'
SCALARS = dict(OutlineThicknessPx=1.4, OutlineStrength=1.0, NormalEdgeStrength=0.30,
               FadeStartMeters=150.0, FadeEndMeters=400.0)


def create_outline():
    """生成显式声明场景纹理依赖的材质，保持原有实例和关卡引用。"""
    require(0.5 <= SCALARS['OutlineThicknessPx'] <= 4.0, '描边宽度超出制作范围')
    require(0.0 <= SCALARS['OutlineStrength'] <= 1.0 and 0.0 <= SCALARS['NormalEdgeStrength'] <= 1.0,
            '描边强度须在 0～1 内')
    require(0 < SCALARS['FadeStartMeters'] < SCALARS['FadeEndMeters'], '描边淡出距离顺序无效')
    material = get_or_create(f'{FOLDER}/M_PP_StylizedOutline', ue.Material, ue.MaterialFactoryNew)
    for expression in list(EDITING.get_material_expressions(material)):
        EDITING.delete_material_expression(material, expression)
    require(EDITING.get_num_material_expressions(material) == 0, f'{material.get_path_name()} 仍有旧节点')
    # 负优先级使风格线先写入场景，已有悬停白边以默认优先级叠在其上。
    material.set_editor_properties(dict(material_domain=ue.MaterialDomain.MD_POST_PROCESS,
                                        blendable_location=ue.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF,
                                        blendable_priority=-10))
    inputs = {}
    for index, (name, texture) in enumerate((
        ('SceneColor', ue.SceneTextureId.PPI_POST_PROCESS_INPUT0),
        ('SceneDepth', ue.SceneTextureId.PPI_SCENE_DEPTH),
        ('WorldNormal', ue.SceneTextureId.PPI_WORLD_NORMAL),
        ('CustomDepth', ue.SceneTextureId.PPI_CUSTOM_DEPTH),
        ('CustomStencil', ue.SceneTextureId.PPI_CUSTOM_STENCIL),
    )):
        expression = node(material, ue.MaterialExpressionSceneTexture, -690, index * 150)
        expression.set_editor_property('scene_texture_id', texture)
        inputs[name] = expression
    for index, (name, value) in enumerate(SCALARS.items()):
        parameter = node(material, ue.MaterialExpressionScalarParameter, -1000, index * 115)
        parameter.set_editor_properties(dict(parameter_name=name, default_value=value))
        inputs[name] = parameter
    color = node(material, ue.MaterialExpressionVectorParameter, -1000, 610)
    color.set_editor_properties(dict(parameter_name='InkTint', default_value=ue.LinearColor(0.46, 0.33, 0.27, 1.0)))
    inputs['InkTint'] = color
    bit = node(material, ue.MaterialExpressionConstant, -1000, 760)
    # 与 CastleRenderingConstants::StylizedNoOutlineStencilBit 对应。
    bit.set_editor_property('r', 1.0)
    inputs['NoOutlineBit'] = bit

    pins = []
    for name in inputs:
        pin = ue.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    custom = node(material, ue.MaterialExpressionCustom, -250, 0)
    custom.set_editor_properties(dict(description='深度与法线的暖棕描边',
                                      output_type=ue.CustomMaterialOutputType.CMOT_FLOAT3, inputs=pins,
                                      code=Path(__file__).with_name('M_PP_StylizedOutline.hlsl').read_text(encoding='utf-8')))
    for name, expression in inputs.items():
        connect(expression, custom, name)
    require(EDITING.connect_material_property(custom, '', ue.MaterialProperty.MP_EMISSIVE_COLOR),
            f'{material.get_path_name()} 无法输出场景颜色')
    compile_material(material)

    instance = get_or_create(f'{FOLDER}/MI_PP_StylizedOutline', ue.MaterialInstanceConstant,
                             ue.MaterialInstanceConstantFactoryNew)
    EDITING.set_material_instance_parent(instance, material)
    save(material)
    save(instance)
    return instance


def attach_outline(instance):
    """只修改项目的晴空体积，权重与同一体积的其它 Blendable 相互独立。"""
    levels = ue.get_editor_subsystem(ue.LevelEditorSubsystem)
    actors = ue.get_editor_subsystem(ue.EditorActorSubsystem)
    require(not levels.is_in_play_in_editor(), '需先退出 PIE 再写入后处理')
    require(levels.load_level(MAP), f'无法打开 {MAP}')
    volume, = [actor for actor in actors.get_all_level_actors()
               if isinstance(actor, ue.PostProcessVolume) and actor.get_actor_label() == 'Stylized Exposure']
    require(volume.get_editor_property('unbound'), f'{volume.get_path_name()} 必须覆盖全关卡')
    volume.add_or_update_blendable(instance, 1.0)
    # 塔侧有透明字边，允许此信息材质将 C++ 设置的 bit 0 写入 CustomDepth。
    tower = ue.load_asset('/Game/ART/Material/MA_Tower.MA_Tower')
    require(isinstance(tower, ue.Material), '塔侧格面母材质 MA_Tower 不可用')
    tower.set_editor_property('allow_translucent_custom_depth_writes', True)
    compile_material(tower)
    save(tower)
    require(levels.save_current_level(), f'无法保存 {MAP}')


if __name__ == '__main__':
    attach_outline(create_outline())
