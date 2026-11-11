# NPC 共享导航（UE 5.8）

这一阶段实现全局 NavMesh 的更新调度与一份共享路径的集中求解。路径由建筑最终的实际碰撞生成，包含楼板、楼梯与合并后的几何；不记录建筑六面关系。玩家路径点、1→N→1 循环路线及 Mass NPC 跟随 Processor 尚未接入，现有 `MoveUnitsTo` 仍是直线移动。

## 直接验收

打开 `/Game/MovingCastle/Maps/L_NPCNavigationDemo` 并点击 Play：

1. 两段墙体同时播放原有飞入组件，屏幕 `Animations` 大于 0，`Refresh` 保持 0。
2. 全部归位后，出现绿色共享路径和黄色转折点，路径绕过墙体并沿楼梯抵达上层。此时 `Refresh=1`、`Path queries=1`。
3. 路径出现 4 秒后，自动播放原有合并组件，墙体向另一侧重组。动画期间路线失效，刷新数保持 1。
4. 合并完成后绿色路径从新开口绕行，`Refresh=2`、`Path queries=2`、`Phase=3`。静止后计数不再增长。重新 Play 可重播。

输出日志搜索 `NPC 导航` 或 `NPC 共享路线` 可查看每轮构建与寻路次数。配置错误、无法投影或不可达会报告原因，不会用旧路径或部分路径冒充成功。演示仅画路径，不生成 NPC。

`/Game/MovingCastle/TestMap1` 也已添加覆盖现有棋盘的一个全局 `NavMeshBoundsVolume`。原有放置、合并、回收和直接销毁建筑均已接入刷新调度；该地图目前没有设置 NPC 路线起终点，因此只更新网格。

## NavMesh 配置

- 关卡使用一个覆盖整个可建造范围的 `NavMeshBoundsVolume`，高度必须包含所有可行走楼层。
- Recast 的 `Runtime Generation` 使用 **Dynamic**，这是运行时增加楼板/楼梯所需的底层能力。实际生成受导航构建锁控制，飞入与合并期间不会持续生成。
- 新关卡的默认模式已写入 `DefaultEngine.ini`。已经保存过的 Recast Actor 需要检查自己的实例值；两个示例地图已经设置。
- 导航直接读取建筑实际碰撞。楼板、楼梯应有正确的可行走碰撞并阻挡 Pawn；通路宽度、净高、台阶高度需满足 Recast Agent 的半径、高度和最大台阶设置。仅扩大 Bounds 不会修正建筑碰撞。
- 所有建筑由 C++ `BuildingNavigationActor` 过滤导航资格，只读取 `Building Gameplay Root` 功能分支；外部美术组件即便勾选允许影响导航也不参与生成。棋盘建筑统一继承，主塔模型使用同一父类。功能分支仍保留各组件自身的导航和碰撞开关，不会将触发盒强制变成导航表面。详见 [GameplayRoot 配置与验收](../Animation/GameplayRoot.md)。
- 当前服务使用默认的一种导航 Agent，默认半径 35 cm，与 NPC 基础配置一致。不同尺寸 NPC 或专用跳跃/攀爬行为需要后续扩展。
- 默认/高精度水平采样使用 **5 cm**，以支持 GameplayRoot 下 100×100 cm 的功能楼板。原默认 19 cm（以及实测的 10 cm）在扣除 Agent 半径和体素边缘后会把这种小顶面完全过滤。若底层楼板与棋盘地块只相隔几厘米，Recast 的 `walkableClimb`（由 `Agent Max Step Height` 与 `Cell Height` 换算）会参与相邻高度 span 的可行走性过滤；当前表面间距与 Agent 设置组合下，实测结果是不保留上层薄板顶面。棋盘会在有相应 GameplayRoot 可行走楼板时暂时关闭该格地面的导航输入，移除/替换建筑后再恢复。触发器、分支外美术和高层组件不会触发此覆盖。低精度仍为 38 cm，仅供显式选择低精度的区域使用；更细采样会增加单次构建的计算与内存开销。
- 已有 Recast Actor 如果显式覆盖了项目默认值，也应同步 `Nav Mesh Resolution Params`：索引 1（Default）和 2（High）的 `Cell Size` 均为 5；`Cell Height=10`、`Agent Max Step Height=35`。本次重启后已确认 `TestMap1` 自动继承 5 cm 配置，无需重存关卡资产。只有被 Bounds 覆盖、具备可行走碰撞且净空足够的表面才会生成导航，挂到 GameplayRoot 并不意味着触发盒也自动变成可行走楼板。

### 100 cm Cube 的复现与验收

`TestMap1` 中用卡牌正常放置 `BP_ArcherTower_lv1`，其 `GameplayRoot/Cube` 保持引擎默认 100×100×100 cm、阻挡 Pawn、允许影响导航。运行时在控制台输入 `show Navigation`，等飞入动画全部结束，Cube 顶面应出现小块绿色网格；Actor 原点位于 Z=235 cm 时顶面为 Z=285 cm，受 10 cm 垂直采样影响，导航查询结果约为 Z=290 cm。

输出日志筛选 `NPC 导航`，单次放置只应在飞入结束后增加一轮开始/完成日志；等待数秒不应继续刷新。两座相同建筑触发自动合并时，须等飞入和合并整轮结束才刷新；合并结果的可行走表面由结果等级自身的 GameplayRoot 碰撞提供，不继承被销毁旧建筑的 Cube。

## 更新生命周期

`UCastleNavigationSubsystem` 仅存在于 Game / PIE 世界。

