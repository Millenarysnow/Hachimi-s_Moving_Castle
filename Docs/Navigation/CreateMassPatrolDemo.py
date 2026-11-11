"""创建可直接 Play 的完整共享闭环 + Mass NPC 演示。已有地图保留，不批量改动其他资产。"""
import unreal

MAP_PATH = "/Game/MovingCastle/Maps/L_MassPatrolDemo"
assets = unreal.EditorAssetLibrary
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

if assets.does_asset_exist(MAP_PATH):
    unreal.log("MASS_PATROL_DEMO_EXISTS: 保留现有演示地图")
else:
    npc_config = assets.load_asset("/Game/MovingCastle/Mass/DA_MassNPCBase")
    if not npc_config:
        raise RuntimeError("缺少现有 Mass NPC 基础配置，不创建无法显示的演示")
    if not levels.new_level(MAP_PATH):
        raise RuntimeError("无法创建共享巡逻演示地图")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.GameModeBase)

    # 复用经过验证的真实楼梯与飞入墙体，不复制另一套导航几何或用蓝图实现移动。
    geometry = actors.spawn_actor_from_class(unreal.CastleNavigationDemo, unreal.Vector())
    geometry.set_actor_label("PatrolDemo_ActualFloorsStairsAndWalls")
    geometry.set_editor_property("run_path_demonstration", False)
    geometry.set_editor_property("use_fixed_camera", False)
    route = actors.spawn_actor_from_class(unreal.CastlePatrolRoute, unreal.Vector())
    route.set_actor_label("SharedPatrol_1_2_3_4_1")
    route.set_editor_property("local_waypoints", [unreal.Vector(-1200, -300, 5),
                                                 unreal.Vector(1900, -280, 605),
                                                 unreal.Vector(1900, 280, 605),
                                                 unreal.Vector(-1200, 300, 5)])
    spawner = actors.spawn_actor_from_class(unreal.CastleMassSpawner, unreal.Vector())
    spawner.set_actor_label("MassNPCs_32_OneSharedLoop")
    spawner.set_editor_property("configuration", npc_config)
    spawner.set_editor_property("count", 32)
    spawner.set_editor_property("follow_shared_patrol_route", True)

    # 一个覆盖整座演示的全局 NavMeshBoundsVolume；读取实际 Brush 尺寸后设置范围。
    bounds = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(400, 0, 450))
    bounds.set_actor_label("Castle_GlobalNavigationBounds")
    _, current = bounds.get_actor_bounds(False)
    if min(current.x, current.y, current.z) <= 0:
        raise RuntimeError("导航范围没有有效 Brush")
    scale = bounds.get_actor_scale3d()
    bounds.set_actor_scale3d(unreal.Vector(scale.x * 2200 / current.x,
                                          scale.y * 1000 / current.y,
                                          scale.z * 800 / current.z))
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.RecastNavMesh):
            actor.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
    actors.spawn_actor_from_class(unreal.DirectionalLight, [0, 0, 1800], unreal.Rotator(pitch=-55, yaw=-25))
    actors.spawn_actor_from_class(unreal.SkyLight, [0, 0, 1000])
    actors.spawn_actor_from_class(unreal.SkyAtmosphere, [0, 0, 0])
    # 默认飞行相机支持 WASD/鼠标，P 编辑时沿用屏幕中心瞄准，方便在多个楼层放点。
    actors.spawn_actor_from_class(unreal.PlayerStart, [-1800, -3100, 2300], unreal.Rotator(pitch=-31, yaw=55))
    if not levels.save_current_level():
        raise RuntimeError("保存 Mass 巡逻演示失败")
    unreal.log("MASS_PATROL_DEMO_READY: 四段最短路一次发布，32 个 Mass NPC 共用完整闭环")
