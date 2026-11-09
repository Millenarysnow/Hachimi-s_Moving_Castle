# 建筑 Data Asset

每种建筑使用一份 `CastleBuildingDefinition` Data Asset。棋盘的 `Building Definitions` 是可用建筑池，开局卡组和商店读取这份池；背包卡牌保存资产引用、等级、颜色和数字。

每个等级只配置一个 `Building Class`。有模型时使用直接继承该等级 C++ 类的建筑蓝图，没有模型时使用对应 C++ 类并显示统一彩色立方体。放置、合成以及背包和商店的动态预览都直接生成这个等级类，不再创建额外的模型 Actor。

默认定义位于 `/Game/MovingCastle/Data/Buildings/`：`DA_Road`、`DA_ArcherTower`、`DA_CannonTower`、`DA_Farm`。`BP_CastleBoard` 已配置这四份资产。

## 配置建筑与模型

1. 在内容浏览器右键，选择 **Miscellaneous → Data Asset**，类型选择 **CastleBuildingDefinition**。
2. 填写 `Display Name` 和 `Building Type`。
3. 在 `Levels` 中从一级开始连续添加等级，填写 `Level` 与 `Building Class`。例如箭塔二级选择继承 `CastleArcherTowerLevel2` 的 `BP_ArcherTower_lv2`。道路只配置一级。等级缺失、重复，以及生成类的类别或等级不匹配都会明确报错。
4. 在等级蓝图自身添加或调整 Static Mesh 组件和材质。蓝图只用于配置组件和默认参数，建筑规则由继承的 C++ 实现。拆件模型继续保留各个独立组件，不合并网格。
5. 模型的位置、旋转和缩放在蓝图组件上编辑。迁移后的箭塔使用 `BuildingMeshGroup` 统一管理原模型零件；原有根节点缩放和旧配置中的模型偏移已合入这个组件，二级箭塔原有的 `0.1` 缩放保留在这里。
6. 没有美术模型的等级把 `Building Class` 配置为相应 C++ 类，例如 `CastleCannonTowerLevel1`。这个等级会显示立方体，背包和商店显示相同立方体的动态描边预览。最终生成类本身缺失时报告配置错误。
7. 保存资产并加入目标棋盘的 `Building Definitions`。购买、升级、回收与重新放置都会保留同一份 Data Asset 引用。

单格中心间距为 700 厘米，层高为 500 厘米。正式模型不会被按格子或包围盒自动拉伸；二级建筑沿 Y 方向合成时，生成的建筑整体旋转 90°。调整蓝图默认根缩放时，运行时生成使用与根缩放叠乘的策略。

## 默认等级与合成规则

箭塔一级、二级和四级的 `Building Class` 分别直接指向 `BP_ArcherTower_lv1`、`BP_ArcherTower_lv2` 和 `BP_ArcherTower_lv4`。它们的父类分别为对应等级的 C++ 类。

`BP_ArcherTower_lv2_vertical` 和 `BP_ArcherTower_lv3` 也已改为继承对应等级 C++ 类，但没有加入默认合成映射。三级现有模型与当前平面 2×2 规则不同，继续使用用户确认的四格立方体占位。五级以及道路、炮塔、农田使用没有模型的 C++ 等级类。

合成规则保持：一级一格、二级水平两格、三级平面 2×2、四级 2×2×2；四级为自动合成上限。不同 Data Asset 的建筑不会相互合成。回收后的升级卡按对应范围重新放置，并检查全部占用格、支撑与匹配条件。

## 动态卡片预览

背包和商店直接生成 Data Asset 对应等级的建筑类，隐藏通用标签，关闭碰撞，并只让 SceneCapture 看见它的直接模型组件。相同定义和等级共用模型与捕获纹理，各张卡片独立应用自己的颜色。

描边材质为 `/Game/MovingCastle/UI/Materials/M_BuildingPreviewOutline`，根据实际几何法线的变化提取轮廓和结构折线，内部透明并带柔光。相机每秒绕模型中心旋转 14°，约 26 秒一圈，每秒捕获 20 次；不可见时停止更新，停止运行时释放。

## 验收

打开 `/Game/MovingCastle/Maps/BuildingDefinitionsTest`，点击 **Play**：

1. 底部箭塔卡片显示真实建筑的彩色描边，内部透明并缓慢旋转。按 `B`，商店使用相同效果。
2. 按 `3` 选第一张箭塔，用 WASD、空格与左 Ctrl 移动，鼠标转向。对准颜色或数字匹配的地块左键放置，场上直接生成 `BP_ArcherTower_lv1`，并显示数字和等级。
3. 再按 `3` 选第二张同属性箭塔，放在第一座的水平邻格。两座建筑合成为 `BP_ArcherTower_lv2`，模型保留蓝图的 `0.1` 缩放。沿 Y 邻格合成时，二级建筑整体旋转 90°。
4. 停止运行，打开 `BP_ArcherTower_lv2`，在 **Class Settings** 检查父类为 `CastleArcherTowerLevel2`；在组件树选择 `BuildingMeshGroup`，检查其缩放为 `0.1`。修改这个组件的缩放并保存，重新运行、合成后应看到对应尺寸变化。
5. 查看 `DA_ArcherTower` 的二级配置：仅有 `Level` 和 `Building Class`，生成类直接为上述蓝图，没有旧的独立模型类、网格或模型变换字段。

自动化测试组为 `MovingCastle.Logic`，覆盖定义校验、占位模型、直接生成建筑蓝图、蓝图组件变换保留、合成、回收、购买与重新放置。此次迁移的组件基线与运行验证报告位于 `Saved/Automation/BuildingActorMigration/`。

此次迁移验证：Win64 Development Editor 构建成功，10 项自动化测试全部通过、无测试警告；真实 PIE 的 37 项购买、放置、合成、回收与预览检查通过。5 个蓝图共 675 个模型零件的局部变换与原资产一致，二级的 `0.1` 缩放得到保留，描边预览实测旋转速度保持每秒 14°。旧模型载体源码、序列化字段、临时生成脚本与旧验证产物已清理，所有本次创建的编辑器实例均已关闭。
