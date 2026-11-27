"""在 UE 编辑器中生成放置塔专用的半透明全息占位盒材质。"""
from pathlib import Path

import unreal


MATERIAL_FOLDER = "/Game/MovingCastle/FX/Placement"
MATERIAL_NAME = "M_CastlePlacementHoloBox"
MATERIAL_PATH = f"{MATERIAL_FOLDER}/{MATERIAL_NAME}"
CUBE_PATH = "/Engine/BasicShapes/Cube.Cube"
HLSL_PATH = Path(__file__).with_name("CastlePlacementHoloBox.hlsl")


def require(result, message):
    """任何资产创建、节点创建、接线或保存失败都立即中止生成。"""
    if not result:
        raise RuntimeError(message)
    return result


def verify_cube_face_uvs():
    """把 LOD0 三角形按几何面合并，检查边角与整面 UV 覆盖。"""
    cube = require(unreal.load_asset(CUBE_PATH), f"无法加载用于 UV 核验的网格 {CUBE_PATH}")
    description = require(cube.get_static_mesh_description(0), f"无法读取 {CUBE_PATH} 的 LOD0 网格")
    tolerance = 0.0001
    face_uvs = {(axis, side): [] for axis in range(3) for side in (-1, 1)}
    for index in range(description.get_polygon_count()):
        polygon_id = unreal.PolygonID(index)
        instances = description.get_polygon_vertex_instances(polygon_id)
        positions = [description.get_vertex_position(description.get_vertex_instance_vertex(instance))
                     for instance in instances]
        coordinates = [(position.x, position.y, position.z) for position in positions]
        planes = [(axis, side) for axis, side in face_uvs
                  if all(abs(vertex[axis] - side * 50.0) < tolerance for vertex in coordinates)]
        require(len(planes) == 1, f"{CUBE_PATH} 的多边形 {index} 不在单位盒的单一几何面上")
        axis, side = planes[0]
        in_plane_axes = [in_plane_axis for in_plane_axis in range(3) if in_plane_axis != axis]
        for instance, vertex in zip(instances, coordinates):
            uv = description.get_vertex_instance_uv(instance, 0)
            face_position = tuple((vertex[in_plane_axis] + 50.0) / 100.0 for in_plane_axis in in_plane_axes)
            face_uvs[axis, side].append((face_position, (uv.x, uv.y)))

    for (axis, side), samples in face_uvs.items():
        # 同一个面只允许一套轴交换或翻转关系，确保三角形之间连续且 UV 边界贴合盒体边界。
        require(samples, f"{CUBE_PATH} 缺少几何面 Axis={axis} Side={side}")
        positions, uvs = zip(*samples)
        require(all(abs(min(channel)) < tolerance and abs(max(channel) - 1.0) < tolerance
                    for channel in zip(*positions)), f"{CUBE_PATH} 的几何面 Axis={axis} Side={side} 尺寸错误")
        require(any(
            all(abs(uv[0] - (1.0 - position[u_axis] if flip_u else position[u_axis])) < tolerance
                and abs(uv[1] - (1.0 - position[1 - u_axis] if flip_v else position[1 - u_axis])) < tolerance
                for position, uv in samples)
            for u_axis in (0, 1) for flip_u in (False, True) for flip_v in (False, True)),
            f"{CUBE_PATH} 的几何面 Axis={axis} Side={side} 不具备独立的 0–1 UV 方形")
        unreal.log(f"CASTLE_BASIC_CUBE_UV_PER_FACE_01 Axis={axis} Side={side} "
                   f"U=[{min(uv[0] for uv in uvs):g}, {max(uv[0] for uv in uvs):g}] "
                   f"V=[{min(uv[1] for uv in uvs):g}, {max(uv[1] for uv in uvs):g}]")


