# 建筑 Data Asset

每种建筑使用一份 `CastleBuildingDefinition` Data Asset。棋盘的 `Building Definitions` 是可用建筑池，开局卡组和商店都读取这份池；背包卡牌保存资产引用、等级、颜色和数字，放置与升级从同一资产读取对应等级。

默认资产位于 `/Game/MovingCastle/Data/Buildings/`：`DA_Road`、`DA_ArcherTower`、`DA_CannonTower`、`DA_Farm`。`BP_CastleBoard` 已配置这四份资产。

## 配置一个建筑

1. 在内容浏览器右键，选择 **Miscellaneous → Data Asset**，类型选择 **CastleBuildingDefinition**。
2. 填写 `Display Name` 和 `Building Type`。
3. 在 `Levels` 中从一级开始连续添加等级。每级填写 `Level` 与对应的 C++ `Building Class`，例如箭塔一级使用 `CastleArcherTowerLevel1`。道路只允许一级。等级缺失、重复或逻辑类不匹配会明确报错。
4. 每级模型二选一：单个网格填 `Static Mesh`；拆成多个零件的既有模型蓝图填 `Model Actor Class`。模型蓝图只提供组件与材质，建筑规则仍由 C++ 执行。
5. 使用 `Model Transform` 调整模型相对占用范围中心的位置、旋转与缩放。单位为厘米，默认单格中心间距为 700、层高为 500。箭塔主体仍保持原来的 500 厘米模块，屋檐与相邻建筑之间有更多空间。不会按包围盒自动拉伸正式模型；二级沿 Y 方向合成时模型自动旋转 90°。
6. 保存资产，将它加入目标棋盘的 `Building Definitions`，或修改 `BP_CastleBoard` 默认值。商店从池中生成一级商品，购买、回收与升级都会保留同一资产身份。

`Static Mesh` 和 `Model Actor Class` 都留空时，该等级显示原有彩色立方体；背包与商店预览也显示相同占位模型。填写了引用但资产加载失败时会报告错误，不改用占位模型。不要把建筑逻辑 Actor 或模型载体本身填入 `Model Actor Class`。

## 默认模型

箭塔一级、二级、四级已引用 `Model/Buildings/` 下对应模型蓝图，模型保持原始比例并配置了位置偏移。三级现有模型的形状与当前平面 2×2 合成规则不同，按用户确认暂时留空。五级及道路、炮塔、农田尚未配置模型，显示立方体。

合成仍使用当前规则：一级一格、二级水平两格、三级平面 2×2、四级 2×2×2；四级为自动合成上限。不同 Data Asset 的建筑不会相互合成。回收后的升级卡按对应范围重新放置，并检查全部占用格、支撑与匹配条件。

## 验收

打开 `/Game/MovingCastle/Maps/BuildingDefinitionsTest`，点击 **Play**：

1. 底部背包中的箭塔显示实际模型的彩色描边，内部透明并带柔光，整座建筑缓慢旋转。道路、炮塔和农田显示占位立方体的同款旋转描边。
2. 按 `3` 选第一张箭塔，用 WASD、空格与左 Ctrl 移动，鼠标转向。对准与卡牌颜色或数字匹配的地块，左键放置，场上出现一级模型并保留数字/等级标签。
3. 按 `3` 选取数组前移后的第二张同属性箭塔，放在第一座的水平邻格，自动合成为二级模型。
4. 按 `B` 打开商店，商品与背包使用同样的模型预览；购买后关闭商店，按对应数字键选择新卡再放置。
5. 停止运行，把 `DA_ArcherTower` 一级的 `Model Actor Class` 清空并保存，重新运行。背包、商店箭塔与新放置建筑同时变成立方体。恢复该引用并保存后再运行即可恢复正式模型。

自动化测试组为 `MovingCastle.Logic`。新增定义校验与真实 World 放置测试覆盖模型选择、空模型占位、升级保留定义、不同定义不误合成，以及回收后重新放置。预览按 Data Asset 和等级共享模型与纹理，各卡片独立应用自己的颜色。可见卡片使用的模型以每秒 14 度旋转，约 26 秒一圈，每秒更新 20 次；离开屏幕后停止更新，停止运行时释放。

描边材质为 `/Game/MovingCastle/UI/Materials/M_BuildingPreviewOutline`。它根据实际模型法线的变化提取轮廓和结构折线，不把三角网格的每条边都画出来，不修改场上建筑的原始材质。

本次验证：Win64 Development Editor 构建成功，9 项自动化测试通过且无测试警告；真实 PIE 完成购买、背包、正式模型放置、子 Actor 射线命中、二级合成、回收后重新放置的 19 项检查，并另行验证三级四格占位、四级八格模型、沿 Y 轴二级合成的模型旋转，以及全新启动后的卡片材质显示。报告位于 `Saved/Automation/BuildingDefinitions/`。

间距与预览调整后已重新构建，并再次通过 9 项自动化测试和上述 19 项 PIE 检查；实测相邻格中心相距 700 厘米、正式模型缩放保持 1。背包与商店相同定义和等级共用捕获，实际旋转速度与每秒 14 度一致。旋转测量及自动化报告位于 `Saved/Automation/PreviewOutline/`，停止 PIE 后已确认没有遗留预览 Actor 或未保存的项目资产。
