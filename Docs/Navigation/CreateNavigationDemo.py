"""创建原生导航演示，并为当前建造测试关卡添加一个覆盖棋盘的全局导航范围。"""
import unreal

assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def ensure_bounds(center, extent):
    existing = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.NavMeshBoundsVolume)]
    if len(existing) > 1:
        raise RuntimeError("关卡已有多个 NavMeshBoundsVolume，请先检查范围，脚本不会自动删除它们")
    volume = existing[0] if existing else actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, center)
    if not volume:
        raise RuntimeError("无法创建全局导航范围")
    volume.set_actor_label("Castle_GlobalNavigationBounds")
    volume.set_actor_location(center, False, False)
    volume.set_actor_rotation(unreal.Rotator(), False)
    _, current_extent = volume.get_actor_bounds(False)
    if min(current_extent.x, current_extent.y, current_extent.z) <= 0:
        raise RuntimeError("导航范围缺少 Brush 几何，无法计算体积")
    scale = volume.get_actor_scale3d()
    volume.set_actor_scale3d(unreal.Vector(scale.x * extent.x / current_extent.x,
                                         scale.y * extent.y / current_extent.y,
                                         scale.z * extent.z / current_extent.z))
    unreal.log(f"NAV_BOUNDS: {volume.get_actor_bounds(False)}")
    # 只更新导航本身的属性，其他场景 Actor 保持原有设置。
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.RecastNavMesh):
            actor.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
    return volume


demo_path = "/Game/MovingCastle/Maps/L_NPCNavigationDemo"
if not assets.does_asset_exist(demo_path):
    if not levels.new_level(demo_path):
        raise RuntimeError("无法创建导航演示关卡")
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.GameModeBase)
    actors.spawn_actor_from_class(unreal.CastleNavigationDemo, unreal.Vector())
    actors.spawn_actor_from_class(unreal.DirectionalLight, [0, 0, 1800], unreal.Rotator(pitch=-55, yaw=-25))
    actors.spawn_actor_from_class(unreal.SkyLight, [0, 0, 1000])
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, [0, 0, 0])
    actors.spawn_actor_from_class(unreal.PlayerStart, [-1800, -3100, 2300], unreal.Rotator(pitch=-25, yaw=60))
    ensure_bounds(unreal.Vector(400, 0, 450), unreal.Vector(2200, 1000, 800))
    if not levels.save_current_level():
        raise RuntimeError("保存导航演示失败")

# 当前实际游戏仍使用 TestMap1；范围跟随棋盘的完整尺寸和变换计算，不修改棋盘或美术。
main_map = "/Game/MovingCastle/TestMap1"
if not levels.load_level(main_map):
    raise RuntimeError("无法读取现有建造关卡")
boards = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.CastleBoard)]
if len(boards) != 1:
    raise RuntimeError(f"预期一个棋盘，实际 {len(boards)} 个；没有修改此关卡")
board = boards[0]
size = board.get_editor_property("grid_size")
origin = board.get_editor_property("grid_origin")
corners = []
for x in [origin.x, origin.x + size.x - 1]:
    for y in [origin.y, origin.y + size.y - 1]:
        for z in [origin.z, origin.z + size.z - 1]:
            corners.append(board.get_grid_world_location(unreal.IntVector(x, y, z)))
mins = unreal.Vector(min(p.x for p in corners), min(p.y for p in corners), min(p.z for p in corners))
maxs = unreal.Vector(max(p.x for p in corners), max(p.y for p in corners), max(p.z for p in corners))
ensure_bounds((mins + maxs) * 0.5, (maxs - mins) * 0.5 + unreal.Vector(1000, 1000, 1000))
if not levels.save_current_level():
    raise RuntimeError("保存实际关卡的导航范围失败")
unreal.log("NAVIGATION_ASSETS_READY: 全局 NavMesh 范围和楼梯/合并共享寻路演示已保存。")
