"""按正式材质清册迁移 MainMap 可见资产。由 UE 编辑器 Python 执行。"""

import json
from collections import deque
from pathlib import Path
import sys

import unreal as ue


sys.path.insert(0, str(Path(__file__).resolve().parent))
from create_toon_assets import (ASSETS, EDITING, ROOT, TOOLS, build_shared_assets,
                                compile_material, connect_color_input, connect_front,
                                function_call, get_or_create, load, node, require, save)


GAME = '/Game/MovingCastle'
MOTHERS = f'{ROOT}/Materials'
PHONG = '/InterchangeAssets/Materials/FBXLegacyPhongSurfaceMaterial'
ENEMY = '/Game/RPGMonsterWave2PBR/Material'
MANNY = '/Game/Characters/Mannequins/Materials'
MANIFEST = Path(__file__).with_name('material_manifest.json')


def scan_materials(roots):
    """递归审计序列化的硬引用及软引用。清册另外声明 C++ 动态加载的入口。"""
    registry = ue.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    for path in roots:
        require(registry.get_assets_by_package_name(path), f'找不到材质审计根包：{path}')
    settings = ue.AssetRegistryDependencyOptions()
    settings.set_editor_properties(dict(include_soft_package_references=True, include_hard_package_references=True,
                                        include_searchable_names=False, include_soft_management_references=False,
                                        include_hard_management_references=False))
    pending = deque(roots)
    visited = set(roots)
    materials = set()
    while pending:
        package = pending.popleft()
        if any(str(asset.asset_class_path.asset_name) in ('Material', 'MaterialInstance', 'MaterialInstanceConstant')
               for asset in registry.get_assets_by_package_name(package)):
            materials.add(package)
        dependencies = registry.get_dependencies(package, settings)
        if dependencies is None:
            # 引擎脚本包和无依赖的叶资产均不产生后续材质引用。
            continue
        for path in map(str, dependencies):
            if path.startswith('/Script/') or path in visited:
                continue
            pending.append(path)
            visited.add(path)
    return materials


def mark_usage(material):
    """预编译静态网格、骨骼网格和 Mass 的原生实例化骨骼着色器排列。"""
    material.set_editor_properties(dict(used_with_skeletal_mesh=True, used_with_instanced_static_meshes=True,
                                        used_with_instanced_skinned_mesh=True))


def retarget_surface(material, function, constant_v4=False):
    """保留连接中的颜色与发光，只有无节点的 V4 源材质需要从固定输入转为节点。"""
    existing = EDITING.get_material_property_input_node(material, ue.MaterialProperty.MP_FRONT_MATERIAL)
    if existing:
        require(isinstance(existing, ue.MaterialExpressionMaterialFunctionCall)
                and existing.get_editor_property('material_function') == function,
                f'{material.get_path_name()} 已有未声明的 Front Material')
        if not constant_v4:
            # 兼容已经接入 Toon 的资产，只在补入原图的内联发光时重编译。
            count = EDITING.get_num_material_expressions(material)
            require(TOOLS.connect_legacy_surface(material, existing), f'{material.get_path_name()} 无法复核源接线')
            if EDITING.get_num_material_expressions(material) != count:
                compile_material(material)
        return
    call = function_call(material, function)
    if constant_v4:
        base, emissive = require(TOOLS.read_v4_colors(material), f'{material.get_path_name()} 无法读取线性色彩')
        connect_color_input(material, call, 'BaseColor', 'BaseColor', base)
        connect_color_input(material, call, 'EmissiveColor', 'Emissive', emissive)
    else:
        require(TOOLS.connect_legacy_surface(material, call), f'{material.get_path_name()} 无法迁移原始接线')
    connect_front(material, call)
    compile_material(material)


