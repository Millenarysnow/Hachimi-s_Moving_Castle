# 建筑功能分支 GameplayRoot

所有 `CastleBuildingBase` 派生建筑自带一个名为 `GameplayRoot` 的原生 `Building Gameplay Root` 场景组件。把隐藏地板、简化楼梯、碰撞盒和触发区域挂到它下面，即可把这些功能组件与建筑美术零件分开。功能分支可以继续嵌套普通 Scene Component 分组，任意深度的后代均遵循同一规则，无需逐件设置排除标签。

## 行为

- 所有建筑通过原生 `BuildingNavigationActor::IsComponentRelevantForNavigation` 限定导航资格：只有 `GameplayRoot` 自身及其任意深度后代可以参与；分支外的可见/隐藏美术、标签、未附着组件和非场景导航修改器一律排除。直接拖入关卡和运行时新增组件同样生效，不依赖棋盘初始化，也不依赖美术组件是否勾选 `Can Ever Affect Navigation`。
- 棋盘建筑经由 `CastleBuildingBase` 继承该规则；`BP_MainTower` 使用不添加组件或业务逻辑的 `BuildingNavigationActor` 作为父类。主塔需要功能碰撞时，可自行添加原生 `Building Gameplay Root` 分组。其他新建独立建筑模型也应使用这个父类。
- 飞入动画不收集该分支中的网格，不为其生成显示副本，也不修改其显隐或碰撞。参与标签不能覆盖这一排除规则。
- 合并动画的来源、结果和连续合并都排除该分支。结果建筑的功能组件不被动画统一隐藏；旧建筑仍按原合并生命周期整体隐藏、禁用碰撞和销毁，避免旧碰撞残留。
- 建筑模型初始化保留功能分支的碰撞开关、碰撞通道、重叠事件和导航设置，不根据可见性覆盖它们。
- 功能组件不算作正式美术模型，也不扩大标签高度、卡片取景或演示相机范围。专门用于卡片的预览 Actor 会隐藏功能几何，并保留功能组件原有的碰撞和导航设置。
- 离线零件目录和对应关系分析工具也调用同一原生判断，不把功能网格纳入美术零件。

规则按 `UBuildingGameplayRootComponent` 类型和当前附着父级链识别，重命名根节点仍然有效。普通 Scene Component 仅改名为 `GameplayRoot` 不会获得该功能。普通 Actor 可以通过 Add Component 添加 `Building Gameplay Root`；城堡建筑直接使用已继承的节点即可。把组件移出功能分支后，它重新遵循普通美术组件规则。

## 配置与直接验收

1. 编译 C++ 后重新打开编辑器，打开需要配置的建筑等级蓝图，在 Components 面板找到继承的 `GameplayRoot`。
2. 在它下面添加一个 Scene Component，例如 `FloorGroup`；在该分组下面添加 Cube 静态网格作为观察用的功能组件。先保持可见，放在建筑侧面便于观察，并编译、保存蓝图。
3. 打开该建筑使用的游戏地图，点击 Play，通过卡牌放置建筑。建筑模型逐件飞入时，观察用 Cube 应始终留在原位且不闪隐；输出日志 `LogAssemblyFlight` 的零件总数不应因为增加它而增长。
4. 在对应的合并结果等级蓝图中也添加观察用组件。放置满足同定义、颜色、数字和相邻占位规则的两座建筑，等待飞入结束后自动合并：结果功能组件从结果 Actor 生成时就保持原位，不加入合并零件计数，旧建筑功能组件随旧 Actor 一起退出。
5. 验证完成后，把观察用 Cube 调整成所需地板尺寸，勾选 `Hidden in Game`；按实际行走需求配置有效碰撞、Pawn 通道响应和 `Can Ever Affect Navigation`。这些配置在建筑初始化后保持不变。功能分支本身不会自动生成碰撞，也不会自动选择碰撞预设或启用导航。
6. 在同一建筑的 `GameplayRoot` 外再放一个 Cube，与功能 Cube 水平错开，均设置 `BlockAll`、允许影响导航，尺寸至少 300×300 cm 且顶面与下方地面相差超过 200 cm。将该建筑直接拖入带 NavMeshBoundsVolume 的关卡，开启导航显示并构建路径：只有功能 Cube 顶面应有绿色导航，美术 Cube 顶面没有。将美术 Cube 移到 `GameplayRoot` 下并编译，重新构建后它才获得资格。

现有 `CastleNavigationSubsystem` 继续等待整轮建造和动画结束后刷新导航。功能组件不飞行，但不因此提前发布可用路线；本次没有改变功能碰撞的生效时机。后续 NPC 实际跟随仍需使用配置正确的碰撞与导航。

## 自动化回归

`MovingCastle.Components.Building.GameplayRoot` 下的 `Assembly`、`Merge`、`BuildingConfiguration` 和 `NavigationBoundary` 在独立游戏世界中验证：多级嵌套、隐藏/可见组件、空网格辅助件、参与标签优先级、取消和重播、移出分支后重新参与，以及初始化和卡片预览边界。导航测试覆盖独立模型、道路、箭塔/炮塔/农田各五个等级，并验证运行时移入/移出分支、默认允许导航的美术网格、非场景导航修改器与功能组件作者设置。

真实 Game 世界测试 `MovingCastle.NavigationDemo.BuildingGeometryBoundary` 在现有楼梯演示地图中创建两组同样具有可行走碰撞的楼板，验证分支外的美术顶面没有 NavMesh、分支内的隐藏楼板生成 NavMesh，并在移除后正确清理。运行该测试时无需调用棋盘模型初始化。

UE 5.8.2 Development Editor 完整构建通过；32 项 `MovingCastle` 编辑器自动化和两项独立 Game 世界测试（`NavigationDemo.BuildingGeometryBoundary`、`NavigationDemo.SharedPath`）全部通过，无测试警告。报告位于 `Saved/Tests/BuildingNavigationEditorReport/`、`Saved/Tests/BuildingNavigationGeometryReport/` 和 `Saved/Tests/BuildingNavigationSharedPathReport/`。
