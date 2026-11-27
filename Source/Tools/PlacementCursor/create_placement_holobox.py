"""在 UE 编辑器中生成放置塔专用的半透明全息占位盒材质。"""
from pathlib import Path

import unreal


MATERIAL_FOLDER = "/Game/MovingCastle/FX/Placement"
MATERIAL_NAME = "M_CastlePlacementHoloBox"
MATERIAL_PATH = f"{MATERIAL_FOLDER}/{MATERIAL_NAME}"
HLSL_PATH = Path(__file__).with_name("CastlePlacementHoloBox.hlsl")


def require(result, message):
    """任何资产创建、节点创建、接线或保存失败都立即中止生成。"""
    if not result:
        raise RuntimeError(message)
    return result


def create_holo_box_material():
    """重建唯一拥有的节点图并保存到正式运行时资产路径。"""
    editing = unreal.MaterialEditingLibrary
    assets = unreal.EditorAssetLibrary
    require(HLSL_PATH.is_file(), f"找不到全息占位盒 HLSL 源码 {HLSL_PATH}")
    hlsl = HLSL_PATH.read_text(encoding="utf-8-sig")
    if not assets.does_directory_exist(MATERIAL_FOLDER):
        require(assets.make_directory(MATERIAL_FOLDER), f"无法创建资源目录 {MATERIAL_FOLDER}")

    # 记录引擎立方体的逐面 UV 分布，报告实际结果并保持其资产只读。
    cube_path = "/Engine/BasicShapes/Cube.Cube"
    cube_mesh = require(assets.load_asset(cube_path), f"无法加载用于 UV 核验的网格 {cube_path}")
    require(isinstance(cube_mesh, unreal.StaticMesh), f"{cube_path} 不是 StaticMesh")
    mesh_description = require(cube_mesh.get_static_mesh_description(0), f"无法读取 {cube_path} 的 LOD0 描述")
    cube_uvs_are_per_face = True
    face_uv_summaries = []
    for polygon_id in range(mesh_description.get_polygon_count()):
        polygon_uvs = [mesh_description.get_vertex_instance_uv(instance_id, 0)
                       for instance_id in mesh_description.get_polygon_vertex_instances(polygon_id)]
        if len(polygon_uvs) != 4:
            cube_uvs_are_per_face = False
            break
        minimum_u = min(uv.x for uv in polygon_uvs)
        maximum_u = max(uv.x for uv in polygon_uvs)
        minimum_v = min(uv.y for uv in polygon_uvs)
        maximum_v = max(uv.y for uv in polygon_uvs)
        b_is_unit_square = (
            abs(minimum_u) <= 0.0001 and abs(minimum_v) <= 0.0001
            and abs(maximum_u - 1.0) <= 0.0001 and abs(maximum_v - 1.0) <= 0.0001)
        face_uv_summaries.append((polygon_id, minimum_u, minimum_v, maximum_u, maximum_v))
        cube_uvs_are_per_face = cube_uvs_are_per_face and b_is_unit_square

    if len(face_uv_summaries) != 6:
        cube_uvs_are_per_face = False

    # 本生成工具使用项目自有 100 cm 立方体，明确为六面写入 0-1 UV 并隔离引擎资源变化。
    mesh_name = "SM_CastlePlacementHoloBox"
    mesh_path = f"{MATERIAL_FOLDER}/{mesh_name}"
    if assets.does_asset_exist(mesh_path):
        mesh_asset = require(assets.load_asset(mesh_path), f"无法加载专属占位网格 {mesh_path}")
        require(isinstance(mesh_asset, unreal.StaticMesh), f"{mesh_path} 不是 StaticMesh")
    else:
        mesh_asset = require(
            unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                mesh_name, MATERIAL_FOLDER, unreal.StaticMesh, unreal.StaticMeshFactoryNew()),
            f"无法创建专属占位网格 {mesh_path}")

    # 每次重新烘焙 100 cm 六面网格，避免脚本重跑时残留旧 UV 或拓扑。
    mesh_description = unreal.StaticMesh.create_static_mesh_description(mesh_asset)
    material_group = mesh_description.create_polygon_group()
    mesh_description.set_polygon_group_material_slot_name(material_group, "PlacementHoloBox")
    polygon_ids = mesh_description.create_cube(
        unreal.Vector(0.0, 0.0, 0.0), unreal.Vector(50.0, 50.0, 50.0), material_group)
    for polygon_id in polygon_ids:
        vertex_instance_ids = mesh_description.get_polygon_vertex_instances(polygon_id)
        face_positions = [
            mesh_description.get_vertex_position(mesh_description.get_vertex_instance_vertex(instance_id))
            for instance_id in vertex_instance_ids]
        # 选取面中心绝对值最大的轴为深度轴，其余两轴线性映射到该面的 0-1 方形。
        face_center = unreal.Vector(
            sum(position.x for position in face_positions) / len(face_positions),
            sum(position.y for position in face_positions) / len(face_positions),
            sum(position.z for position in face_positions) / len(face_positions))
        depth_axis = max(range(3), key=lambda axis: abs((face_center.x, face_center.y, face_center.z)[axis]))
        uv_axes = [axis for axis in range(3) if axis != depth_axis]
        for instance_id, position in zip(vertex_instance_ids, face_positions):
            coordinates = (position.x, position.y, position.z)
            uv = unreal.Vector2D((coordinates[uv_axes[0]] + 50.0) / 100.0,
                                 (coordinates[uv_axes[1]] + 50.0) / 100.0)
            mesh_description.set_vertex_instance_uv(instance_id, uv, 0)
    mesh_asset.build_from_static_mesh_descriptions([mesh_description], False, True)
    require(assets.save_loaded_asset(mesh_asset, False), f"无法保存专属占位网格 {mesh_path}")

    # 保存后重新读取网格描述，逐面证明材质依赖的 UV 范围没有在构建过程中变化。
    built_mesh_description = require(
        mesh_asset.get_static_mesh_description(0), f"无法复查专属占位网格 {mesh_path} 的 LOD0")
    require(built_mesh_description.get_polygon_count() == 6,
            f"专属占位网格 {mesh_path} 必须包含六个独立面")
    for polygon_id in range(built_mesh_description.get_polygon_count()):
        polygon_uvs = [built_mesh_description.get_vertex_instance_uv(instance_id, 0)
                       for instance_id in built_mesh_description.get_polygon_vertex_instances(polygon_id)]
        require(len(polygon_uvs) == 4, f"专属占位网格 {mesh_path} 的面 {polygon_id} 不是四边形")
        require(
            abs(min(uv.x for uv in polygon_uvs)) <= 0.0001
            and abs(min(uv.y for uv in polygon_uvs)) <= 0.0001
            and abs(max(uv.x for uv in polygon_uvs) - 1.0) <= 0.0001
            and abs(max(uv.y for uv in polygon_uvs) - 1.0) <= 0.0001,
            f"专属占位网格 {mesh_path} 的面 {polygon_id} UV 未完整覆盖 0-1 方形")

    if assets.does_asset_exist(MATERIAL_PATH):
        material = require(assets.load_asset(MATERIAL_PATH), f"无法加载全息占位盒材质 {MATERIAL_PATH}")
        require(isinstance(material, unreal.Material), f"{MATERIAL_PATH} 不是 Material 资产")
        editing.delete_all_material_expressions(material)
    else:
        material = require(
            unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                MATERIAL_NAME, MATERIAL_FOLDER, unreal.Material, unreal.MaterialFactoryNew()),
            f"无法创建全息占位盒材质 {MATERIAL_PATH}")

    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)

    def node(kind, x, y):
        """创建一个属于目标材质的节点，失败时保留明确的节点类型信息。"""
        return require(editing.create_material_expression(material, kind, x, y), f"创建材质节点失败：{kind}")

    uv = node(unreal.MaterialExpressionTextureCoordinate, -900, -180)
    world_normal = node(unreal.MaterialExpressionPixelNormalWS, -900, 20)
    normal_to_local = node(unreal.MaterialExpressionTransform, -650, 20)
    normal_to_local.set_editor_property(
        "transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    normal_to_local.set_editor_property(
        "transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    require(editing.connect_material_expressions(world_normal, "", normal_to_local, "Input"),
            "世界法线到局部法线的变换接线失败")

    holo_color = node(unreal.MaterialExpressionVectorParameter, -900, 220)
    holo_color.set_editor_property("parameter_name", "HoloColor")
    holo_color.set_editor_property("default_value", unreal.LinearColor(0.16, 0.88, 1.0, 1.0))

    support_normal = node(unreal.MaterialExpressionVectorParameter, -900, 390)
    support_normal.set_editor_property("parameter_name", "SupportNormal")
    support_normal.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))

    invalid_mode = node(unreal.MaterialExpressionScalarParameter, -900, 550)
    invalid_mode.set_editor_property("parameter_name", "InvalidMode")
    invalid_mode.set_editor_property("default_value", 0.0)

    fill_opacity = node(unreal.MaterialExpressionScalarParameter, -900, 700)
    fill_opacity.set_editor_property("parameter_name", "FaceFillOpacity")
    fill_opacity.set_editor_property("default_value", 0.09)

    edge_width = node(unreal.MaterialExpressionScalarParameter, -900, 850)
    edge_width.set_editor_property("parameter_name", "EdgeWidth")
    edge_width.set_editor_property("default_value", 0.035)

    corner_length = node(unreal.MaterialExpressionScalarParameter, -900, 1000)
    corner_length.set_editor_property("parameter_name", "CornerLength")
    corner_length.set_editor_property("default_value", 0.20)

    time = node(unreal.MaterialExpressionTime, -900, 1150)
    custom = node(unreal.MaterialExpressionCustom, -380, 300)
    custom.set_editor_property("description", "12 条棱线、8 角标、透明面、呼吸扫描与不可放斜纹")
    custom.set_editor_property("code", hlsl)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)

    bindings = (
        ("UV", uv, ""),
        ("LocalNormal", normal_to_local, ""),
        ("SupportNormal", support_normal, ""),
        ("HoloColor", holo_color, "RGBA"),
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

    emissive_mask = node(unreal.MaterialExpressionComponentMask, 80, 230)
    emissive_mask.set_editor_property("r", True)
    emissive_mask.set_editor_property("g", True)
    emissive_mask.set_editor_property("b", True)
    emissive_mask.set_editor_property("a", False)
    require(editing.connect_material_expressions(custom, "", emissive_mask, ""), "发光颜色遮罩接线失败")
    require(editing.connect_material_property(
        emissive_mask, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR), "发光颜色输出接线失败")

    opacity_mask = node(unreal.MaterialExpressionComponentMask, 80, 420)
    opacity_mask.set_editor_property("r", False)
    opacity_mask.set_editor_property("g", False)
    opacity_mask.set_editor_property("b", False)
    opacity_mask.set_editor_property("a", True)
    require(editing.connect_material_expressions(custom, "", opacity_mask, ""), "透明度遮罩接线失败")
    require(editing.connect_material_property(
        opacity_mask, "", unreal.MaterialProperty.MP_OPACITY), "透明度输出接线失败")

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
    unreal.log(f"CASTLE_PLACEMENT_HOLOBOX_MESH_SAVED {mesh_path}")
    unreal.log(f"CASTLE_BASIC_CUBE_UV_PER_FACE_01 {cube_uvs_are_per_face} {face_uv_summaries}")


create_holo_box_material()
