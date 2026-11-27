"""从空岛原始 FBX 中可重复拆出离开门与指定机台的独立静态网格。"""

from math import isfinite
from pathlib import Path

import unreal


PROJECT_ROOT = Path(__file__).resolve().parents[3]
SOURCE_FOLDER = "/Game/MovingCastle/Model/SkyIslands"
OUTPUT_FOLDER = SOURCE_FOLDER

# 拆分选择依据组件包围盒中心与实测最大尺度，不以材质名或文件名猜测几何归属。
# 原网格可从 RawContent FBX 重导入，因此每次运行均从同一份完整几何重新开始。
MESHES = (
    {
        "name": "SM_SkyIsland_Rest",
        "triangles": 23454,
        "targets": (
            {
                "name": "SM_SkyIsland_Departure_Rest",
                "center_min": (620.0, -210.0, 0.0),
                "center_max": (770.0, 210.0, 550.0),
                "max_dimension": 260.0,
                "anchor": (700.0, 0.0, 200.0),
                "expected_components": 26,
                "expected_triangles": 1568,
            },
        ),
    },
    {
        "name": "SM_SkyIsland_Shop",
        "triangles": 30902,
        "targets": (
            {
                "name": "SM_SkyIsland_Departure_Shop",
                "center_min": (620.0, -210.0, 0.0),
                "center_max": (770.0, 210.0, 550.0),
                "max_dimension": 260.0,
                "anchor": (700.0, 0.0, 200.0),
                "expected_components": 26,
                "expected_triangles": 1568,
            },
        ),
    },
    {
        "name": "SM_SkyIsland_Gamble",
        "triangles": 26166,
        "targets": (
            {
                "name": "SM_SkyIsland_Departure_Gamble",
                "center_min": (620.0, -210.0, 0.0),
                "center_max": (770.0, 210.0, 550.0),
                "max_dimension": 260.0,
                "anchor": (700.0, 0.0, 200.0),
                "expected_components": 26,
                "expected_triangles": 1568,
            },
            {
                "name": "SM_Gamble_Roulette",
                "center_min": (-250.0, -720.0, -20.0),
                "center_max": (80.0, -370.0, 240.0),
                "max_dimension": 380.0,
                "anchor": (-80.0, -550.0, 66.0),
                "expected_components": 9,
                "expected_triangles": 1196,
            },
            {
                "name": "SM_Gamble_SlotMachine",
                "center_min": (-130.0, -130.0, 0.0),
                "center_max": (100.0, 150.0, 260.0),
                "max_dimension": 320.0,
                "anchor": (-10.0, 10.0, 114.0),
                "expected_components": 34,
                "expected_triangles": 2116,
            },
            {
                "name": "SM_Gamble_BalloonRecycler",
                "center_min": (-170.0, 400.0, 0.0),
                "center_max": (100.0, 660.0, 320.0),
                "max_dimension": 320.0,
                "anchor": (-40.0, 530.0, 116.0),
                "expected_components": 15,
                "expected_triangles": 1404,
            },
        ),
    },
    {
        "name": "SM_SkyIsland_PinballNPC",
        "triangles": 18270,
        "targets": (
            {
                "name": "SM_SkyIsland_Departure_PinballNPC",
                "center_min": (620.0, -210.0, 0.0),
                "center_max": (770.0, 210.0, 550.0),
                "max_dimension": 260.0,
                "anchor": (700.0, 0.0, 200.0),
                "expected_components": 26,
                "expected_triangles": 1568,
            },
        ),
    },
    {
        "name": "SM_Pinball_Board",
        "triangles": 1688,
        "targets": (
            {
                "name": "SM_Pinball_Console",
                "center_min": (500.0, -120.0, -80.0),
                "center_max": (660.0, 120.0, 240.0),
                "max_dimension": 300.0,
                "anchor": (585.0, 0.0, 39.0),
                "expected_components": 14,
                "expected_triangles": 616,
            },
        ),
    },
)


def require(value, message):
    """资产或网格数据不符合已查证事实时立即中止，不生成部分结果。"""
    if not value:
        unreal.log_error(message)
        raise RuntimeError(message)
    return value


def find_root(parents, triangle_index):
    """查找并压缩共享焊接顶点的三角形并查集根节点。"""
    root = triangle_index
    while parents[root] != root:
        root = parents[root]
    while parents[triangle_index] != triangle_index:
        next_index = parents[triangle_index]
        parents[triangle_index] = root
        triangle_index = next_index
    return root


