"""只读检查空岛 FBX 来源网格的连通三角形、包围盒和材质槽分布。"""

import os
import unreal


SKY_ISLAND_MESHES = (
    (
        "SM_SkyIsland_Rest",
        "/Game/MovingCastle/Model/SkyIslands/SM_SkyIsland_Rest",
        "RawContent/SkyIslands/SM_SkyIsland_Rest.fbx",
    ),
    (
        "SM_SkyIsland_Shop",
        "/Game/MovingCastle/Model/SkyIslands/SM_SkyIsland_Shop",
        "RawContent/SkyIslands/SM_SkyIsland_Shop.fbx",
    ),
    (
        "SM_SkyIsland_Gamble",
        "/Game/MovingCastle/Model/SkyIslands/SM_SkyIsland_Gamble",
        "RawContent/SkyIslands/SM_SkyIsland_Gamble.fbx",
    ),
    (
        "SM_SkyIsland_PinballNPC",
        "/Game/MovingCastle/Model/SkyIslands/SM_SkyIsland_PinballNPC",
        "RawContent/SkyIslands/SM_SkyIsland_PinballNPC.fbx",
    ),
    (
        "SM_Pinball_Board",
        "/Game/MovingCastle/Model/SkyIslands/SM_Pinball_Board",
        "RawContent/SkyIslands/SM_Pinball_Board.fbx",
    ),
)

# 交互物在原合并模型中的作者坐标。该范围只用于定位可能属于交互物的
# 连通分量，不会直接据此删除、过滤或拆分任何几何。
TARGET_REGIONS = (
    ("SM_SkyIsland_Rest", "DeparturePortal", (700.0, 0.0, 200.0), (240.0, 240.0, 300.0)),
    ("SM_SkyIsland_Shop", "DeparturePortal", (700.0, 0.0, 200.0), (240.0, 240.0, 300.0)),
    ("SM_SkyIsland_Gamble", "DeparturePortal", (700.0, 0.0, 200.0), (240.0, 240.0, 300.0)),
    ("SM_SkyIsland_PinballNPC", "DeparturePortal", (700.0, 0.0, 200.0), (240.0, 240.0, 300.0)),
    ("SM_SkyIsland_Gamble", "Roulette", (-80.0, -550.0, 66.0), (260.0, 260.0, 300.0)),
    ("SM_SkyIsland_Gamble", "SlotMachine", (-10.0, 10.0, 114.0), (260.0, 260.0, 300.0)),
    ("SM_SkyIsland_Gamble", "BalloonRecycler", (-40.0, 530.0, 116.0), (260.0, 260.0, 300.0)),
    ("SM_Pinball_Board", "PinballConsole", (585.0, 0.0, 39.0), (180.0, 180.0, 220.0)),
)


def require(value, message):
    """缺少资产或源文件时立即停止检查，不把不完整结果报告为成功。"""
    if not value:
        unreal.log_error(message)
        raise RuntimeError(message)
    return value


def find_component_root(parents, triangle_index):
    """查找并路径压缩三角形并查集根节点。"""
    root = triangle_index
    while parents[root] != root:
        root = parents[root]
    while parents[triangle_index] != triangle_index:
        next_index = parents[triangle_index]
        parents[triangle_index] = root
        triangle_index = next_index
    return root


def union_components(parents, first_triangle, second_triangle):
    """将共享同一个 FVertexID 的三角形连入同一连通分量。"""
    first_root = find_component_root(parents, first_triangle)
    second_root = find_component_root(parents, second_triangle)
    if first_root != second_root:
        parents[second_root] = first_root


