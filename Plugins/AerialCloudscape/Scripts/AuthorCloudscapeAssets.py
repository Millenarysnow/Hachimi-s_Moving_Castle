"""在 UE 编辑器制作独立体积云材质与纹理。关卡、玩法和引擎示例资产不在此脚本写入范围内。"""

from pathlib import Path

import unreal as ue


ROOT = '/AerialCloudscape'
ASSETS = ue.EditorAssetLibrary
EDIT = ue.MaterialEditingLibrary
TOOLS = ue.AssetToolsHelpers.get_asset_tools()


def required_path_switch(name):
    """从编辑器的命令行解析显式制作目录，支持 Windows 路径中的空格。"""
    _, _, values = ue.SystemLibrary.parse_command_line(ue.SystemLibrary.get_command_line())
    assert name in values and values[name], f'缺少制作参数 -{name}=绝对路径'
    return Path(values[name]).resolve(strict=True)


def save(asset):
    assert ASSETS.save_loaded_asset(asset, only_if_is_dirty=False), f'无法保存 {asset.get_path_name()}'


def import_linear_texture(source, repeat):
    """图片内容是线性密度数据，不能套照片的颜色解码或有损压缩。"""
    assert source.is_file(), f'缺少离线制作的源纹理：{source}'
    task = ue.AssetImportTask()
    task.set_editor_properties(dict(filename=source.as_posix(), destination_path=f'{ROOT}/Textures',
                                    automated=True, replace_existing=True, save=False))
    TOOLS.import_asset_tasks([task])
    texture, = task.get_objects()
    assert isinstance(texture, ue.Texture2D), f'未导入 Texture2D：{source}'
    texture.set_editor_properties(dict(srgb=False, compression_settings=ue.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,
                                       mip_gen_settings=ue.TextureMipGenSettings.TMGS_NO_MIPMAPS,
                                       filter=ue.TextureFilter.TF_BILINEAR))
    address = ue.TextureAddress.TA_WRAP if repeat else ue.TextureAddress.TA_CLAMP
    texture.set_editor_properties(dict(address_x=address, address_y=address))
    save(texture)
    return texture


def make_volume(atlas):
    """行优先图集转换为可原生三线性采样的 64³ 体积，保留图集供编辑器重建。"""
    name = 'VT_AerialCloudShape'
    folder = f'{ROOT}/Textures'
    path = f'{folder}/{name}'
    if ASSETS.does_asset_exist(path):
        volume = ASSETS.load_asset(path)
    else:
        volume = TOOLS.create_asset(name, folder, ue.VolumeTexture, ue.VolumeTextureFactory())
    assert isinstance(volume, ue.VolumeTexture), f'无法创建或加载 {path}'
    # 分开通知属性修改，源图设置会让 UE 当场重建体积；图集维度不允许被误判为其他切片数。
    volume.set_editor_property('source2d_texture', atlas)
    assert volume.get_editor_property('source2d_tile_size_x') == 64 and volume.get_editor_property('source2d_tile_size_y') == 64, path
    volume.set_editor_properties(dict(srgb=False, compression_settings=ue.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,
                                      address_mode=ue.TextureAddress.TA_WRAP,
                                      mip_gen_settings=ue.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE,
                                      filter=ue.TextureFilter.TF_TRILINEAR))
    save(volume)
    return volume


def node(material, kind, x, y, **properties):
    expression = EDIT.create_material_expression(material, kind, x, y)
    assert expression, f'{material.get_name()} 无法创建 {kind}'
    expression.set_editor_properties(properties)
    return expression


def connect(source, target, input_name, output_name=''):
    assert EDIT.connect_material_expressions(source, output_name, target, input_name), (
        f'无法接线 {source.get_name()}.{output_name} -> {target.get_name()}.{input_name}'
    )


def custom(material, x, y, description, code, output, sources):
    """材质图只承载参数和光照入口，密度算法由插件着色器统一维护。"""
    inputs = []
    for name in sources:
        item = ue.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    result = node(material, ue.MaterialExpressionCustom, x, y, description=description, code=code,
                  output_type=output, include_file_paths=['/Plugin/AerialCloudscape/Private/AerialCloudDensity.ush'],
                  inputs=inputs)
    for name, (source, output_name) in sources.items():
        connect(source, result, name, output_name)
    return result