1. 棋盘在生成、替换或删除几何前，通过 `FScopedCastleNavigationChange` 开始变化；第一项变化加上 UE 导航构建锁，并发布失效的路径快照。
2. 原有 `UAssemblyFlightComponent` 与 `UBuildingMergeComponent` 起播时登记自己，直接消费完成通知。合并增加了与飞入一致的原生结束通知，覆盖完成、Reset 和 EndPlay。
3. 同一来源可嵌套，多座建筑可并发。每个开始必须与一个结束配对；结束本身只排队。最后一个结束后的下一帧才提交，让“飞入结束→立即自动合并”的原有回调完整执行。
4. 将最终几何同步到导航八叉树，解锁，并为默认导航数据提交一次全局重建。当前每轮采用全局重建，范围和几何复杂度决定构建开销；没有实现局部 Tile 调度。
5. 等待引擎的脏区队列和异步生成任务全部完成，递增 `NavigationRevision`；有路线请求时只统一重算一次。

稳定场景不 Tick 导航子系统，也不逐帧扫描建筑或动画。已有异步构建期间若再次开始建造，会取消旧构建，等待新一轮变化结束后再提交，避免发布过时结果。

合并临时件沿用原有的无碰撞/不影响导航设置；初始化顺序已修正为先设置这些属性、再赋 Static Mesh，避免 UE 5.8 在尚未注册的组件上提前留下旧位置导航几何。没有增加额外导航模型。

不经过原有棋盘/动画的几何修改，应在修改期间配对 `BeginGeometryChange` / `EndGeometryChange`，或使用栈作用域。只有最终完成通知的外部生成器也可以在完成后调用 `RequestNavigationRefresh`；多次刷新请求会合并，但要抑制生成过程中的更新，应使用开始/结束配对。

## 共享路径接口

蓝图入口：**Get World Subsystem → CastleNavigationSubsystem**。核心逻辑全部在 C++。

| 接口 | 用途 |
| --- | --- |
| `RequestSharedPath(Start, Goal, Failure)` | 设置全世界唯一一条路线的起终点；相同起终点复用已接收的请求。返回 true 表示请求被接收，构建期间可能尚无结果。 |
| `AcquireSharedPath()` | C++ 获取同一个线程安全、只读共享快照；供后续 Mass Processor 每批取得一次。 |
| `GetSharedPath()` | 蓝图/少量调试读取副本，不适合逐 NPC 高频调用。 |
| `OnSharedPathChanged` | 当前路线失效或新结果发布的事件；不要在每个 NPC 上收到事件后再次寻路。 |
| `IsNavigationReady()` | 本轮变化与网格构建均已完成。 |
| `LastFailure` | 异步构建或集中寻路失败的具体原因。 |

路径先在有限 `ProjectionExtent` 内投影起终点，再调用一次 UE 完整路径查询；拒绝部分路径。使用默认导航过滤器，计算 NavMesh 上的最小代价路径。默认区域等价时按距离选择路线；添加有额外代价的 NavArea 后，结果按对应区域代价选择。

快照保存世界坐标转折点、引擎点标记、长度与版本，不保存会随 Tile 重建失效的多边形引用。转折点之间不是逐台阶贴地采样，后续跟随器需处理地面高度与特殊连接；演示以绿色前景折线显示计划路线。每次发布（包括失效）均递增 `PathRevision`；`bValid=false` 表示消费者必须暂停使用路线。旧快照内容不会被原地修改，但持有旧快照不代表旧路线仍可行走。

```cpp
UCastleNavigationSubsystem* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
FText Failure;
if (!Navigation->RequestSharedPath(StartLocation, GoalLocation, Failure))
{
    // 同步校验失败：在界面显示 Failure，或向上层返回错误。
    return;
}

// 后续 Mass Processor 每轮读取一次，整批 NPC 共享；各实体只保存自己的进度。
const auto Path = Navigation->AcquireSharedPath();
if (Path && Path->bValid)
{
    // Path->Points 是路线转折点；单位跟随与台阶贴地移动在后续阶段实现。
}
```

该服务目前没有路径请求队列或每 NPC 路径缓存。新的一组起终点会替换全局唯一请求，地图变化后会按这组起终点自动重算。

## 验证与资产脚本

- Editor 自动化：`MovingCastle.Navigation.InputValidation`，验证缺少导航时明确拒绝，不创建虚假路径或后台轮询。
- 真实 Game 世界自动化：打开演示地图后运行 `MovingCastle.NavigationDemo.SharedPath`，验证真实飞入/合并、楼梯高度、绕行方向切换、1000 次相同请求复用、旧快照不变、取消释放构建锁、稳定后停止 Tick、失败后恢复，以及正式棋盘放置→合并→移除的刷新配对。
- 一级箭塔实际蓝图回归：运行 `MovingCastle.NavigationGameplay.BlueprintPlacement`，验证调整后 `GameplayRoot/Cube` 顶面、动画完成后的单次刷新、分支外美术过滤，以及移除建筑后地面导航恢复。
- 运行时测试输出截图到 `Saved/Navigation/InitialPath.png` 和 `Saved/Navigation/SharedPath.png`。

`CreateNavigationDemo.py` 可在 UE Python 命令行创建演示地图并配置现有 `TestMap1` 的导航范围。已有演示地图会保留；已有多个 Bounds 时明确拒绝，不会自动删除场景 Actor。

```powershell
& 'E:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'F:/Ue5 Project/Moving_Castle-feature-gameplay/Moving_Castle.uproject' `
  -run=pythonscript '-script=F:/Ue5 Project/Moving_Castle-feature-gameplay/Docs/Navigation/CreateNavigationDemo.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' -unattended -nullrhi -nosound -nop4
```