def create_holo_box_material():
    """重建唯一拥有的节点图并保存到正式运行时资产路径。"""
    editing = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    require(HLSL_PATH.is_file(), f"找不到全息占位盒 HLSL 源码 {HLSL_PATH}")
    hlsl = HLSL_PATH.read_text(encoding="utf-8-sig")
    verify_cube_face_uvs()
    if not assets.does_directory_exist(MATERIAL_FOLDER):
        require(assets.make_directory(MATERIAL_FOLDER), f"无法创建资源目录 {MATERIAL_FOLDER}")

    asset_exists = assets.does_asset_exist(MATERIAL_PATH)
    if asset_exists:
        material = require(assets.load_asset(MATERIAL_PATH), f"无法加载全息占位盒材质 {MATERIAL_PATH}")
        require(isinstance(material, unreal.Material), f"{MATERIAL_PATH} 不是 Material 资产")
    else:
        material = require(
            unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                MATERIAL_NAME, MATERIAL_FOLDER, unreal.Material, unreal.MaterialFactoryNew()),
            f"无法创建全息占位盒材质 {MATERIAL_PATH}")

    # 已加载的节点可能被 UE 标为 Root，编辑器库批量删除会触发断言。按类型和参数名复用节点。
    expressions = list(editing.get_material_expressions(material))

    def node(kind, x, y, parameter_name=None, material_output=None):
        """新资产创建节点，重复生成时按节点职责更新已有节点和接线。"""
        if not asset_exists:
            return require(editing.create_material_expression(material, kind, x, y), f"创建材质节点失败：{kind}")
        if material_output is not None:
            expression = editing.get_material_property_input_node(material, material_output)
            matches = [expression] if expression in expressions and isinstance(expression, kind) else []
        else:
            matches = [expression for expression in expressions if isinstance(expression, kind)
                       and (parameter_name is None
                            or str(expression.get_editor_property("parameter_name")) == parameter_name)]
        require(len(matches) == 1, f"{MATERIAL_PATH} 无法唯一定位材质节点 {kind.__name__} "
                                   f"{parameter_name or material_output or ''}")
        expressions.remove(matches[0])
        return matches[0]

    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)

    uv = node(unreal.MaterialExpressionTextureCoordinate, -900, -180)
    # 将网格的局部顶点法线传给像素阶段。双面渲染时仍指向真实盒面的外侧。
    vertex_normal = node(unreal.MaterialExpressionPreSkinnedNormal, -900, 20)
    local_normal = node(unreal.MaterialExpressionVertexInterpolator, -650, 20)
    require(editing.connect_material_expressions(vertex_normal, "", local_normal, ""),
            "局部顶点法线插值接线失败")

    holo_color = node(unreal.MaterialExpressionVectorParameter, -900, 220, parameter_name="HoloColor")
    holo_color.set_editor_property("parameter_name", "HoloColor")
    holo_color.set_editor_property("default_value", unreal.LinearColor(0.16, 0.88, 1.0, 1.0))

    support_normal = node(unreal.MaterialExpressionVectorParameter, -900, 390, parameter_name="SupportNormal")
    support_normal.set_editor_property("parameter_name", "SupportNormal")
    support_normal.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))

    invalid_mode = node(unreal.MaterialExpressionScalarParameter, -900, 550, parameter_name="InvalidMode")
    invalid_mode.set_editor_property("parameter_name", "InvalidMode")
    invalid_mode.set_editor_property("default_value", 0.0)

    fill_opacity = node(unreal.MaterialExpressionScalarParameter, -900, 700, parameter_name="FaceFillOpacity")
    fill_opacity.set_editor_property("parameter_name", "FaceFillOpacity")
    fill_opacity.set_editor_property("default_value", 0.09)

    edge_width = node(unreal.MaterialExpressionScalarParameter, -900, 850, parameter_name="EdgeWidth")
    edge_width.set_editor_property("parameter_name", "EdgeWidth")
    edge_width.set_editor_property("default_value", 0.035)

    corner_length = node(unreal.MaterialExpressionScalarParameter, -900, 1000, parameter_name="CornerLength")
    corner_length.set_editor_property("parameter_name", "CornerLength")
    corner_length.set_editor_property("default_value", 0.20)

    time = node(unreal.MaterialExpressionTime, -900, 1150)
    custom = node(unreal.MaterialExpressionCustom, -380, 300)
    custom.set_editor_property("description", "12 条棱线、8 角标、透明面、呼吸扫描与不可放斜纹")
    custom.set_editor_property("code", hlsl)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)

    bindings = (
        ("UV", uv, ""),
        ("LocalNormal", local_normal, ""),
        ("SupportNormal", support_normal, ""),
        ("HoloColor", holo_color, ""),
        ("InvalidMode", invalid_mode, ""),
        ("FaceFillOpacity", fill_opacity, ""),
        ("EdgeWidth", edge_width, ""),
        ("CornerLength", corner_length, ""),
        ("Time", time, ""),
    )
    custom_inputs = []
    for input_name, _, _ in bindings:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", input_name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)
    for input_name, expression, output_name in bindings:
        require(editing.connect_material_expressions(expression, output_name, custom, input_name),
                f"全息材质输入接线失败：{input_name}")

    emissive_mask = node(unreal.MaterialExpressionComponentMask, 80, 230,
                         material_output=unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    emissive_mask.set_editor_property("r", True)
    emissive_mask.set_editor_property("g", True)
    emissive_mask.set_editor_property("b", True)
    emissive_mask.set_editor_property("a", False)
    require(editing.connect_material_expressions(custom, "", emissive_mask, ""), "发光颜色遮罩接线失败")
    require(editing.connect_material_property(
        emissive_mask, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR), "发光颜色输出接线失败")

    opacity_mask = node(unreal.MaterialExpressionComponentMask, 80, 420,
                        material_output=unreal.MaterialProperty.MP_OPACITY)
    opacity_mask.set_editor_property("r", False)
    opacity_mask.set_editor_property("g", False)
    opacity_mask.set_editor_property("b", False)
    opacity_mask.set_editor_property("a", True)
    require(editing.connect_material_expressions(custom, "", opacity_mask, ""), "透明度遮罩接线失败")
    require(editing.connect_material_property(
        opacity_mask, "", unreal.MaterialProperty.MP_OPACITY), "透明度输出接线失败")

    require(not expressions, f"{MATERIAL_PATH} 含有生成器未声明的节点：{[item.get_name() for item in expressions]}")
    editing.recompile_material(material)
    # 确认运行时代码依赖的参数已成为材质参数节点，而不是仅写在 HLSL 源码中。
    vector_parameter_names = {str(name) for name in editing.get_vector_parameter_names(material)}
    scalar_parameter_names = {str(name) for name in editing.get_scalar_parameter_names(material)}
    require("HoloColor" in vector_parameter_names and "SupportNormal" in vector_parameter_names,
            f"材质 {MATERIAL_PATH} 缺少向量参数 HoloColor 或 SupportNormal")
    require("InvalidMode" in scalar_parameter_names,
            f"材质 {MATERIAL_PATH} 缺少标量参数 InvalidMode")
    require(assets.save_loaded_asset(material, False), f"无法保存全息占位盒材质 {MATERIAL_PATH}")
    unreal.log(f"CASTLE_PLACEMENT_HOLOBOX_SAVED {MATERIAL_PATH}")


create_holo_box_material()
