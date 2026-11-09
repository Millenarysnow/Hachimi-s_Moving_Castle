# 连续飞入装配测试组件

全部效果逻辑位于 `Source/Moving_Castle/Tests/AssemblyFlight/AssemblyFlightComponent.h` 和 `.cpp`，类型为 `UAssemblyFlightComponent`。沿用项目已有模块，只新增测试目录，不接入正式城堡建筑逻辑。蓝图只提供拆好的模型组件，不承担排序、运动或发射调度。

`/Game/MovingCastle/TestMap1` 中“房屋自下而上组装特效测试”使用原始 `/Game/test/BP_SketchUpHouse_Assembly`，仅在关卡实例上挂载 `AssemblyFlight` 原生组件。打开该关卡、选中实例按 `F` 聚焦，再选择运行菜单的 **Simulate / 模拟** 即可观察。旧特效蓝图和对应四份 `.dsl` 图表注释源码已移除。

## 在其他 Actor 上复用

1. 将模型整理为同一个 Actor 自身的多个独立 `StaticMeshComponent`，保持各零件已经位于最终装配位置。
2. 选中关卡中的 Actor，在 Details 点击 **Add Component**，搜索并添加 **Assembly Flight (Test)**；也可在该 Actor 的蓝图 Components 面板添加这个原生组件，再编译并保存。
3. 保持组件 **Auto Play** 勾选。运行菜单选择 **Simulate / 模拟**，零件会从四周沿弧线连续飞入，由下向上归位。停止后再次模拟即可重播，不需要连接 EventGraph。
4. 需要排除装饰或其他网格时，在参与零件的 **Component Tags** 中添加统一标签（例如 `AssemblyPart`），再将组件的 **Part Component Tag** 填为相同标签。留空时收集所属 Actor 自身所有原本可见的静态网格。

| 参数 | 默认值 | 效果 |
| --- | --- | --- |
| Flight Duration | 1.2 秒 | 单件飞行时长 |
| Launch Interval | 0.08 秒 | 相邻零件发射间隔；减小会增加同时飞行数量 |
| Flight Distance | 750 厘米 | 模型水平包围圆之外的额外飞入距离 |
| Flight Arc Height | 220 厘米 | 飞行中的额外抬升高度，同时影响侧向弯曲 |
| Start Delay | 1 秒 | 开始后等待时间，期间参与零件隐藏 |
| Show Status On Screen | 开启 | 显示开始、完成或失败文字 |

默认稳定阶段约 15 件同时飞行。123 件房屋的理论总时长为 `1 + 122 × 0.08 + 1.2 = 11.96` 秒。零件按最终世界包围盒底部高度排序，同底部再按中心高度和组件名排序。

## 运行方式与边界

动画使用临时静态网格显示组件，围绕零件几何中心转正，支持 SketchUp 共享原点、网格根组件和嵌套组件。原零件的变换、层级、材质和 Mobility 不变；播放期间只隐藏原件并关闭该件碰撞，落定时恢复原件显隐和碰撞、销毁对应临时组件。移动或旋转所属 Actor 时，飞入路径跟随 Actor。完成后停止组件 Tick。

仅处理所属 Actor 自身的独立静态网格，不递归处理 Child Actor 内的网格；不拆分 ISM/HISM，不处理骨骼网格或正在模拟物理的零件。原本隐藏的辅助网格跳过。无可用零件、参数非法或选中不受支持的静态网格时，明确输出 `LogAssemblyFlight` Error 和屏幕红字；不会切换成其他效果。

游戏代码可以通过 `StartAssembly()` 开始或重播、`ResetAssembly()` 取消并恢复，或绑定 `OnAssemblyCompleted` 完成事件。`TotalPartCount`、`CompletedPartCount`、`FlyingPartCount`、`ElapsedTime`、`LastError` 可在运行时 Details 中检查。手动调用必须处于游戏世界；Auto Play 已覆盖普通挂载使用方式。

## 验证

`AssemblyFlightComponentTests.cpp` 包含两个引擎自动化测试：`MovingCastle.Tests.AssemblyFlight.ComponentLifecycle` 验证普通 Actor、网格根、并发飞入、移动、取消恢复、重播与非法参数；`MovingCastle.Tests.AssemblyFlight.ImportedHouse` 使用原始拆件房屋，验证全部 123 件的并发与精确归位。可在 Session Frontend 的 Automation 中搜索 `AssemblyFlight` 并运行。

UE 5.8.2 完整编辑器构建与上述两个测试均通过。真实关卡模拟中第 4 秒已归位 23 件、同时飞行 15 件；约第 12 秒全部 123 件归位，零件变换与碰撞恢复，临时网格为零。验证结果位于 `Saved/Tests/AssemblyFlightReport/` 和 `Saved/Tests/AssemblyFlightPIE.json`。
