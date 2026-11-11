"""只读比较等级建筑的真实零件，输出跨等级网格身份、组件名和世界空间位置。"""

import collections
import json
from pathlib import Path

import unreal


def vector(value):
    """统一使用厘米导出位置与尺寸，避免导入模型局部缩放干扰比较。"""
    return [value.x, value.y, value.z]


unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report = {}
for name, x in [("lv1_left", 0), ("lv1_right", 700), ("lv2", 350), ("lv4", 350)]:
    level = int(name[2])
    cls = unreal.load_class(None, f"/Game/MovingCastle/Model/Buildings/BP_ArcherTower_lv{level}.BP_ArcherTower_lv{level}_C")
    assert cls, f"等级模型不存在：{level}"
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(x, 0, 235))
    assert actor, "无法建立只读分析实例。"
    parts = []
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        # 功能组件不会参与合并匹配，离线对照也必须先排除任意深度的功能分支。
        if unreal.BuildingGameplayRootComponent.is_in_gameplay_hierarchy(component):
            continue
        mesh = component.get_editor_property("static_mesh")
        if not mesh or not component.is_visible() or component.component_has_tag("MergeExclude"):
            continue
        center, extent, _ = unreal.SystemLibrary.get_component_bounds(component)
        rotation = component.get_world_rotation()
        parts.append({"component": component.get_name(), "mesh": mesh.get_path_name(),
                      "center": vector(center), "extent": vector(extent),
                      "rotation": [rotation.pitch, rotation.yaw, rotation.roll],
                      "scale": vector(component.get_world_scale())})
    report[name] = parts
    actors.destroy_actor(actor)

source = report["lv1_left"] + report["lv1_right"]
target = report["lv2"]
source_meshes = collections.Counter(part["mesh"] for part in source)
target_meshes = collections.Counter(part["mesh"] for part in target)
report["summary"] = {"source_parts": len(source), "target_parts": len(target),
                     "same_asset_capacity": sum((source_meshes & target_meshes).values()),
                     "source_unique_meshes": len(source_meshes), "target_unique_meshes": len(target_meshes),
                     "shared_names": sorted({p["component"] for p in source} & {p["component"] for p in target})}
folder = Path(unreal.Paths.project_saved_dir()) / "Tests/BuildingPartCorrespondence"
folder.mkdir(parents=True, exist_ok=True)
(folder / "parts.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("BUILDING_PART_CORRESPONDENCE: " + json.dumps(report["summary"], ensure_ascii=False))
