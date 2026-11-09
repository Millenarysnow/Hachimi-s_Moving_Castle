# 建筑局部变化与联动动画

这一版把合并表现改成“识别变化，再表现变化”。结果建筑不会再把全部零件拉到公共中心旋转。系统会先建立旧零件和结果零件的一对一对应，然后分别处理：

- 对应后位置、朝向和尺寸都没有变化的零件继续留在原地。
- 对应成功但位置发生变化的旧零件，沿短弧移动到结果位置。
- 新放置建筑中被复用的零件，从放置方向飞入结果位置。
- 结果中新出现、旧建筑没有对应的零件从放置方向飞入。
- 旧建筑中没有结果对应的多余零件向外离场并缩小消失。

动画的主体始终是建筑自己的 Static Mesh 组件和材质。光效不代替建筑结构；默认只显示零件的移动、飞入和消失。

## 直接观察

打开 /Game/MovingCastle/Maps/BuildingMergeDemo，点击 **Play**。演示会先显示两组相邻的一级箭塔。约两秒后同时放置两座相邻的一级箭塔：

- 下方一组升级为二级，数字为 3。
- 上方一组升级为二级，数字为 4。
- 两组变化共用同一个变化原点。
- 离原点近的变化先开始，远处变化按真实厘米距离稍后开始。
- 原地不变的零件不会被抽走；发生变化的旧零件短距离移动，新放置的零件从外侧飞入。

演示 Actor 原生协调两次相邻放置，共用同一变化原点，用于检视多处同时变化时的表现。现有游戏仍按原合成规则运行；将来一次放置引起全局 PCG 结构更新时，需要把各处结果接入这一共享原点，当前没有新增该玩法规则。

## 程序如何判断零件对应关系

运行时不读取完整网格顶点，也不使用 Mesh_001 这样的导出序号猜测身份。编辑器离线工具 Tools/BuildBuildingPartCatalog.py 会把各等级 Static Mesh 的 LOD0 几何生成目录：

1. 去掉整体平移和统一单位缩放。
2. 记录三角形表面、轴向比例和尺寸。
3. 相同表面即使重新导出后三角形重新切分，也通过表面采样校验为同一几何身份。
4. 目录只保存网格到 GeometryId 的映射；发布运行时只读取这个结果。

同一几何身份出现多次时，BuildingPartMatcher 会在同身份组内做一对一最小总代价匹配。距离和朝向共同参与代价，避免逐件贪心把一个旧零件重复分配。多出来的旧件和新件分别明确成为“移除件”和“新增件”。

如果两个外形相同但语义不同，例如两个完全相同的窗框承担不同功能，可以在 Static Mesh Component 的 Component Tags 添加：

    MergeIdentity=NorthWindow

对应的组件使用同一个身份文本后，会优先按这个语义身份匹配；同一组件上设置多个身份或空身份时会明确报错。同一角色身份可用于多个副本，它们再按距离一对一分配。没有目录项时，系统只承认完全相同的网格资产，不根据包围盒或组件顺序推测跨资产关系。几何比较容忍约最长边 1/4096 的导出误差；它不理解“这是设计上同一块砖”的语义。模型变形、整体旋转后重新烘焙或一件拆为多件时，应补身份标记或专门的对应规则。

现有箭塔目录已覆盖 675 个导入网格；一级两座到二级的 114 个结果零件全部建立了对应关系。不同等级的网格资产存在导入单位和枢轴差异时，动画会使用旧网格飞行，并在最终阶段切换回结果 Actor 的正式组件，因此不会把新旧模型错误缩放或改写作者局部变换。

## 多处变化如何联动

UBuildingMergeComponent 提供两种入口：

- StartMerge(Sources)：至少两个旧 Actor，适用于合并升级。
- StartChange(Sources)：至少一个旧 Actor，适用于 PCG 或相邻建筑变化只有一座旧建筑参与的情况。

每个结果建筑仍由自己的组件负责零件计划，但可以设置同一组参数：

