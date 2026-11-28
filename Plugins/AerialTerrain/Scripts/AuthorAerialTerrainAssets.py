"""在 UE 编辑器中制作插件的固定网格和两份材质。运行时不加载此脚本。"""

import unreal as ue


ROOT = '/AerialTerrain'
EDIT = ue.MaterialEditingLibrary
ASSET = ue.EditorAssetLibrary


def register_authored_assets():
    """Commandlet 默认不扫描插件内容；显式登记本脚本负责的资产目录。"""
    registry = ue.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([ROOT], force_rescan=True)


def node(material, kind, x, y, **properties):
    """用反射设置材质节点，接线失败立即终止制作，不保存残缺效果。"""
    expression = EDIT.create_material_expression(material, kind, x, y)
    assert expression, f'{material.get_name()} 无法创建节点 {kind}'
    for name, value in properties.items():
        expression.set_editor_property(name, value)
    return expression


def link(material, expression, property_name):
    assert EDIT.connect_material_property(expression, '', property_name), (
        f'{material.get_name()} 无法连接输出 {property_name}'
    )


def create_custom(material, pos, description, include, code, output_type, inputs):
    """Custom 节点只负责接线，算法全部保存在可审阅的 .ush 里。"""
    arguments = []
    for name in inputs:
        argument = ue.CustomInput()
        argument.set_editor_property('input_name', name)
        arguments.append(argument)
    custom = node(material, ue.MaterialExpressionCustom, *pos, description=description,
                  include_file_paths=[f'/Plugin/AerialTerrain/Private/{include}.ush'],
                  code=code, output_type=output_type, inputs=arguments)
    for name, source in inputs.items():
        assert EDIT.connect_material_expressions(source, '', custom, name), (
            f'{material.get_name()} 无法连接 {description}.{name}'
        )
    return custom


def make_material(name):
    """只生成插件自管的材质，重建节点时保留已摆放 Actor 的资产引用。"""
    folder = f'{ROOT}/Materials'
    path = f'{folder}/{name}'
    if ASSET.does_asset_exist(path):
        material = ASSET.load_asset(path)
        assert isinstance(material, ue.Material), f'{path} 不是 Material'
        # UE 5.8 的批量删除可能漏掉相邻节点，先取副本再逐个移除。
        for expression in list(EDIT.get_material_expressions(material)):
            EDIT.delete_material_expression(material, expression)
        assert EDIT.get_num_material_expressions(material) == 0, f'{path} 仍有旧节点'
    else:
        material = ue.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, ue.Material, ue.MaterialFactoryNew())
    assert material, f'无法创建或打开 {path}'
    material.set_editor_property('shading_model', ue.MaterialShadingModel.MSM_UNLIT)
    return material


def save_material(material):
    errors = EDIT.recompile_material(material)
    assert not errors, f'{material.get_path_name()} 编译失败：{errors}'
    assert ASSET.save_loaded_asset(material, only_if_is_dirty=False), f'无法保存 {material.get_path_name()}'


def make_field_texture_binding():
    """为材质声明线性 Texture2D 参数。每局的数据写入独立 RT，不改动这份资产。"""
    name = 'RT_AerialTerrainFieldBinding'
    folder = f'{ROOT}/Materials'
    path = f'{folder}/{name}'
    if ASSET.does_asset_exist(path):
        texture = ASSET.load_asset(path)
        assert isinstance(texture, ue.TextureRenderTarget2D), f'{path} 不是 TextureRenderTarget2D'
    else:
        factory = ue.TextureRenderTargetFactoryNew()
        texture = ue.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, ue.TextureRenderTarget2D, factory)
        assert texture, f'无法创建 {path}'
    texture.set_editor_property('size_x', 16)
    texture.set_editor_property('size_y', 16)
    texture.set_editor_property('render_target_format', ue.TextureRenderTargetFormat.RTF_RGBA16F)
    texture.set_editor_property('address_x', ue.TextureAddress.TA_WRAP)
    texture.set_editor_property('address_y', ue.TextureAddress.TA_WRAP)
    # 和运行时数据图使用相同的采样设置，使材质编辑视图也能访问合法的 mip 链。
    texture.set_editor_property('auto_generate_mips', True)
    texture.set_editor_property('mips_address_u', ue.TextureAddress.TA_WRAP)
    texture.set_editor_property('mips_address_v', ue.TextureAddress.TA_WRAP)
    texture.set_editor_property('mips_sampler_filter', ue.TextureFilter.TF_BILINEAR)
    texture.set_editor_property('filter', ue.TextureFilter.TF_TRILINEAR)
    assert ASSET.save_loaded_asset(texture, only_if_is_dirty=False), f'无法保存 {path}'
    return texture


