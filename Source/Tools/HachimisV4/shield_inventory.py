"""将独立盾塔 FBX 交付整理为 HachimisV4Import -Shield 的输入清单。"""

import argparse
import hashlib
import json
from pathlib import Path


# 数据来自本批 FBX 的作者根及节点树；UE 导入器会重新读取并核对，而非按名称猜测占格。
FORMS = {
    # 造型: (占格, 主体网格数, 空节点数, 单份屋面网格数, 跨格接缝网格数)
    "L1": ((1, 1, 1), 212, 7, 103, 0),
    "L2H": ((2, 1, 1), 320, 11, 51, 7),
    "L2V": ((1, 1, 2), 438, 13, 106, 0),
    "L3H": ((2, 2, 1), 487, 15, 43, 10),
    "L3V": ((2, 1, 2), 600, 19, 53, 7),
    "L4": ((2, 2, 2), 906, 25, 44, 14),
    "L5": ((1, 1, 1), 292, 7, 117, 0),
}

# Bay/Edge 是作者的安装跨度，不是外观包围盒。所交付开墙两侧端柱之间实测 160.8 cm。
SPAN = {1: 434, 2: 474.5}
WALLS = {
    "L1_Bay": (1, 18, 11),
    "L2_Bay": (1, 20, 12),
    "L2_Edge": (2, 20, 12),
    "L3_Bay": (1, 16, 10),
    "L3_Edge": (2, 16, 10),
    "L4_Edge": (2, 18, 13),
    "L5_Bay": (1, 18, 13),
}


def slots(form, grid):
    """生成待核验的作者墙位：前后沿 X、左右沿 Y；坐标沿用 FBX 的米与 Y 轴方向。"""
    x, y, z = grid
    result = []
    for level in range(z):
        for side, axis, sign in (("Front", 0, -1), ("Rear", 0, 1), ("Left", 1, -1), ("Right", 1, 1)):
            count = grid[axis]
            span = SPAN[count] / 100
            edge = sign * grid[1 - axis] * SPAN[grid[1 - axis]] / 200
            for bay in range(count):
                along = (bay - (count - 1) / 2) * span
                use = "Bay" if count == 1 else "Edge"
                wall = f"L{form[1]}_{use}"
                if wall not in WALLS:
                    raise ValueError(f"Shield_{form} 缺少作者墙型：{wall}")
                result.append(dict(side=side, level=level, bay=bay + 1, span=span,
                                   pivot=[along, edge] if axis == 0 else [edge, along], wall_key=f"Shield_{wall}"))
    return result


def inventory(source):
    """仅接受完整的 41 个正式 FBX；任何额外文件或缺件都指出路径，不跳过或补件。"""
    source = source.resolve(strict=True)
    chosen = set()

    def entry(file, root, mesh_parts, empty_nodes=1):
        if file in chosen:
            raise ValueError(f"盾塔 FBX 清单路径重复：{file}")
        chosen.add(file)
        path = source / file
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        return dict(file=file, root=root, mesh_parts=mesh_parts, empty_nodes=empty_nodes, sha256=digest)

    buildings, walls = [], []
    for form, (grid, meshes, empties, roof_parts, keys) in FORMS.items():
        stem = f"Shield_{form}"
        bp = f"BP_ShieldTower_lv{form[1]}{'_vertical' if form.endswith('V') else ''}"
        folder = f"RoofModules/07_Shield/{form}"
        roof = [entry(f"{folder}/Roof_{stem}_X{x}Y{y}.fbx", f"ROOT_Roof_{stem}_X{x}Y{y}", roof_parts)
                for x in range(1, grid[0] + 1) for y in range(1, grid[1] + 1)]
        # 接缝文件只存在于作者划成多个屋面格的造型中，不用单格文件替代它。
        if keys:
            roof.append(entry(f"{folder}/RoofKeys_{stem}.fbx", f"ROOT_RoofKeys_{stem}", keys))
        buildings.append(dict(entry(f"Buildings_Closed/07_Shield/{stem}_Closed.fbx", f"ROOT_{stem}", meshes, empties),
                              kind="Shield", form=form, grid=grid, blueprint=f"/Game/MovingCastle/BP/{bp}",
                              slots=slots(form, grid), chassis=False, roof_bays=grid[0] * grid[1], roof_keys=keys, roof_modules=roof))
    for variant, (side, opened, closed) in WALLS.items():
        key = f"Shield_{variant}"
        walls.append(dict(key=key, family="Shield", span=SPAN[side], opening=160.8,
                          open=entry(f"WallParts/Open/07_Shield/WALL_{key}_Open.fbx", f"ROOT_{key}_Open", opened),
                          closed=entry(f"WallParts/Closed/07_Shield/WALL_{key}_Closed.fbx", f"ROOT_{key}_Closed", closed)))
    ladder = dict(entry("FloorLadder/07_Shield/KIT_Shield_FloorLadder.fbx", "ROOT_Shield_FloorLadder", 109, 3), kind="Shield")
    actual = {file.relative_to(source).as_posix() for file in source.rglob("*.fbx")}
    if actual != chosen or len(chosen) != 41:
        raise ValueError(f"盾塔 FBX 范围错误：缺少={sorted(chosen - actual)}，未知={sorted(actual - chosen)}；清单数={len(chosen)}")
    return dict(source=str(source), buildings=buildings, walls=walls, ladders=[ladder], unselected=[])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    # 输入范围核对完成才发布清单；不改 FBX 目录和工程资产。
    manifest = inventory(args.source)
    args.output.write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"已核对 7 套盾塔、7 对墙、1 套楼板梯和 19 份屋顶模块/接缝：{args.output.resolve()}")


if __name__ == "__main__":
    main()
