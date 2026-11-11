# 建筑功能分支 GameplayRoot

所有 `CastleBuildingBase` 派生建筑自带一个名为 `GameplayRoot` 的原生 `Building Gameplay Root` 场景组件。把隐藏地板、简化楼梯、碰撞盒和触发区域挂到它下面，即可把这些功能组件与建筑美术零件分开。功能分支可以继续嵌套普通 Scene Component 分组，任意深度的后代均遵循同一规则，无需逐件设置排除标签。

## 行为

- 飞入动画不收集该分支中的网格，不为其生成显示副本，也不修改其显隐或碰撞。参与标签不能覆盖这一排除规则。
- 合并动画的来源、结果和连续合并都排除该分支。结果建筑的功能组件不被动画统一隐藏；旧建筑仍按原合并生命周期整体隐藏、禁用碰撞和销毁，避免旧碰撞残留。
- 建筑模型初始化保留功能分支的碰撞开关、碰撞通道、重叠事件和导航设置，不根据可见性覆盖它们。
- 功能组件不算作正式美术模型，也不扩大标签高度、卡片取景或演示相机范围。专门用于卡片的预览 Actor 会隐藏功能几何并关闭其碰撞和导航影响。
- 离线零件目录和对应关系分析工具也调用同一原生判断，不把功能网格纳入美术零件。

规则按 `UBuildingGameplayRootComponent` 类型和当前附着父级链识别，重命名根节点仍然有效。普通 Scene Component 仅改名为 `GameplayRoot` 不会获得该功能。普通 Actor 可以通过 Add Component 添加 `Building Gameplay Root`；城堡建筑直接使用已继承的节点即可。把组件移出功能分支后，它重新遵循普通美术组件规则。

## 配置与直接验收

1. 编译 C++ 后重新打开编辑器，打开需要配置的建筑等级蓝图，在 Components 面板找到继承的 `GameplayRoot`。
2. 在它下面添加一个 Scene Component，例如 `FloorGroup`；在该分组下面添加 Cube 静态网格作为观察用的功能组件。先保持可见，放在建筑侧面便于观察，并编译、保存蓝图。
3. 打开该建筑使用的游戏地图，点击 Play，通过卡牌放置建筑。建筑模型逐件飞入时，观察用 Cube 应始终留在原位且不闪隐；输出日志 `LogAssemblyFlight` 的零件总数不应因为增加它而增长。
4. 在对应的合并结果等级蓝图中也添加观察用组件。放置满足同定义、颜色、数字和相邻占位规则的两座建筑，等待飞入结束后自动合并：结果功能组件从结果 Actor 生成时就保持原位，不加入合并零件计数，旧建筑功能组件随旧 Actor 一起退出。
5. 验证完成后，把观察用 Cube 调整成所需地板尺寸，勾选 `Hidden in Game`；按实际行走需求配置有效碰撞、Pawn 通道响应和 `Can Ever Affect Navigation`。这些配置在建筑初始化后保持不变。功能分支本身不会自动生成碰撞，也不会自动选择碰撞预设或启用导航。

现有 `CastleNavigationSubsystem` 继续等待整轮建造和动画结束后刷新导航。功能组件不飞行，但不因此提前发布可用路线；本次没有改变功能碰撞的生效时机。后续 NPC 实际跟随仍需使用配置正确的碰撞与导航。

## 自动化回归

`MovingCastle.Components.Building.GameplayRoot` 下的 `Assembly`、`Merge` 和 `BuildingConfiguration` 在独立游戏世界中验证：多级嵌套、隐藏/可见组件、空网格辅助件、参与标签优先级、取消和重播、移出分支后重新参与，以及初始化和卡片预览边界。

本次 UE 5.8.2 Development Editor 构建通过，`MovingCastle` 的 30 项编辑器自动化测试全部通过，无测试警告或失败；报告位于 `Saved/Tests/GameplayRootReport/`。本轮未运行需要真实 Game 世界的 `NavigationDemo.SharedPath` 演示测试。
