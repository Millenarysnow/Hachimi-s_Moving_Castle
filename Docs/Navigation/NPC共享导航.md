# NPC 共享导航（UE 5.8）

本文是关卡配置、操作与验收入口。长期维护、接口契约、计算/发布时序、Mass 进度、线程边界和排障方法见 [NPC 共享导航技术设计](NPC共享导航技术设计.md)；后续 agent 修改此系统前应先阅读该文档。

全局 NavMesh 更新调度、玩家路径点和 Mass NPC 循环巡逻已接通。玩家设置 1～N 个点后，C++ 一次算完 **1→2、2→3、…、N→1 的全部最短路**，再发布一份完整、不可变的共享快照；不会等 NPC 到点再求下一段。所有 NPC 只保存各自距离、当前路段和圈数，不保存自己的路径。路线来自建筑最终实际碰撞，包含楼板、楼梯与合并几何，不记录六面关系。

## 直接验收

打开 `/Game/MovingCastle/Maps/L_MassPatrolDemo` 并点击 Play：

1. 墙体飞入完成、NavMesh 构建结束后，出现绿色完整闭环、编号 1～4 和 32 个实际 Mass 骨骼实例 NPC；NPC 绕墙、上下楼梯并按 1→2→3→4→1 持续循环。画面显示 `Path queries=4`、`Refresh=1`。
2. 用 WASD 与鼠标移动飞行相机。等 NPC 走过路径点甚至完成整圈，`Path queries` 都不应增长。
3. 按 **P** 编辑，画面显示橙色草稿点；瞄准楼板/地面顶面后左键加点，**Backspace** 删除末点，**Enter** 一次提交整条新路线，**P** 取消草稿。编辑过程中不执行寻路，原路线继续工作。**Delete** 清空已发布路线并停止 NPC。
4. 单独一个路径点是原地待命，不查询“自己到自己”的路径。任一点没有 NavMesh、任一段不可达或包含尚未实现的特殊 NavLink 动作，画面和日志都会给出错误，整条路线无效，NPC 停止。

`TestMap1` 的城堡控制器自动提供同一套 P/左键/Enter 操作。先放置路径点，再打开控制台执行 **`CastleSpawnPatrolNPCs 32`**，即可沿完整路线生成 32 个 NPC；也可以先执行命令，生成器会等整条路线就绪。重复该命令会替换它上次生成的批次。建造左键只在巡逻编辑模式下被截获，商店打开时自动退出巡逻草稿模式。

在其他关卡可放置一个 **CastlePatrolRoute**：在 `Local Waypoints` 数组按顺序增加元素并拖动三维手柄，或运行时用上述输入编辑。每个世界只能有一个此 Actor。再放置 **CastleMassSpawner**，配置 `/Game/MovingCastle/Mass/DA_MassNPCBase`、数量，并勾选 **Follow Shared Patrol Route**、关闭 **Start Moving**；生成器等完整闭环就绪后，沿整圈均匀生成。普通网格生成位置不用于巡逻模式。

现有单段导航刷新演示仍独立保留。打开 `/Game/MovingCastle/Maps/L_NPCNavigationDemo` 并点击 Play：

1. 两段墙体同时播放原有飞入组件，屏幕 `Animations` 大于 0，`Refresh` 保持 0。
2. 全部归位后，出现绿色共享路径和黄色转折点，路径绕过墙体并沿楼梯抵达上层。此时 `Refresh=1`、`Path queries=1`。
3. 路径出现 4 秒后，自动播放原有合并组件，墙体向另一侧重组。动画期间路线失效，刷新数保持 1。
4. 合并完成后绿色路径从新开口绕行，`Refresh=2`、`Path queries=2`、`Phase=3`。静止后计数不再增长。重新 Play 可重播。

输出日志搜索 `NPC 导航` 或 `NPC 共享路线` 可查看每轮构建与寻路次数。配置错误、无法投影或不可达会报告原因，不会用旧路径或部分路径冒充成功。演示仅画路径，不生成 NPC。