def vector4_parameter(material, x, y, name, value):
    # Vector Parameter 的主输出是 RGB，单独把 A 拼回去后再传入 HLSL 的 float4。
    parameter = node(material, ue.MaterialExpressionVectorParameter, x, y,
                     parameter_name=name, default_value=ue.LinearColor(*value))
    result = node(material, ue.MaterialExpressionAppendVector, x + 240, y)
    assert EDIT.connect_material_expressions(parameter, '', result, 'A'), f'{name} 无法连接 RGB'
    assert EDIT.connect_material_expressions(parameter, 'A', result, 'B'), f'{name} 无法连接 Alpha'
    return result


def make_field_material():
    """Canvas 材质把高程、水位、湿度三个数据通道写到线性 RT，不做显示着色。"""
    material = make_material('M_AerialTerrainField')
    material.set_editor_property('blend_mode', ue.BlendMode.BLEND_OPAQUE)
    uv = node(material, ue.MaterialExpressionTextureCoordinate, -610, -80)
    seed = node(material, ue.MaterialExpressionScalarParameter, -610, 120,
                parameter_name='TerrainSeed', default_value=12021.0)
    field = create_custom(material, (-300, 0), '周期地貌数据', 'AerialTerrainField',
                          'return AerialBuildField(UV, Seed);', ue.CustomMaterialOutputType.CMOT_FLOAT3,
                          {'UV': uv, 'Seed': seed})
    link(material, field, ue.MaterialProperty.MP_EMISSIVE_COLOR)
    save_material(material)
    return material