def inspect_static_mesh(display_name, asset_path, expected_source_path):
    """读取一个静态网格的原始来源、顶点连通分量和各分量材质槽。"""
    mesh = unreal.EditorAssetLibrary.load_asset(asset_path)
    require(isinstance(mesh, unreal.StaticMesh), f"网格资产 {asset_path} 缺失或不是 StaticMesh")

    import_data = mesh.get_editor_property("asset_import_data")
    require(import_data, f"网格资产 {asset_path} 缺少导入来源元数据")
    source_path = import_data.get_first_filename().replace("\\", "/")
    expected_source_suffix = expected_source_path.replace("\\", "/").lower()
    require(
        source_path.lower().endswith(expected_source_suffix),
        f"网格资产 {asset_path} 的实际来源为 {source_path}，预期来源为 {expected_source_path}",
    )

    mesh_description = mesh.get_static_mesh_description(0)
    require(mesh_description, f"网格资产 {asset_path} 无法读取 LOD0 MeshDescription")
    triangle_count = mesh_description.get_triangle_count()
    require(triangle_count > 0, f"网格资产 {asset_path} 的 LOD0 没有三角形")

    material_slots = [
        str(material.get_editor_property("material_slot_name"))
        for material in mesh.get_editor_property("static_materials")
    ]
    triangle_vertices = []
    triangle_groups = []
    vertex_objects = {}
    parents = list(range(triangle_count))
    first_triangle_for_vertex = {}

    # MeshDescription 的三角形通过 VertexInstanceID 读取，再映射到真正焊接共享的
    # VertexID。仅比较顶点实例会把同一焊接顶点上的不同法线/UV 误判为独立零件。
    for triangle_index in range(triangle_count):
        triangle_id = unreal.TriangleID(triangle_index)
        vertex_instance_ids = mesh_description.get_triangle_vertex_instances(triangle_id)
        require(
            len(vertex_instance_ids) == 3,
            f"网格资产 {asset_path} 的三角形 {triangle_index} 具有 {len(vertex_instance_ids)} 个顶点实例",
        )

        vertex_ids = []
        for vertex_instance_id in vertex_instance_ids:
            vertex_id = mesh_description.get_vertex_instance_vertex(vertex_instance_id)
            vertex_value = vertex_id.get_editor_property("id_value")
            require(
                mesh_description.is_vertex_valid(vertex_id),
                f"网格资产 {asset_path} 的三角形 {triangle_index} 引用了无效 VertexID {vertex_value}",
            )
            vertex_ids.append(vertex_value)
            vertex_objects[vertex_value] = vertex_id

            previous_triangle = first_triangle_for_vertex.get(vertex_value)
            if previous_triangle is None:
                first_triangle_for_vertex[vertex_value] = triangle_index
            else:
                union_components(parents, triangle_index, previous_triangle)

        triangle_vertices.append(vertex_ids)
        polygon_group_id = mesh_description.get_triangle_polygon_group(triangle_id)
        triangle_groups.append(polygon_group_id.get_editor_property("id_value"))

    # 每个根节点代表一个由共享顶点焊接形成的分量，保存分量的完整三角形集合。
    component_triangles = {}
    for triangle_index in range(triangle_count):
        component_root = find_component_root(parents, triangle_index)
        component_triangles.setdefault(component_root, []).append(triangle_index)

    components = []
    for component_root, component_triangle_indices in component_triangles.items():
        component_vertex_ids = set()
        material_group_counts = {}
        for triangle_index in component_triangle_indices:
            component_vertex_ids.update(triangle_vertices[triangle_index])
            group_id = triangle_groups[triangle_index]
            material_group_counts[group_id] = material_group_counts.get(group_id, 0) + 1

        positions = [
            mesh_description.get_vertex_position(vertex_objects[vertex_id])
            for vertex_id in component_vertex_ids
        ]
        minimum = (
            min(position.x for position in positions),
            min(position.y for position in positions),
            min(position.z for position in positions),
        )
        maximum = (
            max(position.x for position in positions),
            max(position.y for position in positions),
            max(position.z for position in positions),
        )
        slot_distribution = []
        for group_id, group_triangle_count in sorted(material_group_counts.items()):
            slot_name = material_slots[group_id] if 0 <= group_id < len(material_slots) else "<无对应材质槽>"
            slot_distribution.append(f"{group_id}:{slot_name}={group_triangle_count}")

        components.append(
            (
                len(component_triangle_indices),
                len(component_vertex_ids),
                minimum,
                maximum,
                slot_distribution,
            )
        )
    components.sort(key=lambda component: component[0], reverse=True)

    unreal.log(
        f"CASTLE_SKY_MESH {display_name} path={asset_path} source={source_path} "
        f"vertices={mesh_description.get_vertex_count()} triangles={triangle_count} "
        f"polygon_groups={mesh_description.get_polygon_group_count()} material_slots={len(material_slots)}"
    )
    unreal.log(f"CASTLE_SKY_MESH_SLOTS {display_name} {material_slots}")

    # 每个分量输出其三角形/顶点数、本地坐标范围和材质分组，便于确认目标模型是否
    # 与整岛顶点焊接，并计算不改变世界摆放的精确独立资产内容。
    for component_index, component in enumerate(components):
        triangle_total, vertex_total, minimum, maximum, slots = component
        unreal.log(
            f"CASTLE_SKY_COMPONENT {display_name} index={component_index} "
            f"triangles={triangle_total} vertices={vertex_total} "
            f"bounds_min={minimum} bounds_max={maximum} slots={slots}"
        )

    # 只报告与已核对的交互物坐标范围相交的连通分量，便于人工确认目标是否
    # 独立、是否存在覆盖目标区域的整岛连通分量。脚本不据此改动模型。
    for target_mesh_name, target_name, center, extent in TARGET_REGIONS:
        if target_mesh_name != display_name:
            continue

        region_minimum = tuple(center[axis] - extent[axis] for axis in range(3))
        region_maximum = tuple(center[axis] + extent[axis] for axis in range(3))
        candidate_components = []
        for component_index, component in enumerate(components):
            triangle_total, vertex_total, minimum, maximum, slots = component
            intersects_region = all(
                minimum[axis] <= region_maximum[axis] and maximum[axis] >= region_minimum[axis]
                for axis in range(3)
            )
            if not intersects_region:
                continue
            distance_squared = sum(
                max(minimum[axis] - center[axis], 0.0, center[axis] - maximum[axis]) ** 2
                for axis in range(3)
            )
            candidate_components.append(
                (distance_squared, component_index, triangle_total, vertex_total, minimum, maximum, slots)
            )

        candidate_components.sort(key=lambda candidate: (candidate[0], -candidate[2]))
        for rank, candidate in enumerate(candidate_components[:20]):
            distance_squared, component_index, triangle_total, vertex_total, minimum, maximum, slots = candidate
            unreal.log(
                f"CASTLE_SKY_TARGET_COMPONENT {display_name} target={target_name} "
                f"rank={rank} distance_cm={distance_squared ** 0.5:.1f} index={component_index} "
                f"triangles={triangle_total} vertices={vertex_total} "
                f"bounds_min={minimum} bounds_max={maximum} slots={slots}"
            )
        unreal.log(
            f"CASTLE_SKY_TARGET_REGION {display_name} target={target_name} "
            f"candidate_count={len(candidate_components)} reported={min(20, len(candidate_components))} "
            f"center={center} extent={extent}"
        )


def inspect_all_sky_island_meshes():
    """逐个查验指南列出的五个 FBX 来源，不跳过任何缺失或格式错误的资产。"""
    for display_name, asset_path, source_path in SKY_ISLAND_MESHES:
        inspect_static_mesh(display_name, asset_path, source_path)
    unreal.log(f"CASTLE_SKY_MESH_INSPECTION_COMPLETE count={len(SKY_ISLAND_MESHES)}")


inspect_all_sky_island_meshes()