`/Game/MovingCastle/TestMap1` 已有覆盖现有棋盘的全局 `NavMeshBoundsVolume`。原有放置、合并、回收和直接销毁建筑均已接入刷新调度；未设置路径点时只更新网格。

## NavMesh 配置

- 关卡使用一个覆盖整个可建造范围的 `NavMeshBoundsVolume`，高度必须包含所有可行走楼层。
- Recast 的 `Runtime Generation` 使用 **Dynamic**，这是运行时增加楼板/楼梯所需的底层能力。实际生成受导航构建锁控制，飞入与合并期间不会持续生成。
- 新关卡的默认模式已写入 `DefaultEngine.ini`。已经保存过的 Recast Actor 需要检查自己的实例值；现有演示地图已配置。
- 导航直接读取建筑实际碰撞。楼板、楼梯应有正确的可行走碰撞并阻挡 Pawn；通路宽度、净高、台阶高度需满足 Recast Agent 的半径、高度和最大台阶设置。仅扩大 Bounds 不会修正建筑碰撞。
- 所有建筑由 C++ `BuildingNavigationActor` 过滤导航资格，只读取 `Building Gameplay Root` 功能分支；外部美术组件即便勾选允许影响导航也不参与生成。棋盘建筑统一继承，主塔模型使用同一父类。功能分支仍保留各组件自身的导航和碰撞开关，不会将触发盒强制变成导航表面。详见 [GameplayRoot 配置与验收](../Animation/GameplayRoot.md)。
- 当前服务使用默认的一种导航 Agent，默认半径 35 cm，与 NPC 基础配置一致。不同尺寸 NPC 或专用跳跃/攀爬行为需要后续扩展。
- 默认/高精度 NavMesh 水平体素使用 **5 cm**，以支持 GameplayRoot 下 100×100 cm 的功能楼板。更粗的体素与 Agent 半径侵蚀可能把这种小顶面过滤掉。低精度仍为 38 cm，仅供显式选择低精度的区域使用；更细体素会增加单次构建的计算与内存开销。此设置与共享路线的 20 cm 表面采样间距不同。
- **棋盘显示地块始终不参与单位导航**：它只提供 Visibility 射线查询，不阻挡 Pawn，也不会在移除建筑后恢复为导航面。可行走表面需要由真实的功能楼板、楼梯等提供；能放置草稿点不代表该位置可导航。
- 已有 Recast Actor 如果显式覆盖了项目默认值，也应同步 `Nav Mesh Resolution Params`：索引 1（Default）和 2（High）的 `Cell Size` 均为 5；`Cell Height=10`、`Agent Max Step Height=35`。只有被 Bounds 覆盖、具备可行走碰撞且净空足够的表面才会生成导航，挂到 GameplayRoot 并不意味着触发盒也自动变成可行走楼板。

### 100 cm Cube 的复现与验收

`TestMap1` 中用卡牌正常放置 `BP_ArcherTower_lv1`，其 `GameplayRoot/Cube` 保持有效可行走碰撞、阻挡 Pawn、允许影响导航。运行时在控制台输入 `show Navigation`，等飞入动画全部结束，功能 Cube 顶面应出现小块绿色网格。顶面以当前蓝图的实际组件 Bounds 为准，不能把曾用过的 Actor 原点或旧高度写死为判断依据；10 cm 的垂直体素可能使导航高度与实际顶面存在误差。

输出日志筛选 `NPC 导航`，单次放置只应在飞入结束后增加一轮开始/完成日志；等待数秒不应继续刷新。两座相同建筑触发自动合并时，须等飞入和合并整轮结束才刷新；合并结果的可行走表面由结果等级自身的 GameplayRoot 碰撞提供，不继承被销毁旧建筑的 Cube。

## 更新生命周期

`UCastleNavigationSubsystem` 仅存在于 Game / PIE 世界。