def analyze_connected_components(description, material_slot_names):
    """按真正共享的 VertexID 还原互不焊接的三角形分量及材质槽分布。"""
    triangle_count = description.get_triangle_count()
    parents = list(range(triangle_count))
    first_triangle_for_vertex = {}
    triangle_vertices = []
    triangle_polygon_ids = []
    triangle_material_groups = []
    vertex_objects = {}

    for triangle_index in range(triangle_count):
        triangle_id = unreal.TriangleID(triangle_index)
        vertex_ids = []
        for vertex_instance_id in description.get_triangle_vertex_instances(triangle_id):
            vertex_id = description.get_vertex_instance_vertex(vertex_instance_id)
            value = vertex_id.get_editor_property("id_value")
            require(description.is_vertex_valid(vertex_id),
                    f"三角形 {triangle_index} 引用了无效顶点 {value}。")
            vertex_ids.append(value)
            vertex_objects[value] = vertex_id
            previous_triangle = first_triangle_for_vertex.get(value)
            if previous_triangle is None:
                first_triangle_for_vertex[value] = triangle_index
            else:
                first_root = find_root(parents, triangle_index)
                second_root = find_root(parents, previous_triangle)
                if first_root != second_root:
                    parents[second_root] = first_root

        triangle_vertices.append(vertex_ids)
        polygon_id = description.get_triangle_polygon(triangle_id)
        triangle_polygon_ids.append(polygon_id.get_editor_property("id_value"))
        group_id = description.get_triangle_polygon_group(triangle_id)
        triangle_material_groups.append(group_id.get_editor_property("id_value"))

    components = {}
    for triangle_index in range(triangle_count):
        root = find_root(parents, triangle_index)
        component = components.setdefault(root, {"triangles": [], "vertices": set(), "polygons": set(), "slots": {}})
        component["triangles"].append(triangle_index)
        component["vertices"].update(triangle_vertices[triangle_index])
        component["polygons"].add(triangle_polygon_ids[triangle_index])
        group_id = triangle_material_groups[triangle_index]
        component["slots"][group_id] = component["slots"].get(group_id, 0) + 1

    for component in components.values():
        positions = [description.get_vertex_position(vertex_objects[index]) for index in component["vertices"]]
        position_values = [(position.x, position.y, position.z) for position in positions]
        component["minimum"] = tuple(min(position[axis] for position in position_values) for axis in range(3))
        component["maximum"] = tuple(max(position[axis] for position in position_values) for axis in range(3))
        component["center"] = tuple(
            (component["minimum"][axis] + component["maximum"][axis]) * 0.5 for axis in range(3)
        )
        component["dimensions"] = tuple(
            component["maximum"][axis] - component["minimum"][axis] for axis in range(3)
        )
        component["slot_names"] = {
            material_slot_names[index] if 0 <= index < len(material_slot_names) else "<无对应材质槽>": count
            for index, count in component["slots"].items()
        }
    return components, triangle_polygon_ids, vertex_objects


def select_target_components(components, target_spec, mesh_name):
    """根据查证后的局部范围选出目标独立分量，并校验已见过的准确数量。"""
    selected_roots = set()
    center_minimum = target_spec["center_min"]
    center_maximum = target_spec["center_max"]
    for root, component in components.items():
        is_inside_center_range = all(
            center_minimum[axis] <= component["center"][axis] <= center_maximum[axis]
            for axis in range(3)
        )
        is_within_size = max(component["dimensions"]) <= target_spec["max_dimension"]
        if is_inside_center_range and is_within_size:
            selected_roots.add(root)

    selected_triangles = {
        triangle
        for root in selected_roots
        for triangle in components[root]["triangles"]
    }
    selected_polygons = {
        polygon
        for root in selected_roots
        for polygon in components[root]["polygons"]
    }
    require(len(selected_roots) == target_spec["expected_components"],
            f"网格 {mesh_name} 目标 {target_spec['name']} 分量数 {len(selected_roots)}，"
            f"预期 {target_spec['expected_components']}。")
    require(len(selected_triangles) == target_spec["expected_triangles"],
            f"网格 {mesh_name} 目标 {target_spec['name']} 三角形数 {len(selected_triangles)}，"
            f"预期 {target_spec['expected_triangles']}。")
    require(selected_roots, f"网格 {mesh_name} 未找到目标 {target_spec['name']} 的独立连通分量。")
    return selected_roots, selected_triangles, selected_polygons


