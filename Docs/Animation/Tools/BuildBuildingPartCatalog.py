"""离线生成箭塔各级的几何身份目录；只保存目录和建筑定义，不修改原始模型。"""

import collections
import json
from pathlib import Path

import unreal


CATALOG_PATH = "/Game/MovingCastle/Data/Animation/DA_BuildingPartCatalog"
unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
mesh_groups = {}
all_meshes = {}
for suffix in ["lv1", "lv2", "lv2_vertical", "lv3", "lv4"]:
    asset = f"/Game/MovingCastle/Model/Buildings/BP_ArcherTower_{suffix}"
    cls = unreal.load_class(None, asset + "." + asset.rsplit("/", 1)[1] + "_C")
    assert cls, "无法加载等级建筑：" + asset
    actor = subsystem.spawn_actor_from_class(cls, unreal.Vector())
    assert actor, "无法生成分析实例。"
    meshes = [c.get_editor_property("static_mesh") for c in actor.get_components_by_class(unreal.StaticMeshComponent)
              if c.get_editor_property("static_mesh") and c.is_visible() and not c.component_has_tag("MergeExclude")]
    mesh_groups[suffix] = meshes
    all_meshes.update({mesh.get_path_name(): mesh for mesh in meshes})
    subsystem.destroy_actor(actor)

catalog = unreal.load_asset(CATALOG_PATH)
if not catalog:
    # 工具显式创建唯一目录资产，已有目录则在完整重建成功后更新同一资产。
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.BuildingPartCatalog)
    catalog = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "DA_BuildingPartCatalog", "/Game/MovingCastle/Data/Animation", unreal.BuildingPartCatalog, factory)
assert catalog, "无法创建零件几何目录。"
# UE Python 把布尔成功标志用于可空返回值，成功时只返回输出字符串。
error = catalog.rebuild_from_meshes(list(all_meshes.values()))
assert error is not None and not error, error
entries = catalog.get_editor_property("entries")
identities = {mesh.get_path_name(): record.geometry_id for mesh, record in entries.items()}
source = collections.Counter(identities[mesh.get_path_name()] for mesh in mesh_groups["lv1"] * 2)
target = collections.Counter(identities[mesh.get_path_name()] for mesh in mesh_groups["lv2"])
capacity = sum((source & target).values())
report = {"mesh_count": len(entries), "unique_shapes": len(set(identities.values())),
          "lv1_pair_parts": sum(source.values()), "lv2_parts": sum(target.values()),
          "geometry_match_capacity": capacity}
assert capacity == len(mesh_groups["lv2"]), "一级到二级几何身份未覆盖全部零件：" + json.dumps(report)

# 绑定发生在验证之后，避免把不完整目录写进正式建筑定义。
definition = unreal.load_asset("/Game/MovingCastle/Data/Buildings/DA_ArcherTower")
assert definition, "正式箭塔定义不存在。"
definition.set_editor_property("part_catalog", catalog)
assert unreal.EditorAssetLibrary.save_loaded_asset(catalog), "目录保存失败。"
assert unreal.EditorAssetLibrary.save_loaded_asset(definition), "建筑定义保存失败。"
folder = Path(unreal.Paths.project_saved_dir()) / "Tests/BuildingPartCorrespondence"
folder.mkdir(parents=True, exist_ok=True)
(folder / "catalog.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("BUILDING_PART_CATALOG: " + json.dumps(report, ensure_ascii=False))