def make_material(coverage, volume):
    name = 'M_AerialCloudscape'
    folder = f'{ROOT}/Materials'
    path = f'{folder}/{name}'
    if ASSETS.does_asset_exist(path):
        material = ASSETS.load_asset(path)
        assert isinstance(material, ue.Material), f'{path} 不是材质'
        # 逐项删除预先取出的副本。UE 5.8 的批量接口边遍历边移除，会漏掉相邻节点。
        for expression in list(EDIT.get_material_expressions(material)):
            EDIT.delete_material_expression(material, expression)
        assert EDIT.get_num_material_expressions(material) == 0, f'{path} 仍有旧节点'
    else:
        material = TOOLS.create_asset(name, folder, ue.Material, ue.MaterialFactoryNew())
    assert material, f'无法创建 {path}'
    # 传统 Volume 输出由引擎兼容到 Substrate 体积；插件不要求宿主开启 Substrate。
    material.set_editor_properties(dict(material_domain=ue.MaterialDomain.MD_VOLUME,
                                        blend_mode=ue.BlendMode.BLEND_ADDITIVE,
                                        shading_model=ue.MaterialShadingModel.MSM_DEFAULT_LIT))
    EDIT.set_base_material_usage(material, ue.MaterialUsage.MATUSAGE_VOLUMETRIC_CLOUD)

    world = node(material, ue.MaterialExpressionWorldPosition, -1120, -550)
    relative = node(material, ue.MaterialExpressionWorldPosition, -1120, -430,
                    world_position_shader_offset=ue.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
    distance = node(material, ue.MaterialExpressionLength, -870, -430)
    connect(relative, distance, '')
    altitude = node(material, ue.MaterialExpressionCloudSampleAttribute, -1080, -295)
    phase = node(material, ue.MaterialExpressionVectorParameter, -1100, -120,
                 parameter_name='CloudTravelPhase', default_value=ue.LinearColor(0, 0, 0, 0))
    period, extinction, start, end = [node(material, ue.MaterialExpressionScalarParameter, -1100, y,
                                           parameter_name=param, default_value=value)
                                      for y, param, value in ((65, 'CloudPeriodKm', 24.0),
                                                              (175, 'CloudExtinction', 0.043),
                                                              (285, 'CloudFadeStartKm', 17.0),
                                                              (395, 'CloudFadeEndKm', 40.0))]
    texture_2d = node(material, ue.MaterialExpressionTextureObjectParameter, -1100, 520,
                      parameter_name='CloudCoverage', texture=coverage, sampler_type=ue.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    texture_3d = node(material, ue.MaterialExpressionTextureObjectParameter, -1100, 690,
                      parameter_name='CloudShape', texture=volume, sampler_type=ue.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

    uv = custom(material, -680, -550, '按航程移动世界 XY',
                'return AerialCloudUV(World, Phase, PeriodKm);', ue.CustomMaterialOutputType.CMOT_FLOAT2,
                dict(World=(world, ''), Phase=(phase, ''), PeriodKm=(period, '')))
    support = custom(material, -370, -300, '覆盖、保守密度与远距淡出',
                     'return AerialCloudSupport(Weather, WeatherSampler, UV, Height, Distance, StartKm, EndKm);',
                     ue.CustomMaterialOutputType.CMOT_FLOAT4,
                     dict(Weather=(texture_2d, ''), UV=(uv, ''), Height=(altitude, 'NormAltitudeInLayer'),
                          Distance=(distance, ''), StartKm=(start, ''), EndKm=(end, '')))
    advanced = node(material, ue.MaterialExpressionVolumetricAdvancedMaterialOutput, 240, -340,
                    ray_march_volume_shadow=True, gray_scale_material=True,
                    multi_scattering_approximation_octave_count=1,
                    const_multi_scattering_contribution=0.34, const_multi_scattering_occlusion=0.65,
                    const_phase_g=0.38, const_phase_g2=-0.30, const_phase_blend=0.32)
    connect(support, advanced, 'ConservativeDensity')
    cached = node(material, ue.MaterialExpressionVolumetricAdvancedMaterialInput, -375, 125)
    medium = custom(material, -140, 125, '三维团块消光与天空可见度',
                     'return AerialCloudMedium(Shape, ShapeSampler, UV, Height, HeightCm, PeriodKm, Support, Extinction);',
                     ue.CustomMaterialOutputType.CMOT_FLOAT2,
                     dict(Shape=(texture_3d, ''), UV=(uv, ''), Height=(altitude, 'NormAltitudeInLayer'),
                          HeightCm=(altitude, 'AltitudeInLayer'), PeriodKm=(period, ''),
                          Support=(cached, 'ConservativeDensity as Float4'), Extinction=(extinction, '')))
    density = node(material, ue.MaterialExpressionComponentMask, 130, 125, r=True, g=False)
    occlusion = node(material, ue.MaterialExpressionComponentMask, 130, 300, r=False, g=True)
    connect(medium, density, '')
    connect(medium, occlusion, '')
    albedo = node(material, ue.MaterialExpressionConstant, 135, 510, r=0.98)
    assert EDIT.connect_material_property(density, '', ue.MaterialProperty.MP_SUBSURFACE_COLOR), '无法接入体积消光'
    assert EDIT.connect_material_property(albedo, '', ue.MaterialProperty.MP_BASE_COLOR), '无法接入体积反照率'
    assert EDIT.connect_material_property(occlusion, '', ue.MaterialProperty.MP_AMBIENT_OCCLUSION), '无法接入云内天光遮蔽'

    EDIT.recompile_material(material)
    save(material)
    ue.log(f'AERIAL_CLOUD_ASSET {material.get_path_name()}：体积材质及无缝三维形状已制作。')


if __name__ == '__main__':
    # 编辑器脚本与运行世界分离，导入前显式扫描插件自管资产。
    source_dir = required_path_switch('AerialCloudTextureSource')
    ue.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], force_rescan=True)
    weather = import_linear_texture(source_dir / 'T_AerialCloudCoverage.png', repeat=True)
    source_atlas = import_linear_texture(source_dir / 'T_AerialCloudShapeAtlas.png', repeat=False)
    make_material(weather, make_volume(source_atlas))