def rebuild_mesh(mesh, description, asset_path):
    """完整编辑器构建后校验真实边界，错误资产在保存前报出。"""
    mesh.build_from_static_mesh_descriptions([description], False, False)
    bounds = mesh.get_bounds()
    origin = (bounds.origin.x, bounds.origin.y, bounds.origin.z)
    extent = (bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z)
    require(all(isfinite(value) for value in origin + extent)
            and all(value > 0.0 for value in extent),
            f"网格 {asset_path} 完整构建后的包围盒无效，中心 {origin}，半尺寸 {extent}。")


def set_target_collision_box(mesh, asset_path):
    """用独立网格本地包围盒替换复制而来的整岛碰撞体。"""
    body_setup = mesh.get_editor_property("body_setup")
    aggregate = body_setup.get_editor_property("agg_geom")
    bounds = mesh.get_bounds()
    collision_box = unreal.KBoxElem()
    collision_box.set_editor_properties({
        "center": bounds.origin,
        "x": bounds.box_extent.x * 2.0,
        "y": bounds.box_extent.y * 2.0,
        "z": bounds.box_extent.z * 2.0,
    })
    aggregate.set_editor_property("box_elems", [collision_box])
    aggregate.set_editor_property("sphere_elems", [])
    aggregate.set_editor_property("sphyl_elems", [])
    aggregate.set_editor_property("convex_elems", [])
    body_setup.set_editor_property("agg_geom", aggregate)
    mesh.set_editor_property("body_setup", body_setup)

    mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    require(mesh_editor.get_simple_collision_count(mesh) > 0,
            f"拆分资产 {asset_path} 未生成可见性射线碰撞。")


def make_target_mesh(source_path, target_path, target_spec, selected_polygons, vertex_objects):
    """复制来源网格后仅保留目标多边形，平移到组件原点并保留来源材质槽。"""
    assets = unreal.EditorAssetLibrary
    staging_path = f"{target_path}_BuildTemp"
    if assets.does_asset_exist(staging_path):
        require(assets.delete_asset(staging_path), f"无法移除旧的拆分暂存网格 {staging_path}。")

    # 独立暂存包先完成构建和碰撞验证，再安装到正式路径。
    target_mesh = assets.duplicate_asset(source_path, staging_path)
    require(isinstance(target_mesh, unreal.StaticMesh), f"无法创建拆分暂存网格 {staging_path}。")
    description = target_mesh.get_static_mesh_description(0)
    for polygon_index in range(description.get_polygon_count()):
        if polygon_index not in selected_polygons:
            description.delete_polygon(unreal.PolygonID(polygon_index))

    anchor = target_spec["anchor"]
    for vertex_id in vertex_objects.values():
        # 仅修改被保留多边形使用的顶点，保持原 FBX 坐标与新组件变换严格重合。
        if not description.is_vertex_valid(vertex_id) or description.is_vertex_orphaned(vertex_id):
            continue
        position = description.get_vertex_position(vertex_id)
        description.set_vertex_position(vertex_id, unreal.Vector(
            position.x - anchor[0], position.y - anchor[1], position.z - anchor[2]))

    require(description.get_triangle_count() == target_spec["expected_triangles"],
            f"拆分资产 {staging_path} 构建前三角形数量不匹配。")
    # 完整构建保存有限的渲染 Bounds，并为新几何重建距离场，避免快速构建态写入正式资产。
    rebuild_mesh(target_mesh, description, staging_path)

    # 复制资产会继承原岛体的大碰撞盒，独立组件只保留自身包围盒范围。
    set_target_collision_box(target_mesh, staging_path)
    require(assets.save_loaded_asset(target_mesh, False), f"无法保存拆分网格 {staging_path}。")

    if assets.does_asset_exist(target_path):
        require(assets.delete_asset(target_path), f"无法移除旧的拆分网格 {target_path}。")
    require(assets.rename_asset(staging_path, target_path),
            f"无法将拆分暂存网格 {staging_path} 安装到正式路径 {target_path}。")
    target_mesh = assets.load_asset(target_path)
    require(isinstance(target_mesh, unreal.StaticMesh), f"重命名后的网格 {target_path} 无法加载。")
    require(assets.save_loaded_asset(target_mesh, False), f"无法保存正式拆分网格 {target_path}。")

    bounds = target_mesh.get_bounds()
    material_usage = {}
    target_vertex_count = 0
    for root, component in target_spec["components"].items():
        target_vertex_count += len(component["vertices"])
        for slot_name, count in component["slot_names"].items():
            material_usage[slot_name] = material_usage.get(slot_name, 0) + count
    unreal.log(
        f"CASTLE_SKY_SPLIT_SAVED path={target_path} connected_vertices={target_vertex_count} "
        f"triangles={description.get_triangle_count()} materials={material_usage} "
        f"bounds_origin={bounds.origin} bounds_extent={bounds.box_extent}"
    )


