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
    # RGB 模式保留云的反照率色相；它不改变标量消光，也不染色云后方的地表。
    # 云内多次散射为阴面补光；双瓣相位保留上方受光轮廓，也避免侧看时明暗过于剧烈。
    advanced = node(material, ue.MaterialExpressionVolumetricAdvancedMaterialOutput, 380, -340,
                    ray_march_volume_shadow=True, gray_scale_material=False,
                    multi_scattering_approximation_octave_count=1,
                    const_multi_scattering_contribution=0.92, const_multi_scattering_occlusion=0.28,
                    const_phase_g=-0.25, const_phase_g2=0.12, const_phase_blend=0.31)
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
    # 色阶只影响线性散射反照率和天光补光，消光维持无色，云下地貌仍按透射率呈现。
    sunlit = node(material, ue.MaterialExpressionVectorParameter, -140, 525,
                  parameter_name='CloudSunlitTint', default_value=ue.LinearColor(1.0, 0.98, 0.92, 1.0))
    interior = node(material, ue.MaterialExpressionVectorParameter, -140, 730,
                    parameter_name='CloudInteriorTint', default_value=ue.LinearColor(0.54, 0.72, 0.96, 1.0))
    band_lower, band_upper = 0.45, 0.59
    assert 0.0 <= band_lower < band_upper <= 1.0, '云光照软色阶上下界无效'
    lower, upper = [node(material, ue.MaterialExpressionScalarParameter, -160, y, parameter_name=name,
                         default_value=value) for y, name, value in
                    ((900, 'CloudBandLower', band_lower), (1050, 'CloudBandUpper', band_upper))]
    band = custom(material, 400, 300, '云顶与云腹的软色阶',
                  'return AerialCloudLitBand(Visibility, Height, Lower, Upper);',
                  ue.CustomMaterialOutputType.CMOT_FLOAT1,
                  dict(Visibility=(occlusion, ''), Height=(altitude, 'NormAltitudeInLayer'),
                       Lower=(lower, ''), Upper=(upper, '')))
    albedo = custom(material, 710, 585, '天蓝云腹与暖白云顶的散射色',
                    'return lerp(Interior.rgb, Sunlit.rgb, Band);',
                    ue.CustomMaterialOutputType.CMOT_FLOAT3,
                    dict(Sunlit=(sunlit, ''), Interior=(interior, ''), Band=(band, '')))
    boost = custom(material, 700, 340, '避免阴面天光降为灰黑', 'return lerp(0.72, 1.0, Band);',
                   ue.CustomMaterialOutputType.CMOT_FLOAT1, dict(Band=(band, '')))
    assert EDIT.connect_material_property(density, '', ue.MaterialProperty.MP_SUBSURFACE_COLOR), '无法接入体积消光'
    assert EDIT.connect_material_property(albedo, '', ue.MaterialProperty.MP_BASE_COLOR), '无法接入体积反照率'
    assert EDIT.connect_material_property(boost, '', ue.MaterialProperty.MP_AMBIENT_OCCLUSION), '无法接入云内天光遮蔽'

    errors = EDIT.recompile_material(material)
    assert not errors, f'{path} 编译失败：{errors}'
    save(material)
    ue.log(f'AERIAL_CLOUD_ASSET {material.get_path_name()}：云团、薄云与散射配色已制作。')
    instance_path = f'{folder}/MI_AerialCloudscape'
    if ASSETS.does_asset_exist(instance_path):
        instance = ASSETS.load_asset(instance_path)
    else:
        instance = TOOLS.create_asset('MI_AerialCloudscape', folder, ue.MaterialInstanceConstant,
                                      ue.MaterialInstanceConstantFactoryNew())
    assert isinstance(instance, ue.MaterialInstanceConstant), f'无法建立 {instance_path}'
    if instance.get_editor_property('parent') != material:
        EDIT.set_material_instance_parent(instance, material)
    save(instance)
    return instance


if __name__ == '__main__':
    # 编辑器脚本与运行世界分离，导入前显式扫描插件自管资产。
    source_dir = required_path_switch('AerialCloudTextureSource')
    ue.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], force_rescan=True)
    weather = import_linear_texture(source_dir / 'T_AerialCloudCoverage.png', repeat=True)
    source_atlas = import_linear_texture(source_dir / 'T_AerialCloudShapeAtlas.png', repeat=False)
    make_material(weather, make_volume(source_atlas))
