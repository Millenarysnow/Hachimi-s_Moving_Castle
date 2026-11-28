"""在渲染工作树生成晴空材质，并写入 MainMap 的固定白天布置。"""

import unreal as ue


MAP = '/Game/MovingCastle/Maps/MainMap'
SKY = '/Game/MovingCastle/Rendering/Sky'
HORIZON = ue.LinearColor(0.69, 0.81, 0.86, 1.0)
SUN_ROTATION = ue.Rotator(pitch=-50.0, yaw=207.0, roll=0.0)
ASSETS = ue.EditorAssetLibrary
EDIT = ue.MaterialEditingLibrary


def require(value, message):
    """制作过程中遇到缺失对象或失败接线，保留出错位置并终止。"""
    if not value:
        raise RuntimeError(message)
    return value


def get_or_create(path, kind, factory):
    """按固定包名重做项目自有材质，现有资产保持对象身份。"""
    if ASSETS.does_asset_exist(path):
        asset = ASSETS.load_asset(path)
    else:
        folder, name = path.rsplit('/', 1)
        asset = ue.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, kind, factory())
    require(isinstance(asset, kind), f'{path} 无法创建或加载为 {kind.__name__}')
    return asset


def make_sky():
    """按相机射线仰角定义画面，让天空颜色不随玩家在城堡周围移动。"""
    material = get_or_create(f'{SKY}/M_StylizedSkyDome', ue.Material, ue.MaterialFactoryNew)
    for expression in list(EDIT.get_material_expressions(material)):
        EDIT.delete_material_expression(material, expression)
    require(EDIT.get_num_material_expressions(material) == 0, f'{material.get_path_name()} 仍有旧节点')
    material.set_editor_properties(dict(shading_model=ue.MaterialShadingModel.MSM_UNLIT,
                                        two_sided=True, is_sky=True))
    camera = EDIT.create_material_expression(material, ue.MaterialExpressionCameraVectorWS, -740, -160)
    bindings = {'CameraVector': require(camera, '天空材质缺少相机方向')}
    for index, (name, value) in enumerate((
        ('HorizonColor', HORIZON),
        ('MidSkyColor', ue.LinearColor(0.22, 0.51, 0.83, 1.0)),
        ('ZenithColor', ue.LinearColor(0.08, 0.29, 0.66, 1.0)),
        ('SunColor', ue.LinearColor(1.48, 1.26, 0.99, 1.0)),
    )):
        parameter = require(EDIT.create_material_expression(material, ue.MaterialExpressionVectorParameter,
                                                            -740, index * 145), f'无法建立 {name}')
        parameter.set_editor_properties(dict(parameter_name=name, default_value=value))
        bindings[name] = parameter
    direction = require(EDIT.create_material_expression(material, ue.MaterialExpressionSkyAtmosphereLightDirection,
                                                        -740, 580), '无法读取大气零号太阳光方向')
    bindings['SunDirection'] = direction
    inputs = []
    for name in bindings:
        pin = ue.CustomInput()
        pin.set_editor_property('input_name', name)
        inputs.append(pin)
    shader = require(EDIT.create_material_expression(material, ue.MaterialExpressionCustom, -290, 0),
                     '无法建立天空材质色带节点')
    shader.set_editor_properties(dict(description='晴空渐变与固定日轮',
                                      output_type=ue.CustomMaterialOutputType.CMOT_FLOAT3,
                                      inputs=inputs,
                                      code='''
const float3 View = -normalize(CameraVector);
// 地平线下保持同一个浅青白，给远景地表与雾提供共同的融合端点。
const float3 Sky = lerp(lerp(HorizonColor, MidSkyColor, smoothstep(0.0, 0.48, View.z)),
                        ZenithColor, smoothstep(0.24, 0.98, View.z));
const float Sun = smoothstep(0.99956, 0.99967, dot(View, normalize(SunDirection)));
return lerp(Sky, SunColor, Sun);
'''))
    for name, source in bindings.items():
        require(EDIT.connect_material_expressions(source, '', shader, name), f'天空参数接线失败：{name}')
    require(EDIT.connect_material_property(shader, '', ue.MaterialProperty.MP_EMISSIVE_COLOR), '天空输出接线失败')
    errors = EDIT.recompile_material(material)
    require(not errors, f'{material.get_path_name()} 编译失败：{errors}')
    instance = get_or_create(f'{SKY}/MI_StylizedSkyDome', ue.MaterialInstanceConstant, ue.MaterialInstanceConstantFactoryNew)
    EDIT.set_material_instance_parent(instance, material)
    for asset in (material, instance):
        require(ASSETS.save_loaded_asset(asset, False), f'无法保存 {asset.get_path_name()}')
    return instance


def configure_postprocess(volume):
    """将曝光和轻柔的中性色调存进关卡体积，覆盖镜头默认效果。"""
    settings = volume.get_editor_property('settings')
    values = dict(auto_exposure_method=ue.AutoExposureMethod.AEM_MANUAL,
                  auto_exposure_apply_physical_camera_exposure=False, auto_exposure_bias=0.0,
                  local_exposure_highlight_contrast_scale=1.0, local_exposure_shadow_contrast_scale=1.0,
                  local_exposure_detail_strength=1.0, tone_curve_amount=0.8, film_slope=0.78,
                  film_toe=0.46, film_shoulder=0.31, expand_gamut=0.0, vignette_intensity=0.0,
                  depth_of_field_enabled=False, motion_blur_amount=0.0, scene_fringe_intensity=0.0,
                  lens_flare_intensity=0.0, bloom_intensity=0.23, bloom_threshold=2.0,
                  ambient_occlusion_intensity=0.27, ambient_occlusion_radius=42.0,
                  ambient_occlusion_radius_in_ws=True, ambient_occlusion_quality=50.0)
    for name, value in values.items():
        settings.set_editor_property(f'override_{name}', True)
        settings.set_editor_property(name, value)
    volume.set_editor_properties(dict(unbound=True, settings=settings))


