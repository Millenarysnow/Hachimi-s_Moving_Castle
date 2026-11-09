# 房屋连续飞入装配测试

测试资产为 `/Game/test/AssemblyEffect/BP_HouseAssemblyReveal`，继承 `/Game/test/BP_SketchUpHouse_Assembly`。当前 `TestMap1` 中“房屋自下而上组装特效测试”实例使用此蓝图；本实现不新建模块，也不接入正式城堡建筑逻辑。

打开 `/Game/MovingCastle/TestMap1`，选中该实例并按 `F` 聚焦，向后拉远一些以便看见四周飞入的零件，然后在运行菜单选择“模拟”。零件会按最终包围盒底部高度依次发射，沿弧线飞入并转正；后续零件会在前一件尚未落定时继续进入，形成连续堆叠建造。停止并重新模拟可以重播。

选中关卡实例，在 Details 的“飞入参数”分类可调整：

| 参数 | 默认值 | 效果 |
| --- | --- | --- |
| Flight Duration | 1.2 秒 | 每件零件的飞行时长 |
| Launch Interval | 0.08 秒 | 连续两次发射之间的时间；越小，同时飞入的零件越多 |
| Flight Distance | 750 厘米 | 房屋水平包围圆以外的额外飞入距离 |
| Flight Arc Height | 220 厘米 | 飞行中的额外抬升弧高 |
| Start Delay | 1 秒 | 开始播放后的等待时间 |

默认约 15 件同时飞行；123 件的理论总时长为 `1 + 122 × 0.08 + 1.2 = 11.96` 秒。零件按自身包围盒中心旋转，避免 SketchUp 共享原点导致的绕偏。结束帧直接恢复缓存的完整世界变换，恢复 Actor 原有碰撞开关并关闭 Tick。

同目录四个 `.dsl` 文件是 MCP 蓝图图表的中文注释源码，对应 EventGraph 以及 CacheFlightParts、SortFlightParts、UpdateFlyingPart 三个函数。修改图表后应同步更新这些源码。`CompletedPartCount`、`FlyingPartCount` 为运行时计数，可在 PIE 的实例 Details 中查看。

本次在 UE5.8 的 PIE 模拟中验证：第 4 秒已落定 23 件、同时飞行 15 件；约第 12 秒全部 123 件可见，组件相对位置、旋转和缩放均与播放前一致。蓝图编译启用了 warnings_as_errors。