def make_surface_material():
    """位移、颜色和边缘遮罩使用同一网格；RT 在原生 Actor 初始化时绑定。"""
    material = make_material('M_AerialTerrainSurface')
    material.set_editor_property('blend_mode', ue.BlendMode.BLEND_MASKED)
    material.set_editor_property('has_pixel_animation', True)
    # 地貌保留自身深度，远处才通过色值融入背景。标为天空会与 MainMap 的天空球争抢天空通道。
    material.set_editor_property('is_sky', False)
    uv = node(material, ue.MaterialExpressionTextureCoordinate, -840, -200)
    # UV1 不用于美术贴图；其两个分量是以面片边长为单位的网格采样足迹。
    spacing = node(material, ue.MaterialExpressionTextureCoordinate, -840, -290, coordinate_index=1)
    # 此资产是材质编译所需的 RT 类型声明。Actor 会先绑定真实地貌，再让网格可见。
    texture_type = make_field_texture_binding()
    field = node(material, ue.MaterialExpressionTextureObjectParameter, -840, -410,
                 parameter_name='TerrainField', texture=texture_type,
                 sampler_type=ue.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    offset = vector4_parameter(material, -840, -40, 'SamplingOffset', (0.42, 0.31, 0, 0))
    geometry = vector4_parameter(material, -840, 140, 'GeometrySettings', (1.18, 235000, 0.038, 1 / 2048))
    sun = vector4_parameter(material, -840, 320, 'SunAndHazeStart', (-0.64, 0.12, 0.76, 850000))
    haze = vector4_parameter(material, -840, 510, 'HazeAndFadeEnd', (0.69, 0.81, 0.86, 3050000))
    camera_relative = node(material, ue.MaterialExpressionWorldPosition, -840, 705,
                           world_position_shader_offset=ue.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
    distance = node(material, ue.MaterialExpressionLength, -585, 705)
    assert EDIT.connect_material_expressions(camera_relative, '', distance, ''), '无法读取地表像素的视距'
    common_inputs = {'Field': field, 'UV': uv, 'GridSpacing': spacing, 'Offset': offset, 'Geometry': geometry}
    displacement = create_custom(material, (-335, -410), '滚动高度场，水线与地形共用网格', 'AerialTerrainSurface',
                                 'return AerialTerrainDisplacement(Field, FieldSampler, UV, GridSpacing, Offset, Geometry);',
                                 ue.CustomMaterialOutputType.CMOT_FLOAT3, common_inputs)
    color = create_custom(material, (-335, -85), '气候群系、林冠、浅滩与空气透视', 'AerialTerrainSurface',
                          'return AerialTerrainColor(Field, FieldSampler, UV, GridSpacing, Offset, Geometry, Sun, Haze, DistanceCm);',
                         ue.CustomMaterialOutputType.CMOT_FLOAT3,
                          {**common_inputs, 'Sun': sun, 'Haze': haze, 'DistanceCm': distance})
    # 颜色先在远处完全融入天空，再裁掉面片外缘。遮罩不再随逐帧抖动改变覆盖范围。
    cutoff = create_custom(material, (-335, 390), '天空颜色融合后裁掉地表外缘', 'AerialTerrainSurface',
                           'return AerialTerrainCoverage(Haze, DistanceCm);',
                           ue.CustomMaterialOutputType.CMOT_FLOAT1, {'Haze': haze, 'DistanceCm': distance})
    link(material, displacement, ue.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    link(material, color, ue.MaterialProperty.MP_EMISSIVE_COLOR)
    link(material, cutoff, ue.MaterialProperty.MP_OPACITY_MASK)
    save_material(material)
    return material


def make_fixed_surface_mesh():
    """生成一片不可碰撞的静态平面；中心 60 m 采样，远端用渐疏的布点控制顶点量。"""
    path = f'{ROOT}/Meshes/SM_AerialTerrainGrid'
    central = 840000.0
    extent = 3640000.0
    axis_positive = [6000.0 * i for i in range(141)]
    for i in range(1, 66):
        t = i / 65.0
        axis_positive.append(central + 390000.0 * t + (extent - central - 390000.0) * t * t * t)
    axis = [-p for p in reversed(axis_positive[1:])] + axis_positive
    stride = len(axis)
    vertices = [ue.Vector(x, y, 0) for y in axis for x in axis]
    diameter = 2 * extent
    uv = [ue.Vector2D(x / diameter + 0.5, y / diameter + 0.5) for y in axis for x in axis]
    # 每个顶点以两侧较大的间距为足迹；在渐疏区，横纵跨度均从实际网格推导。
    # 两个外缘顶点只有一个相邻区间，直接使用该区间宽度。
    gaps = [b - a for a, b in zip(axis, axis[1:])]
    spacing = [gap / diameter for gap in [gaps[0], *(max(a, b) for a, b in zip(gaps, gaps[1:])), gaps[-1]]]
    uv1 = [ue.Vector2D(x, y) for y in spacing for x in spacing]
    triangles = []
    for y in range(stride - 1):
        for x in range(stride - 1):
            a = y * stride + x
            b, d = a + 1, a + stride
            triangles.extend((ue.IntVector(a, d, b), ue.IntVector(b, d, d + 1)))

    data = ue.GeometryScriptSimpleMeshBuffers()
    for name, values in [('vertices', vertices), ('normals', [ue.Vector(0, 0, 1)] * len(vertices)),
                         ('uv0', uv), ('uv1', uv1), ('triangles', triangles)]:
        data.set_editor_property(name, values)
    dynamic_mesh = ue.DynamicMesh()
    ue.GeometryScript_MeshEdits.append_buffers_to_mesh(dynamic_mesh, data)
    # 重制已存在的网格也保留包名和对象引用，主关卡不需要重绑地表 Actor。
    if ASSET.does_asset_exist(path):
        asset = ASSET.load_asset(path)
        assert isinstance(asset, ue.StaticMesh), f'{path} 不是 StaticMesh'
        _, result = ue.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(
            dynamic_mesh, asset, ue.GeometryScriptCopyMeshToAssetOptions(), ue.GeometryScriptMeshWriteLOD())
    else:
        options = ue.GeometryScriptCreateNewStaticMeshAssetOptions()
        options.set_editor_property('enable_collision', False)
        asset, result = ue.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dynamic_mesh, path, options)
    assert asset and result == ue.GeometryScriptOutcomePins.SUCCESS, f'无法写入 {path}：{result}'

    # 固定面片的非等距顶点必须使用完整 UV 精度，否则数十公里范围内会量化错位。
    subsystem = ue.get_editor_subsystem(ue.StaticMeshEditorSubsystem)
    build = subsystem.get_lod_build_settings(asset, 0)
    build.set_editor_property('use_full_precision_u_vs', True)
    build.set_editor_property('generate_lightmap_u_vs', False)
    subsystem.set_lod_build_settings(asset, 0, build)
    asset.set_editor_property('positive_bounds_extension', ue.Vector(0, 0, 250000))
    asset.set_editor_property('negative_bounds_extension', ue.Vector(0, 0, 250000))
    assert ASSET.save_loaded_asset(asset, only_if_is_dirty=False), f'无法保存 {path}'
    ue.log(f'AERIAL_ASSET {path}: {len(vertices)} vertices, {len(triangles)} triangles')
    return asset


if __name__ == '__main__':
    register_authored_assets()
    make_field_material()
    make_surface_material()
    make_fixed_surface_mesh()
    ue.log('AERIAL_ASSET 已生成高空地表资产')