| 参数 | 默认值 | 作用 |
| --- | --- | --- |
| Part Catalog | 空 | 不同网格资产之间的离线几何身份目录 |
| Keep Position Tolerance | 0.5 cm | 小于此位置/尺寸/朝向差异的对应件保持原位 |
| Duration | 1.2 秒 | 单个移动或飞入件的运动时间 |
| Lift Height | 120 cm | 局部移动和飞入弧线的抬升 |
| Flight Distance | 550 cm | 新放置件从外侧飞入的距离 |
| Arrival Direction | +X | 新件从哪一侧进入 |
| Propagation Speed | 3600 cm/s | 变化从共享原点向外传播的速度 |
| Change Origin | 关闭 | 开启后多个结果共享同一个世界空间传播原点 |
| Excluded Component Tag | MergeExclude | 排除标签底牌等辅助网格 |

棋盘有 bUseSharedVisualChangeOrigin 和 VisualChangeOrigin，合并流程会把它们传给结果建筑。以后把多种 PCG 结构变化收集为一次放置事务时，只需让这些结果使用同一个 VisualChangeOrigin，每个建筑仍只处理自己的零件，时间轴会自然形成一圈向外传播的变化。这样不会为每座建筑各自播放独立的旋转特效。

## 建筑合并中的接入

CastleBoard::TryMergePlacedBuilding 仍先完成逻辑占用索引和等级替换，然后在旧 Actor 销毁前建立局部变化计划：

- 结果建筑从所属 CastleBuildingDefinition.PartCatalog 读取几何目录。
- 新放置的建筑标记为 IncomingSourceActor，其对应零件按飞入处理。
- 其他旧建筑的对应零件只做短距离移动或保持原位。
- 计划建立并创建全部临时网格成功后才隐藏来源。
- 逻辑替换完成后统一销毁旧 Actor，避免中间等级闪现。
- 动画完成后恢复结果 Actor 的正式组件，清理所有瞬态网格并停止 Tick。

连续升级会把真正参与的旧建筑一起纳入一次计划；中间等级 Actor 只用于更新逻辑索引，不参与表现快照。后续变化消费仍在播放的结果建筑时，新的计划抓取当前显示姿态，并纳入尚未飞入的等待件，保存其起飞姿态和剩余等待；旧动画随旧 Actor 清理，避免连续操作漏件。

原有 Assembly Flight 仍用于单建筑首次放置。若同一 Actor 正在播放飞入，局部变化组件会拒绝启动并输出明确错误，避免两套动画同时改写同一组零件。

## 复用配置

所有 CastleBuildingBase 派生 Actor 原生携带 BuildingMerge 组件。普通 Actor 也可以添加它：

1. 把至少一个旧 Actor 放入 Source Actors。
2. 若要区分新放置来源，设置 Incoming Source Actor。
3. 配置 Part Catalog 或给组件加 MergeIdentity=... 标签。
4. 多个结果 Actor 使用相同的 Change Origin 和 Propagation Speed。
5. 运行时调用 StartMerge 或 StartChange；普通自动演示可以勾选 Auto Play。

运行时统计可直接查看 SourcePartCount、TargetPartCount、ReusedPartCount、UnchangedPartCount、MovedPartCount、IncomingPartCount、AddedPartCount、RemovedPartCount 和 LastError。

## 验证

Win64 Development Editor 构建通过。自动化组 MovingCastle 的 20 项测试全部通过，包含：

- 原有 AssemblyFlight 生命周期和真实拆件房屋。
- 真实箭塔跨资产几何目录和一级到二级的 114 件对应。
- 一对一最优匹配、导入单位差异、相同表面重新三角化。
- 局部变化中原位保留、短距离移动、飞入和多余件离场。
- 共享传播原点和远处变化等待。
- 连续升级、动画中继续合并、等待飞入期间再次合并、回收和失败清理。
- 原有建筑定义、背包、商店和放置逻辑。

报告位于 Saved/Tests/BuildingChangeRegressionFinal/。新增或修改网格后运行 Tools/BuildBuildingPartCatalog.py；它会在重建和一级至二级的覆盖验证通过后保存目录，并绑定 DA_ArcherTower.PartCatalog。脚本仅保存该目录与箭塔定义，不修改等级蓝图或源网格。

演示截图仍可用 -MergeDemoCapture 生成到 Saved/Tests/BuildingMergeVisual/；普通 Play 不带这个参数即可循环查看。Tools/MakeBuildingMergePreview.py 会整理为 BuildingChangePreview.gif，动图放慢播放以便观察零件移动。