def split_sky_island_meshes():
    """先完整查证全部来源，再生成目标网格并从原合并网格移除重复几何。"""
    source_meshes = {}
    source_states = []
    assets = unreal.EditorAssetLibrary

    # 只接受可重导入的五个原始 FBX 资产，并识别全量源网格或本工具已拆分状态。
    for mesh_spec in MESHES:
        mesh_name = mesh_spec["name"]
        source_path = f"{SOURCE_FOLDER}/{mesh_name}"
        source_file = PROJECT_ROOT / "RawContent" / "SkyIslands" / f"{mesh_name}.fbx"
        require(source_file.is_file(), f"空岛网格来源文件 {source_file} 不存在。")
        source_mesh = assets.load_asset(source_path)
        require(isinstance(source_mesh, unreal.StaticMesh), f"来源网格 {source_path} 缺失或不是 StaticMesh。")
        import_data = source_mesh.get_editor_property("asset_import_data")
        require(import_data, f"来源网格 {source_path} 缺少导入来源元数据。")
        actual_source = import_data.get_first_filename().replace("\\", "/").lower()
        expected_source = f"RawContent/SkyIslands/{mesh_name}.fbx".lower()
        require(actual_source.endswith(expected_source),
                f"来源网格 {source_path} 的 FBX 来源是 {actual_source}，预期 {expected_source}。")

        triangle_count = source_mesh.get_static_mesh_description(0).get_triangle_count()
        split_triangle_count = mesh_spec["triangles"] - sum(
            target["expected_triangles"] for target in mesh_spec["targets"])
        if triangle_count == mesh_spec["triangles"]:
            source_states.append("original")
        elif triangle_count == split_triangle_count:
            source_states.append("split")
        else:
            require(False,
                    f"来源网格 {source_path} 有 {triangle_count} 个三角形，既非原始全量"
                    f" {mesh_spec['triangles']}，也非本工具拆分态 {split_triangle_count}。")
        source_meshes[mesh_name] = source_mesh

    require(len(set(source_states)) == 1,
            "五个来源网格处于混合拆分状态；需恢复全部原始 FBX 网格后再运行拆分脚本。")
    if source_states[0] == "split":
        # 已拆分时不再导入 FBX 覆盖材质槽，只验证独立网格并刷新各自精确包围盒碰撞。
        for mesh_spec in MESHES:
            for target_spec in mesh_spec["targets"]:
                target_path = f"{OUTPUT_FOLDER}/{target_spec['name']}"
                target_mesh = assets.load_asset(target_path)
                require(isinstance(target_mesh, unreal.StaticMesh),
                        f"已拆分来源缺少交互网格 {target_path}，请从原始 FBX 重新生成。")
                target_description = target_mesh.get_static_mesh_description(0)
                require(target_description.get_triangle_count() == target_spec["expected_triangles"],
                        f"已拆分网格 {target_path} 三角形数量不匹配。")
                set_target_collision_box(target_mesh, target_path)
                require(assets.save_loaded_asset(target_mesh, False), f"无法保存拆分网格 {target_path}。")
                unreal.log(
                    f"CASTLE_SKY_SPLIT_REUSED path={target_path} "
                    f"triangles={target_description.get_triangle_count()}"
                )

        for mesh_name in ("SM_SkyIsland_Rest", "SM_SkyIsland_Shop", "SM_SkyIsland_Gamble", "SM_SkyIsland_PinballNPC"):
            bounds = source_meshes[mesh_name].get_bounds()
            span_x = bounds.box_extent.x * 2.0
            span_y = bounds.box_extent.y * 2.0
            require(abs(span_x - 2400.0) <= 20.0 and abs(span_y - 2400.0) <= 20.0,
                    f"岛体网格 {mesh_name} 拆分后跨度为 {span_x:.2f} x {span_y:.2f} cm。")
        unreal.log(f"CASTLE_SKY_SPLIT_COMPLETE targets=8 sources={len(MESHES)} reused=true")
        return

    # 初始执行只从原始 StaticMeshDescription 读几何，保留现有 FBX 导入选项和所有材质槽。
    source_data = {}
    all_target_records = []

    # 所有来源和每个预期目标均先通过检查，再进行任何网格写入。
    for mesh_spec in MESHES:
        source_path = f"{SOURCE_FOLDER}/{mesh_spec['name']}"
        source_mesh = unreal.EditorAssetLibrary.load_asset(source_path)
        static_materials = source_mesh.get_editor_property("static_materials")
        material_slot_names = [str(item.get_editor_property("material_slot_name")) for item in static_materials]
        description = source_mesh.get_static_mesh_description(0)
        components, triangle_polygon_ids, vertex_objects = analyze_connected_components(
            description, material_slot_names)
        require(description.get_triangle_count() == mesh_spec["triangles"],
                f"网格 {source_path} 三角形数量与已核对 FBX 不符。")

        selected_by_target = {}
        for target_spec in mesh_spec["targets"]:
            selected_roots, selected_triangles, selected_polygons = select_target_components(
                components, target_spec, mesh_spec["name"])
            selected_components = {root: components[root] for root in selected_roots}
            target_spec["components"] = selected_components
            selected_by_target[target_spec["name"]] = {
                "triangles": selected_triangles,
                "polygons": selected_polygons,
            }
            for root in selected_roots:
                component = components[root]
                unreal.log(
                    f"CASTLE_SKY_SPLIT_COMPONENT mesh={mesh_spec['name']} target={target_spec['name']} "
                    f"triangles={len(component['triangles'])} vertices={len(component['vertices'])} "
                    f"bounds_min={component['minimum']} bounds_max={component['maximum']} "
                    f"materials={component['slot_names']}"
                )
            all_target_records.append((source_path, target_spec, selected_polygons, vertex_objects))

        source_data[mesh_spec["name"]] = {
            "mesh": source_mesh,
            "description": description,
            "targets": selected_by_target,
            "triangle_polygon_ids": triangle_polygon_ids,
            "vertex_objects": vertex_objects,
        }

    for source_path, target_spec, selected_polygons, vertex_objects in all_target_records:
        target_path = f"{OUTPUT_FOLDER}/{target_spec['name']}"
        make_target_mesh(source_path, target_path, target_spec, selected_polygons, vertex_objects)

    # 原岛体/盘面只移除已复制到独立网格中的多边形，其余几何、坐标与材质映射保留。
    for mesh_spec in MESHES:
        data = source_data[mesh_spec["name"]]
        source_mesh = data["mesh"]
        description = data["description"]
        removed_polygons = set()
        for target_data in data["targets"].values():
            removed_polygons.update(target_data["polygons"])
        for polygon_index in sorted(removed_polygons):
            description.delete_polygon(unreal.PolygonID(polygon_index))
        expected_remaining = mesh_spec["triangles"] - sum(
            len(target_data["triangles"]) for target_data in data["targets"].values())
        require(description.get_triangle_count() == expected_remaining,
                f"原网格 {mesh_spec['name']} 移除交互几何后的三角形数量错误。")
        rebuild_mesh(source_mesh, description, source_mesh.get_path_name())
        require(unreal.EditorAssetLibrary.save_loaded_asset(source_mesh, False),
                f"无法保存移除交互几何后的来源网格 {mesh_spec['name']}。")
        unreal.log(
            f"CASTLE_SKY_SOURCE_UPDATED path={source_mesh.get_path_name()} "
            f"triangles={description.get_triangle_count()} removed_triangles="
            f"{mesh_spec['triangles'] - description.get_triangle_count()}"
        )

    # 四座空岛基座仍保留 24 米水平跨度，后续 C++ 端到端测试再检查 NoCollision 与射线命中。
    for mesh_name in ("SM_SkyIsland_Rest", "SM_SkyIsland_Shop", "SM_SkyIsland_Gamble", "SM_SkyIsland_PinballNPC"):
        mesh = source_data[mesh_name]["mesh"]
        bounds = mesh.get_bounds()
        span_x = bounds.box_extent.x * 2.0
        span_y = bounds.box_extent.y * 2.0
        require(abs(span_x - 2400.0) <= 20.0 and abs(span_y - 2400.0) <= 20.0,
                f"岛体网格 {mesh_name} 拆分后跨度为 {span_x:.2f} x {span_y:.2f} cm。")
        unreal.log(f"CASTLE_SKY_ISLAND_SPAN_OK mesh={mesh_name} span_x={span_x:.2f} span_y={span_y:.2f}")

    unreal.log(f"CASTLE_SKY_SPLIT_COMPLETE targets={len(all_target_records)} sources={len(MESHES)}")


split_sky_island_meshes()