1. 棋盘在生成、替换或删除几何前，通过 `FScopedCastleNavigationChange` 开始变化；第一项变化加上 UE 导航构建锁，并发布失效的路径快照。
2. 原有 `UAssemblyFlightComponent` 与 `UBuildingMergeComponent` 起播时登记自己，直接消费完成通知。合并增加了与飞入一致的原生结束通知，覆盖完成、Reset 和 EndPlay。
3. 同一来源可嵌套，多座建筑可并发。每个开始必须与一个结束配对；结束本身只排队。最后一个结束后的下一帧才提交，让“飞入结束→立即自动合并”的原有回调完整执行。
4. 将最终几何同步到导航八叉树，解锁，并为默认导航数据提交一次全局重建。当前每轮采用全局重建，范围和几何复杂度决定构建开销；没有实现局部 Tile 调度。
5. 等待引擎的脏区队列和异步生成任务全部完成，递增 `NavigationRevision`；有巡逻请求时一次求解整组相邻路段与闭合段，全部成功后统一发布。NPC 在失效期间停速，不发起单独重算。

稳定场景不 Tick 导航子系统，也不逐帧扫描建筑或动画。已有异步构建期间若再次开始建造，会取消旧构建，等待新一轮变化结束后再提交，避免发布过时结果。

合并临时件沿用原有的无碰撞/不影响导航设置；初始化顺序已修正为先设置这些属性、再赋 Static Mesh，避免 UE 5.8 在尚未注册的组件上提前留下旧位置导航几何。没有增加额外导航模型。

不经过原有棋盘/动画的几何修改，应在修改期间配对 `BeginGeometryChange` / `EndGeometryChange`，或使用栈作用域。只有最终完成通知的外部生成器也可以在完成后调用 `RequestNavigationRefresh`；多次刷新请求会合并，但要抑制生成过程中的更新，应使用开始/结束配对。

## 共享路径接口

蓝图入口：**Get World Subsystem → CastleNavigationSubsystem**。核心逻辑全部在 C++。

| 接口 | 用途 |
| --- | --- |
| `RequestSharedPath(Start, Goal, Failure)` | 设置全世界唯一一条路线的起终点；相同起终点复用已接收的请求。返回 true 表示请求被接收，构建期间可能尚无结果。 |
| `RequestSharedPatrolRoute(Waypoints, Failure)` | 提交完整有序点列并替换全局请求，一次求解整个闭环；同一组点复用已接收请求。一个点有效，零个点拒绝。 |
| `ClearSharedRoute()` | 清除请求并立即发布失效快照，所有巡逻 NPC 停止。 |
| `AcquireSharedPath()` | C++ 获取同一个线程安全、只读共享快照；当前 Mass Processor 每轮取得一次供整批实体消费。 |
| `GetSharedPath()` | 蓝图/少量调试读取副本，不适合逐 NPC 高频调用。 |
| `OnSharedPathChanged` | 当前路线失效或新结果发布的事件；不要在每个 NPC 上收到事件后再次寻路。 |
| `IsNavigationReady()` | 本轮变化与网格构建均已完成。 |
| `LastFailure` | 异步构建或集中寻路失败的具体原因。 |

先在有限 `ProjectionExtent` 内投影全部路径点；巡逻闭环一次集中求解 N 个相邻路段（含 N→1），旧单段接口只查询一次，均拒绝部分路径。返回 true 仅代表请求被接收，还需检查最新快照的 `bValid/bClosedLoop` 与 `LastFailure`。使用默认导航过滤器，默认区域等价时按距离选择；添加有额外代价的 NavArea 后按区域代价选择。

快照保存世界坐标转折点、引擎点标记、长度与版本，不保存会随 Tile 重建失效的多边形引用。巡逻快照还保存玩家路径点、每段距离边界和沿最短路走廊预采样的表面高度（默认水平间距 20 cm）；台阶采样也只计算一份。`PathRevision` 每次发布（包括失效）递增；`RouteRevision` 仅点列变化时递增。`bValid=false` 表示必须暂停。旧快照不被原地修改，但持有旧快照不代表旧路线仍可行走。

`CastleMassPatrolProcessor` 每轮只取一次快照，再按 Chunk 并行计算所有 NPC 的距离、位置、朝向与速度。跨转折和大帧间隔依旧沿共享折线采样，跨越 N→1 时统计完整圈数；速度驱动原有批量待机/行走动画。`MoveUnitsTo`、`SetUnitsVelocity`、`StopUnits` 显式解除被操作单位的巡逻，避免两套 Processor 同时移动它。

