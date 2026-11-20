"""按作者清单选择正式 FBX，为 Unreal 命令行导入生成可复核的输入。"""

import argparse
import hashlib
import json
from pathlib import Path


# 39 个主体对应 34 个现有蓝图。普通房其余五个主体是单格 PCG 的分件来源。
FAMILIES = {
    "Room": ("01_Room", "NormalRoom", ("L1", "L2H", "L2V", "L3H", "L3V", "L4")),
    "Archer": ("02_Archer", "ArcherTower", ("L1", "L2H", "L2V", "L3H", "L3V", "L4", "L5")),
    "Cannon": ("03_Cannon", "CannonTower", ("L1", "L2H", "L2V", "L3H", "L3V", "L4", "L5")),
    "Farm": ("04_Farm", "Farm", ("L1", "L2H", "L2V", "L3H", "L3V", "L4", "L5")),
    "Balloon": ("05_Balloon", "BalloonTower", ("L1", "L2H", "L2V", "L3H", "L3V", "L4")),
    "Hive": ("06_Hive", "BeeHive", ("L1", "L2H", "L2V", "L3H", "L3V", "L4")),
}

# 仅两份有正式对应件的气球文件不参与导入。其他范围变化均需先核对。
UNSELECTED = {
    "FBX/WallParts/05_Balloon/WALL_Balloon_L3_Edge_Open.fbx": "采用正式 Open 目录的同名文件",
    "FBX/WallParts/05_Balloon/WALL_Balloon_L3_Edge_Closed.fbx": "采用正式 Closed 目录的同名文件",
}


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def index_unique(rows, key):
    result = {}
    for row in rows:
        identity = key(row)
        if identity in result:
            raise ValueError(f"作者清单存在重复项 {identity}")
        result[identity] = row
    return result


def inventory(source):
    """核对磁盘范围、历史清单归属、文件大小及未选文件，向导入器传递相对路径。"""
    source = source.resolve(strict=True)
    qa = read_json(source / "Verification/geometry_qa.json")
    manifest = index_unique(read_json(source / "Verification/fbx_manifest.json"), lambda row: row["file"])
    qa_buildings = index_unique(qa["assets"], lambda row: (row["kind"], row["form"]))
    chosen = set()

    def select(original_path, actual_path=None):
        # Room 底盘在正式交付中移入 Buildings_Closed，本体不带 Closed 后缀。
        actual = actual_path if actual_path is not None else original_path
        entry = dict(manifest[original_path])
        path = source / actual
        if actual in chosen or path.stat().st_size != entry["bytes"]:
            raise ValueError(f"{actual} 重复或字节数与作者清单不符")
        chosen.add(actual)
        entry["file"] = actual
        with path.open("rb") as stream:
            entry["sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
        return entry

    buildings = []
    ladders = []
    walls = []
    for family, (directory, blueprint_prefix, forms) in FAMILIES.items():
        for form in forms:
            data = qa_buildings[family, form]
            stem = f"{family}_{form}"
            closed = f"FBX/Buildings_Closed/{directory}/{stem}"
            if data["chassis_only"]:
                entry = select(f"FBX/Buildings/{directory}/{stem}.fbx", f"{closed}.fbx")
            else:
                entry = select(f"{closed}_Closed.fbx")
            if data["grid"] != entry["grid"] or data["parts_closed"] != entry["mesh_parts"]:
                raise ValueError(f"{closed} 的网格或节点数在作者清单间不一致")
            bp_name = f"BP_{blueprint_prefix}_lv{form[1]}{'_vertical' if form.endswith('V') else ''}"
            entry.update(slots=data["wall_bays"], chassis=data["chassis_only"],
                         roof_bays=data["roof_modules"], roof_keys=data["whole_connector_meshes"])
            blueprint = f"/Game/MovingCastle/BP/{bp_name}" if family != "Room" or form == "L1" else ""
            entry["blueprint"] = blueprint
            buildings.append(entry)
        ladders.append(select(f"FBX/FloorLadder/{directory}/KIT_{family}_FloorLadder.fbx"))
    for key, states in sorted(qa["wall_sets"].items()):
        family = key.split("_", 1)[0]
        directory = FAMILIES[family][0]
        pair = {"key": key, "family": family, "span": states["Open"]["span_cm"],
                "opening": states["Open"]["min_opening_cm"]}
        for state in ("Open", "Closed"):
            part = select(f"FBX/WallParts/{state}/{directory}/WALL_{key}_{state}.fbx")
            if part["wall_key"] != key or part["kind"] != family or part["mesh_parts"] != states[state]["mesh_parts"]:
                raise ValueError(f"{part['file']} 的墙键、系列或网格数在作者清单间不一致")
            pair[state.lower()] = part
        walls.append(pair)
    actual = {p.relative_to(source).as_posix() for p in (source / "FBX").rglob("*.fbx")}
    if actual != chosen | UNSELECTED.keys():
        raise ValueError(f"正式目录范围发生变化。未知={sorted(actual - chosen - UNSELECTED.keys())}，"
                         f"缺少={sorted((chosen | UNSELECTED.keys()) - actual)}")
    unused = []
    for path, reason in UNSELECTED.items():
        with (source / path).open("rb") as stream:
            unused.append({"file": path, "reason": reason, "sha256": hashlib.file_digest(stream, "sha256").hexdigest()})
    if (len(buildings), sum(bool(row["blueprint"]) for row in buildings),
            len(walls), len(ladders)) != (39, 34, 38, 6):
        raise ValueError("交付范围应包含 39 个主体、34 个正式蓝图、38 对墙、6 份楼板梯")
    return {"source": str(source), "buildings": buildings, "walls": walls, "ladders": ladders, "unselected": unused}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    # 全部核对完成后才发布清单，失败时 Python 的异常包含具体输入文件。
    result = inventory(args.source)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"已核对 39 个主体、34 个正式蓝图、38 对墙、6 份梯、2 个非标准副本。清单 {args.output.resolve()}")


if __name__ == "__main__":
    main()