def duplicate_source(source, target, kind):
    """把源图或实例参数复制到项目自有包，保证第三方源包只读。"""
    if ASSETS.does_asset_exist(target):
        return load(target, kind)
    asset = ASSETS.duplicate_asset(source, target)
    return require(isinstance(asset, kind) and asset, f'{source} 无法复制到 {target}')


def toon_copy(source, name, function):
    """复用原贴图、UV、透明和遮罩运算，只替换光照入口。"""
    material = duplicate_source(source, f'{MOTHERS}/{name}', ue.Material)
    mark_usage(material)
    retarget_surface(material, function)
    save(material)
    return material


def replace_instance_parent(instance, old, new):
    """确认每个本地覆盖参数在新母材质有同类型同名定义后再改挂。"""
    parent = instance.get_editor_property('parent')
    require(parent == old or parent == new, f'{instance.get_path_name()} 的父材质不在清册中：{parent}')
    unsupported = TOOLS.get_unsupported_overrides(instance, new)
    require(not unsupported, f'{instance.get_path_name()} 目标母材质缺参数：{", ".join(unsupported)}')
    if parent != new:
        EDITING.set_material_instance_parent(instance, new)
        require(instance.get_editor_property('parent') == new, f'{instance.get_path_name()} 改挂失败')
        save(instance)


def build_color_materials(function):
    """硬编码几何件与数字底板共用原 Color 参数，灯光语义由母材质决定。"""
    for name in ('M_Toon_BasicColor', 'M_Info_UnlitColor'):
        material = get_or_create(f'{MOTHERS}/{name}', ue.Material, ue.MaterialFactoryNew)
        if not EDITING.get_material_expressions(material):
            mark_usage(material)
            color = node(material, ue.MaterialExpressionVectorParameter, -220, 0)
            color.set_editor_properties(dict(parameter_name='Color', default_value=ue.LinearColor(0.5, 0.5, 0.5, 1.0)))
            if name == 'M_Toon_BasicColor':
                call = function_call(material, function)
                require(EDITING.connect_material_expressions(color, '', call, 'BaseColor'), f'{name} 不接受 Color')
                connect_front(material, call)
            else:
                material.set_editor_property('shading_model', ue.MaterialShadingModel.MSM_UNLIT)
                require(EDITING.connect_material_property(color, '', ue.MaterialProperty.MP_EMISSIVE_COLOR),
                        f'{name} 无法写入自发光')
            compile_material(material)
            save(material)