此阶段是共享路线上的行走，不包含各 NPC 从任意出生点独立寻路接入、单位间避让、跳跃、传送或门交互。推荐使用沿共享路线生成。已有单位可通过 `FollowSharedPatrolRoute` 加入；它必须位于路线附近（50 cm 内，吸附到对应采样折线）。NavMesh 重建保留原路段，防止去返路重叠时跳段；若新路线已偏离单位超过 50 cm，该单位明确停止并汇总警告，不能自动传送或直线穿墙接入。重新调整点列覆盖单位，或通过上述验收命令沿新路线重新生成即可。

```cpp
UCastleNavigationSubsystem* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
FText Failure;
if (!Navigation->RequestSharedPatrolRoute(PlayerWaypoints, Failure))
{
    // 同步校验失败：在界面显示 Failure，或向上层返回错误。
    return;
}

// Mass Processor 每轮读取一次，整批 NPC 共享；各实体只保存自己的进度。
const auto Path = Navigation->AcquireSharedPath();
if (Path && Path->bValid && Path->bClosedLoop)
{
    // PatrolPoints/PatrolDistances 是整条闭环的共享贴地数据，各 NPC 只保存自己的距离。
}
```

该服务没有每 NPC 路径缓存或寻路队列。新点列替换全局唯一请求，地图变化后统一重算整条闭环。N≥2 时完整计算一轮需要 N 次原生最短路查询，与 NPC 数量、到点次数、圈数无关；这是一个完整批次，发布只有一次。默认 NavArea 代价相同时选择最短距离；自定义区域代价会改变最小代价路线的含义。

## 验证与资产脚本

- Editor 自动化：`MovingCastle.Navigation.InputValidation`，验证缺少导航时明确拒绝，不创建虚假路径或后台轮询。
- 真实 Game 世界自动化：在原导航演示地图运行 `MovingCastle.Patrol.SharedLoop`（启动参数 `-NavigationDemoAutomation`），验证全部路段原子发布、1024 个 NPC 跨转折/跨圈不新增查询、共享高度采样、NavMesh 失效暂停/恢复、单点/空点列/重合点校验与失败不发布半条路线。
- 可见运行与玩家输入自动化：在新 `L_MassPatrolDemo` 运行 `MovingCastle.PatrolDemo.Runtime`，验证 32 个实际骨骼 NPC、四段完整路线仅四次查询，以及真实输入栈的编辑/撤回/取消/清空/左键放点/Enter 提交。截图写入 `Saved/Navigation/MassPatrol.png`。
- 真实 Game 世界自动化：打开演示地图后运行 `MovingCastle.NavigationDemo.SharedPath`，验证真实飞入/合并、楼梯高度、绕行方向切换、1000 次相同请求复用、旧快照不变、取消释放构建锁、稳定后停止 Tick、失败后恢复，以及正式棋盘放置→合并→移除的刷新配对。
- 一级箭塔实际蓝图回归：运行 `MovingCastle.NavigationGameplay.BlueprintPlacement`，验证当前 `GameplayRoot/Cube` 顶面、动画完成后的单次刷新、分支外美术过滤，以及放置和移除建筑后棋盘地块始终不提供导航。
- 运行时测试输出截图到 `Saved/Navigation/InitialPath.png` 和 `Saved/Navigation/SharedPath.png`。

`CreateNavigationDemo.py` 可在 UE Python 命令行创建演示地图并配置现有 `TestMap1` 的导航范围。已有演示地图会保留；已有多个 Bounds 时明确拒绝，不会自动删除场景 Actor。

`CreateMassPatrolDemo.py` 创建新增的可见 NPC 闭环演示；已有地图保留，不修改其他关卡或基础 NPC 美术资产。

```powershell
& 'E:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'F:/Ue5 Project/Moving_Castle-feature-gameplay/Moving_Castle.uproject' `
  -run=pythonscript '-script=F:/Ue5 Project/Moving_Castle-feature-gameplay/Docs/Navigation/CreateNavigationDemo.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' -unattended -nullrhi -nosound -nop4
```