def author_environment():
    """只写目标关卡，现有云层和大气高度随原关卡保留。"""
    actors = ue.get_editor_subsystem(ue.EditorActorSubsystem)
    levels = ue.get_editor_subsystem(ue.LevelEditorSubsystem)
    require(not levels.is_in_play_in_editor(), '需先退出 PIE 再编辑 MainMap')
    require(levels.load_level(MAP), f'无法打开 {MAP}')
    scene = actors.get_all_level_actors()
    weather = [actor for actor in scene if actor.get_class().get_name() == 'BP_StylizedWeather_fix_C']
    require(len(weather) <= 1, f'{MAP} 含多个天气蓝图')
    require(sum(isinstance(actor, ue.SkyAtmosphere) for actor in scene) == 1, f'{MAP} 必须有一个原生大气')
    require(sum(isinstance(actor, ue.VolumetricCloud) for actor in scene) == 1, f'{MAP} 必须有一个原生体积云')
    sky = make_sky()

    def place(kind, label, location=None, rotation=None):
        """重复执行时重用制作器已命名的 Actor，并拒绝重复的同类光源。"""
        same_type = [actor for actor in scene if isinstance(actor, kind)]
        matching = [actor for actor in same_type if actor.get_actor_label() == label]
        require(len(matching) <= 1 and (kind == ue.StaticMeshActor or len(same_type) == len(matching)),
                f'{MAP} 存在冲突的 {kind.__name__}，请先核对 {label}')
        position = location if location is not None else ue.Vector()
        orientation = rotation if rotation is not None else ue.Rotator()
        if matching:
            actor = matching[0]
            if kind == ue.StaticMeshActor:
                actor.static_mesh_component.set_mobility(ue.ComponentMobility.MOVABLE)
            actor.set_actor_location(position, False, False)
            actor.set_actor_rotation(orientation, False)
        else:
            actor = require(actors.spawn_actor_from_class(kind, position, orientation), f'无法放置 {label}')
        actor.set_actor_label(label)
        return actor

    sun = place(ue.DirectionalLight, 'Stylized Sun', rotation=SUN_ROTATION).light_component
    # 太阳朝向固定；Movable 是关闭烘焙照明时给建筑、Mass、VSM 和云提供实时光照的模式。
    sun.set_mobility(ue.ComponentMobility.MOVABLE)
    sun.set_editor_properties(dict(intensity=3.15, light_color=ue.Color(255, 249, 238, 255),
                                   light_source_angle=0.15, contact_shadow_length=0.0,
                                   atmosphere_sun_light=True, cast_cloud_shadows=False))
    sun.set_transmission(False)

    dome = place(ue.StaticMeshActor, 'Stylized Sky Dome')
    shell = dome.static_mesh_component
    shell.set_mobility(ue.ComponentMobility.MOVABLE)
    sphere = require(ASSETS.load_asset('/Engine/BasicShapes/Sphere'), '找不到引擎球体')
    if shell.get_editor_property('static_mesh') != sphere:
        require(shell.set_static_mesh(sphere), f'{dome.get_path_name()} 无法赋给天空网格')
    dome.set_actor_scale3d(ue.Vector(140000.0, 140000.0, 140000.0))
    shell.set_collision_enabled(ue.CollisionEnabled.NO_COLLISION)
    shell.set_editor_properties(dict(cast_shadow=False, can_ever_affect_navigation=False))
    shell.set_material(0, sky)
    shell.set_mobility(ue.ComponentMobility.STATIC)

    skylight = place(ue.SkyLight, 'Stylized Sky Light').light_component
    skylight.set_mobility(ue.ComponentMobility.MOVABLE)
    skylight.set_editor_properties(dict(intensity=0.64, light_color=ue.Color(224, 239, 255, 255),
                                       real_time_capture=False, lower_hemisphere_is_black=True,
                                       lower_hemisphere_color=ue.LinearColor(0.31, 0.43, 0.60, 1.0)))

    fog = place(ue.ExponentialHeightFog, 'Stylized Horizon Fog', ue.Vector(0, 0, -18000)).component
    fog.set_editor_properties(dict(fog_density=0.012, fog_height_falloff=0.30,
                                   fog_inscattering_luminance=HORIZON, start_distance=30000.0,
                                   fog_max_opacity=0.18, enable_volumetric_fog=False))
    configure_postprocess(place(ue.PostProcessVolume, 'Stylized Exposure'))
    for actor in weather:
        require(actors.destroy_actor(actor), f'无法删除 MainMap 旧天气 {actor.get_path_name()}')
    skylight.recapture_sky()
    require(levels.save_current_level(), f'无法保存 {MAP}')
    ue.log(f'STYLIZED_ENVIRONMENT {MAP} 已保存')


if __name__ == '__main__':
    author_environment()