def build_mass_assets(function):
    """NPC、基础敌人和正式怪物的主体全部按真实网格槽写入 Mass 覆盖。"""
    mat_enemy = toon_copy(f'{ENEMY}/PolyartDefaultMAT', 'M_Toon_EnemyPolyart', function)
    mat_mask = toon_copy(f'{ENEMY}/PolyartMaskTintMAT', 'M_Toon_EnemyPolyartMask', function)
    mat_npc = toon_copy('/Game/ModularRPGHeroesPolyart/Materials/PolyartMat', 'M_Toon_NPCPolyart', function)
    mat_lord = toon_copy(f'{GAME}/Model/Characters/TutorialLord/M_TutorialLord', 'M_Toon_TutorialLord', function)
    mat_manny = toon_copy(f'{MANNY}/M_Mannequin', 'M_Toon_MassHumanoid', function)
    instances = {}
    for suffix in ('01', '02'):
        old = load(f'{MANNY}/Manny/MI_Manny_{suffix}_New', ue.MaterialInstanceConstant)
        instance = duplicate_source(old.get_path_name(), f'{MOTHERS}/MI_Toon_MassHumanoid_{suffix}', ue.MaterialInstanceConstant)
        # 第二个槽源实例继承第一个槽的参数，项目侧实例沿用该层级。
        parent = mat_manny if suffix == '01' else instances['Manny01']
        replace_instance_parent(instance, old.get_editor_property('parent'), parent)
        instances[f'Manny{suffix}'] = instance
        old = load(f'{ENEMY}/PolyartMaskTintMat{suffix}_Inst', ue.MaterialInstanceConstant)
        instance = duplicate_source(old.get_path_name(), f'{MOTHERS}/MI_Toon_EnemyPolyart_{suffix}', ue.MaterialInstanceConstant)
        replace_instance_parent(instance, old.get_editor_property('parent'), mat_mask)
        instances[f'Enemy{suffix}'] = instance

    overrides = {
        'DA_MassNPCBase': [mat_npc], 'DA_MassNPC_TutorialLord': [mat_lord],
        'DA_MassEnemyBase': [instances['Manny01'], instances['Manny02']],
        'DA_MassEnemy_LavaGoliath': [instances['Enemy01']], 'DA_MassEnemy_LavaPup': [instances['Enemy02']],
        'DA_MassEnemy_MiasmaWelp': [mat_enemy], 'DA_MassEnemy_ShadowSwarm': [mat_enemy],
    }
    for name, slots in overrides.items():
        config = load(f'{GAME}/Mass/{name}', ue.MassEntityConfigAsset)
        trait, = [item for item in config.get_editor_property('config').get_editor_property('traits')
                  if item.get_class().get_name() == 'CastleMassHumanoidVisualizationTrait']
        mesh = trait.get_editor_property('animation_provider').get_skinned_asset()
        require(len(mesh.get_editor_property('materials')) == len(slots), f'{name} 的材质槽数量与清册不符')
        if list(trait.get_editor_property('material_overrides')) != slots:
            trait.set_editor_property('material_overrides', slots)
            save(config)

    # 暗影群的剑使用项目自有骨骼网格，同一动画批次的附属网格描述独立读取该槽。
    sword = load(f'{GAME}/Mass/SKM_SpecterSword', ue.SkeletalMesh)
    slots = sword.get_editor_property('materials')
    require(len(slots) == 1, f'{sword.get_path_name()} 的材质槽数量与清册不符')
    if slots[0].get_editor_property('material_interface') != mat_enemy:
        slot = slots[0]
        slot.set_editor_property('material_interface', mat_enemy)
        sword.set_editor_property('materials', [slot])
        save(sword)


def migrate():
    """先验证清册覆盖，再逐族迁移，未声明的资产会阻断本次写入。"""
    manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
    categories = manifest['materials']
    declared = set().union(*map(set, categories.values()))
    require(len(declared) == sum(map(len, categories.values())), '材质清册有重复分类')
    unknown = sorted(scan_materials(manifest['roots']) - declared)
    if unknown:
        for path in unknown:
            ue.log_error(f'Toon 迁移缺少材质分类：{path}')
        raise RuntimeError(f'发现 {len(unknown)} 个未分类材质')

    functions = build_shared_assets(ROOT)
    build_color_materials(functions['Surface'])
    phong = toon_copy(PHONG, 'M_Toon_FBXPhong', functions['Surface'])
    legacy_phong = load(PHONG, ue.Material)
    for path in categories['fbx']:
        instance = load(path, ue.MaterialInstanceConstant)
        missing = TOOLS.get_unsupported_overrides(instance, phong)
        require(not missing, f'{path} 目标 FBX 母材质缺参数：{", ".join(missing)}')
    for path in categories['fbx']:
        replace_instance_parent(load(path, ue.MaterialInstanceConstant), legacy_phong, phong)

    for category, paths in categories.items():
        family, _, profile = category.partition('_')
        if family in ('constants', 'graphs'):
            for path in paths:
                material = load(path, ue.Material)
                if profile == 'Unit':
                    mark_usage(material)
                retarget_surface(material, functions[profile], constant_v4=(family == 'constants'))
                require(ASSETS.save_loaded_asset(material, True), f'{path} 保存失败')
    build_mass_assets(functions['Unit'])
    ue.log(f'STYLIZED_MATERIALS 已核对 {len(declared)} 个清册条目，未分类 0')


if __name__ == '__main__':
    migrate()
