# 实现指南：飞行敌人 Boids 避让与围塔重构

> 一次性施工细则：验收合并后删除或归档。长期设计收尾时用 `tech-design-doc` skill 写进 `Docs/Mass/技术文档-飞行敌人避让与围塔.md`（并同步修正 `Docs/Mass/技术文档-Mass单位框架.md` 中已变更的停步描述），冲突时以用户确认为准并报告。
> 基线：分支 `codex/enemy-boids-refactor`（从干净基线 `21e892439d8f7ae5eea3e03e68133f012fd06d36` 切出，并提交本指南后工作区干净）。
> 状态：用户已确认（2026-11-26）

## 1. 目标与范围

- 目标：用真正的**三维各向同性连续 Boids 合力场 + 主塔表面切向绕流与攻击态贴面滑移**，重构 Mass 飞行敌人的群体移动与围塔行为，让怪物在接近主塔时呈现流畅的群聚流动感，到达主塔时紧贴真实墙面自然铺开（横向绕四面与纵向沿塔身遵循同一套三维流动规则），彻底消除穿模、隔空挥剑、外圈死锁罚站与原地微抖。
- 本期做：
  - 为 `UCastleMainTowerComponent` / `UCastleMainTowerSubsystem` 建立基于主塔真实塔身柱体（底面 `515×515 cm`、高为当前开放层数 `× 560 cm`，随主塔旋转缩放）的表面查询契约，接入近战、激光、远程索敌与敌方追踪弹命中点。
  - 新增独立的敌人三维连续飞行解算模块，接入 `UCastleMassMovementProcessor`，统一驱动存活敌人的三维 Boids 流（趋近、连续软分离、行进同向与微凝聚、近塔三维切向绕流、塔面滑移与塔底托举）。
  - 重构攻击处理器与移动处理器的职责边界：移动处理器是普通飞行阶段唯一的速度与位置写入者；近战/激光/远程进入射程后不再硬清零速度，近战贴靠真实墙面并允许受同伴软排斥沿塔面切向（水平及上下）平滑滑移让位、边滑移边持续攻击。
  - 调整群虫成簇出场的初始三维自然分布，以及熔岩巨骸贴近塔面死亡时的墙外爆散平面转向，使出生与分裂全过程不穿入主塔。
  - 更新既有合成测试中把主塔当作零体积点的旧坐标布置，新增覆盖斜角多波群虫围塔、贴墙距离、前排滑移让位与无死锁的自动化测试，并通过带视口的 Editor MCP 实拍飞行中途与围塔态截图留证。
  - 收尾时按 `tech-design-doc` skill 编写 `Docs/Mass/技术文档-飞行敌人避让与围塔.md`，并更新 `Docs/Mass/技术文档-Mass单位框架.md` 中与之直接冲突的两处旧描述。
- 本期不做：
  - 不给每个 Mass 敌人创建 Actor、Character、AIController 或引擎物理碰撞组件（保持纯 Mass 批量模拟与骨骼实例渲染）。
  - 不把普通房间、塔侧建造面板、双门、吊索或塔顶表现气球并入敌人飞行障碍（普通房间按既有玩法允许敌人穿行）。
  - 不修改玩家追踪弹的索敌与线段球形命中逻辑，不修改四个正式敌人资产（`DA_MassEnemy_*`）、基础资产（`DA_MassEnemyBase`）、测试兵（`DA_MassMeleeTest`）或正式战斗配置资产（`DA_CombatConfig`、`DA_DifficultyCurve_Default`）的数据值。
  - 不新增或删除控制台调试命令（无需改动 `COMMAND.md`）。

## 2. 必读

按顺序读：

1. `AGENTS.md`：项目红线（中文注释、严格禁新增未授权兜底、提交规范、收尾关闭无头/托管编辑器实例）。
2. `AO-WORKER.md`：Worker 开工核对、决策升级、验证门槛与 12 项固定完成报告格式。
3. `.agents/skills/worker-lock-coordinator/SKILL.md`：通过 Agent UE Coordinator（`http://127.0.0.1:37992/mcp`）提交编译（`submit_build`）、申请与关闭 Editor MCP（`open_ue_mcp` / `cancel_ue_task`）的强制流程。
4. `Docs/UE5.8官方MCP使用指南.md`：连接协调器返回的 `mcpUrl` 调用 UE 5.8 官方 MCP 工具集与自动化测试工具的规范。
5. `.agents/skills/tech-design-doc/SKILL.md`：收尾编写长期技术文档的写法要求。
6. `Docs/Mass/技术文档-Mass单位框架.md` 与 `Docs/技术文档-主塔侧面放置.md`：确认 Mass 位移唯一所有者、目标优先级、主塔 `515×515 cm` 底座与 `560 cm` 分级层高规则。
7. 相关源码：
   - 主塔与棋盘：`Source/Moving_Castle/Castle/Components/CastleMainTowerComponent.h`、`.cpp`；`Source/Moving_Castle/Castle/Actors/CastleBoard.h`、`.cpp`（仅读主塔生成与扩层相关段落，第 215–435 行）。
   - Mass 核心与敌人：`Source/Moving_Castle/Units/Mass/CastleMassTypes.h`、`CastleMassFragments.h`、`CastleMassTraits.h`、`CastleMassTraits.cpp`、`CastleMassProcessors.h`、`CastleMassProcessors.cpp`、`CastleMassUnitSubsystem.cpp`、`CastleMassEnemySpawner.h`、`CastleMassEnemySpawner.cpp`。
   - 敌人攻击与分裂：`Source/Moving_Castle/Units/Mass/Enemies/` 下近战、激光、远程、死亡分裂处理器与对应 Fragment，以及 `Projectiles/CastleMassEnemyProjectileProcessor.cpp`。
   - 相关测试：`Source/Moving_Castle/Units/Mass/Tests/CastleMassEnemyAutomationTests.cpp`、`CastleMassConcreteEnemyTests.cpp`、`CastleMassAutomationTests.cpp`、`CastleCombatSubsystemAutomationTests.cpp`，以及 `Source/Moving_Castle/Castle/Automation/CastleWaveCombatAutomationTests.cpp`、`CastleBuildingLevelEffectsAutomationTests.cpp`。

不用读：`Castle/PCG/`、`Castle/UI/`、`Castle/Items/`、`Castle/SkyIslands/`、`Units/Navigation/`（NPC 地面/梯子共享巡逻与飞行敌人无关）、`E:\UE5SourceCode\UnrealEngine`（除非确认某个 Mass API 头文件中看不清的语义）。

## 3. 已核对的事实

以下事实均已在基线 `21e89243` 上核实，直接采用，无需重复遍历仓库：

| 事实 | 出处 |
|---|---|
| 主塔底座固定 `515 × 515 cm`（半宽 `257.5 cm`，与棋盘 `CellSize = 515.0f` 一致），单层层高 `560.0f cm`；正式塔侧关卡 Lv1～Lv4 开放 `3、5、7、9` 层，对应檐口标高 `1680、2800、3920、5040 cm`。主塔 Actor 原点位于中心格底面中心（局部 `Z = 0` 即塔底平面）。 | `Docs/技术文档-主塔侧面放置.md:7,16`，`CastleBoard.h:83,87`，`CastleBoard.cpp:266-269,408-431`，`CastleMainTowerBalloonComponent.cpp:70-76` |
| 主塔静态网格 `SM_MainTower_L01~L05` 在顶部烘焙了外挑挂环（如 `(-312, 312, ...)`）与挑檐，其网格整体 AABB 半宽约 `309.05 cm`，比实际 `257.5 cm` 塔身墙面外扩了 `51.55 cm`。因此绝不能用 `TowerBodyMesh->GetStaticMesh()->GetBoundingBox()` 当作战斗表面，否则近战怪会在白墙半米外砍空气。 | `CastleMainTowerBalloonComponent.cpp:46-58` 及实测坐标核对 |
| 四个正式敌人资产的配置数值（均继承 `DA_MassEnemyBase`）：`LavaGoliath`（巨骸，移速 `140`、`Radius=55`、模型缩放 `1.35`、近战射程 `180`、死后分裂 6 只幼骸）、`LavaPup`（幼骸，移速 `300`、`Radius=28`、模型缩放 `0.65`、激光射程 `800`）、`MiasmaWelp`（龙崽，移速 `240`、`Radius=38`、模型缩放 `1.1`、远程射程 `2200`）、`ShadowSwarm`（群虫，移速 `420`、`Radius=22`、模型缩放 `0.6`、近战射程 `130`、每波 1 簇 `6~8` 只、`ClusterSpreadRadius=120`）。 | `CastleMassConcreteEnemyTests.cpp:557-566`，`CastlePlayerController.cpp:1108-1116` |
| 正式敌人骨骼网格的模型缩放写在 `UCastleMassHumanoidVisualizationTrait::MeshOffset` 上，实体 `FTransformFragment` 默认缩放为 `1.0`（仅当 `AttributeOverrides.ScaleMultiplier != 1.0` 时缩放实体 `Transform`）。且 `SpecterSK` 因前伸长剑导致骨骼包围盒折算半径高达 `81.3 cm`（是逻辑半径 `22 cm` 的 `3.7` 倍）。因此避让半径必须使用 `FAgentRadiusFragment::Radius * Transform.GetScale3D().GetAbsMax()`，严禁读取骨骼网格包围盒。 | `CastleMassTraits.cpp`、`CastleMassConcreteEnemyTests.cpp:578-580`、`Docs/Mass/技术文档-Mass单位框架.md:105` |
| 基线中近战、激光、远程三个攻击处理器均在 `Movement` 组且 `ExecuteAfter(UCastleMassMovementProcessor)` 运行；原逻辑按主塔中心点 `GetMainTowerTargetLocation()` 判射程，并在进入射程时直接令 `Targets[It] = FCastleMassMoveTargetFragment(); Velocities[It].Value = FVector::ZeroVector;`。这既导致近战怪飞入 `257.5 cm` 塔墙内部才满射程，也会在进射程瞬间硬锁死速度。 | `CastleMassMeleeAttackProcessor.cpp:21-23,87-109`，`CastleMassEnemyLaserAttackProcessor.cpp:23-25,85,96,107-108`，`CastleMassEnemyRangedAttackProcessor.cpp:23-25,70,84,93-94` |
| 死亡分裂子实体在 `0.35 s` 抛物线爆散期间携带 `FCastleMassSpawnBurstFragment`，由 `UCastleMassSpawnBurstProcessor` 独占写入位置，到达终点帧移除该 Fragment 后交还普通移动与攻击处理器。 | `Docs/Mass/技术文档-Mass单位框架.md:55,97`，`CastleMassDeathSplitProcessor.cpp:100-113,120-175` |
| 现有四处单元测试在内存中直接 `NewObject<UCastleMainTowerComponent>` 而不经过 `ACastleBoard`，且部分旧用例按“零体积点塔”把测试怪直接放在 `(40, 0, 0)`、`(100, 0, 0)` 或与主塔同坐标处测护盾/倍率/分裂。引入真实塔身柱体后，需将这些合成测试里本意在塔外测试攻击/分裂的初始坐标移到柱体表面外侧合法位置，保留其原有业务断言。 | `CastleMassEnemyAutomationTests.cpp:48-52,120-150`，`CastleMassConcreteEnemyTests.cpp:55-58,260-305`，`CastleWaveCombatAutomationTests.cpp:43-46,166-205`，`CastleBuildingLevelEffectsAutomationTests.cpp:351-355,375-420` |
| 仓库中 `MovingCastle.Combat.Assets.DefaultConfiguration` 存在一处与本功能无关的历史资产差异：测试断言 `DA_CombatConfig` 的刷怪圆环为 `1800 / 2300 cm`，而当前正式资产保存值为 `10000 / 12000 cm`。不得擅自修改 `DA_CombatConfig` 资产，在测试汇总中单列说明即可。 | `CastleCombatAssetRuntimeTests.cpp:35-36` |

请查证（结论写进完成报告）：

- **地面棋盘模式（`GroundBoard`）下 `BP_MainTower` 的默认层高**：通过只读代码或 Editor MCP 确认非逐级扩层地图（如地面棋盘关卡）中生成的主塔网格高度是否同为 3 层（`1680 cm`）。这决定 `ACastleBoard::SpawnMainTower` 在非 `UsesProgressiveTowerLayers()` 分支传给主塔组件的柱体高度（若固定为 3 层模型则传 `3 * LayerHeight`，不能误传棋盘最大建造容量 `GridSize.Z * LayerHeight`）。

## 4. 架构与归属

| 新增 / 调整模块 | 位置 | 职责（一句话） |
|---|---|---|
| 主塔柱体几何结构（如 `FCastleTowerPrism` / `FCastleFlightBox`） | `Source/Moving_Castle/Castle/Combat/` | 表达主塔随变换同步的正交方柱体（底面中心在塔根、半宽 `X/Y`、高度 `[0, H]`），提供球心到柱体表面的有符号距离、最近表面点与外法线，以及球体步进的表面切向滑移投影（不穿入柱体且保留切向位移）。 |
| 主塔表面查询契约 | `Source/Moving_Castle/Castle/Components/CastleMainTowerComponent.h`、`.cpp` | 在主塔组件保存并校验柱体底面边长与高度（由 `ACastleBoard` 在生成与扩层时同步），通过 `UCastleMainTowerSubsystem` 向 Mass 处理器提供存活主塔的柱体快照与表面攻击距离查询；保留原 `GetMainTowerTargetLocation()` 供刷怪圆环与导演继续取塔心。 |
| 敌人三维 Boids 飞行解算 | `Source/Moving_Castle/Units/Mass/Enemies/Flight/` | 纯值类型的批量三维连续转向力场与表面滑移积分器：收集当帧存活敌人的球心、半径、速度、运动参数与目标意图，用三维空间网格统一计算趋近、连续软分离、同向、微凝聚、近塔三维切向绕流、攻击态贴面切向滑移与塔底托举。 |
| 移动与攻击处理器协同状态 | `CastleMassFragments.h`、`CastleMassProcessors.cpp` 及三类敌方攻击处理器 | 将攻击处理器的输出收敛为“目标与射程意图”（含目标点、是否追击主塔、理想停靠净距、是否已进射程及朝向目标面），由 `UCastleMassMovementProcessor` 独占积分所有非爆散敌人的速度与位置。 |
| 验收与单元回归测试 | `Source/Moving_Castle/Units/Mass/Tests/` | 验证三维 Boids 分离与同向流动、多角度（含对角与 `210°` 斜角）多波群虫围塔无死锁、近战贴墙净距、前排受推挤沿塔面切向滑移让位、旋转/扩层塔身防穿模及贴塔分裂。 |

- **依赖方向**：`Units/Mass/` 依赖 `Castle/Combat/` 与 `CastleMainTowerComponent`；`Castle/` 不依赖 `Units/Mass/Enemies/Flight/`。
- **明确不新增（被否决的做法，严禁重蹈覆辙）**：
  1. **不新增静态/半静态塔面车位预约（如 `ReserveTowerStands`、`TowerStand`）**：车位预约加直线寻位会导致跨转角穿障死锁，也彻底破坏流式涌动感。
  2. **不新增离散候选方向枚举与 `ZeroVector` 代价竞价（如 11 方向射线采样）**：离散选角会产生折线跳变，且在两球凹口处会算出“原地不动代价最低”而永久卡死。
  3. **不新增读取骨骼网格包围盒的 `FCastleEnemyFlightBody`**：直接复用 Mass 已有的 `FAgentRadiusFragment`（来自 `UCastleMassUnitTrait::Radius`）乘实体变换缩放，避免长剑和翅膀把避让球撑大数倍。
  4. **不新增锁定来袭高度（`TowerApproachHeight`）或垂直换层惩罚**：纵向与横向必须完全同权自然流动。

## 5. 不变量与禁区

1. **位移唯一所有者不变量**：携带 `FCastleMassSpawnBurstFragment` 的实体只由 `UCastleMassSpawnBurstProcessor` 写位置；其余存活敌人的位置与速度只由 `UCastleMassMovementProcessor` 写（攻击处理器在攻击态仅可旋转水平朝向角面向受击点，不得清零 `FMassVelocityFragment` 或在移动处理器之外改写实体坐标）。违反后果：前排瞬间急停钉死成路障，或一帧被推进两次。
2. **同帧更新对称性不变量**：`UCastleMassMovementProcessor` 解算敌人群体时，必须先基于当帧同一时刻的位置与速度快照算出全部个体的新速度与新位置，再统一提交，不得边遍历边覆写供后续邻居读取的位置。违反后果：Entity Index 偏小的个体单向挤压偏大个体，产生非对称偏航与抖动。
3. **NPC 与弹体隔离不变量**：普通/巡逻 NPC 继续走 `UCastleMassMovementProcessor` 原有并行人形移动分支，玩家追踪弹与普通弹体继续走各自弹道与 `FAgentRadiusFragment` 命中判定，不得因敌人 Boids 改动影响 NPC 巡逻或降低玩家子弹命中率。违反后果：破坏已有建筑触发、NPC 导航或塔防战斗平衡。
4. **刷怪圆环中心不变**：`ACastleMassEnemySpawner` 与战斗波次调度继续以 `GetMainTowerTargetLocation()`（主塔中心轴锚点）作为刷怪圆环圆心，只改攻击与飞行的目标表面。违反后果：刷怪圆环整体偏向某一侧。
5. **禁区**：不修改 `AGENTS.md`、`AO-ORCHESTRATOR.md`、`AO-WORKER.md`、`COMMAND.md`；不修改任何 `.uasset` / `.umap` 二进制资产（尤其是 `DA_CombatConfig`、`DA_MassEnemy_*`、`MainMap`）；严禁绕开 `worker-lock-coordinator` 直接运行 UBT、`Build.bat` 或私自启停 Unreal Editor。

## 6. 决策表

| # | 场景 | 行为 | 来源 | 日志 |
|---|---|---|---|---|
| D1 | 群怪围攻主塔时的横向绕行与纵向（上下）分流关系 | **横向与纵向采用完全相同的三维向量规则**：不记录来袭高度、不划分固定楼层带、不给垂直方向加额外惩罚；三维连续分离力与近塔切向绕流力在主塔表面三维切空间内各向同性作用，使群怪像水流一样同时向左右相邻塔面和塔身上下自然铺开（高度受塔底平面 `Local.Z >= 0` 与塔顶檐高约束）。 | 用户确认："类似于自然流动，纵向应该和横向同一个规则" | 无 |
| D2 | 近战敌人进入攻击范围后的移动与让位行为 | 起手条件为球心到主塔柱体表面净距 `<= Max(AttackRange, R + 20 cm)` 且不穿入塔内；进入射程后法向速度平滑收敛至贴墙理想净距 `MeleeIdealStandOff`（锁住法向贴靠距），**但切向保持开放阻尼滑移**：受后排或侧方同伴的连续软排斥力推动时，沿塔面（水平及上下）平滑滑移腾出空位，滑移期间持续面向塔面受击点并正常累计攻击冷却与伤害，不中断攻击态。 | 用户确认："起手后锁定塔面法向贴靠距，但允许受同伴软排斥力沿塔面切向平滑滑移让位，边滑移边朝塔面持续攻击" | 无 |
| D3 | 主塔战斗表面与敌人避让体积的几何来源 | 主塔表面按实际塔身正交方柱体（局部底面中心 `(0,0,0)`、底面 `515×515 cm`、高为当前层数 `× 560 cm`，随主塔 Actor 旋转与缩放）计算，忽略屋檐与烘焙挂环外挑；敌人避让与贴面球半径 `R` 取 `FAgentRadiusFragment::Radius * Transform.GetScale3D().GetAbsMax()`，球心在实体原点沿主塔上方向（无主塔时为 `+Z`）偏移 `+R` 处，不读取骨骼网格包围盒。 | 用户确认："主塔按实际塔身柱体（底面 515×515 cm、高为当前层数×560 cm，忽略屋檐外挑）判定贴面；单位避让用配置的逻辑半径随实例缩放" | 无 |
| D4 | 飞行途中与近塔区域的 Boids 连续流动与绕障 | 远途追击时合成「目标趋近 + 连续三维软分离 + 同向对齐（Alignment） + 微弱凝聚（Cohesion）」，使成簇群虫呈流束飞行；进入近塔绕流区（距塔面 `<= TowerFlowMargin`）时，同向与凝聚平滑衰减至零，若朝塔前方锥区内存在更靠近主塔的同伴阻挡，则沿阻挡质心在塔面切平面的反向投影施加连续三维切向绕流力（完全共线对称时用基于 `Entity.Index` 的固定三维切向单位向量破除对称死点，严禁依赖当前速度符号翻转），引导外侧单位顺滑绕向相邻塔面或上下空区。 | 用户确认："给怪物做类似于boids的算法，有点像星际争霸2的单位行为。也就是不靠碰撞，但是还能做到流畅的避免相互穿模（以及主塔）"、"为什么没有boids的流动感，且这个一直在外侧的单位为什么没有自然地接近主塔" | 无 |
| D5 | 主塔柱体防穿模、低帧率步进与塔底防钻地边界 | 移动积分按 `MaxSubstepSeconds` 拆分子步；每子步若路径或终点使球心进入主塔柱体外扩 `R` 以内，将球心投影回柱体外表面净距 `R` 处并仅消去指向柱体内部的法向速度分量，**完整保留切向速度实现贴面/绕角滑行**（绝不整步拒绝速度导致卡死）；追塔期间实体原点在主塔局部空间的 `Z` 受塔底平面 `Local.Z >= 0` 柔和托举与下界约束，防止压入地面棋盘以下。 | 用户确认："同意：第一圈满员时外圈在连续合力下自然贴靠前排外侧待命，且追塔高度受塔底平面下界柔和托举防钻地（正常物理边界，不静默丢弃单位）" | 无 |
| D6 | 主塔表面第一圈完全饱和时的外圈单位行为 | 外圈单位在「趋塔引力 + 前排连续软排斥力 + 切向绕流力」的三维动态平衡下自然贴靠在前排外侧涌动待命，不报 Error、不把移动目标硬锁死为自身当前坐标；球心未进入实际 `AttackRange` 的外圈单位不结算伤害，前排出现伤亡空缺时后排由连续合力自动顺滑补入。 | 用户确认（同 D5 原话） | 无 |
| D7 | 远程（瘴翼龙崽）与持续激光（熔岩幼骸）的停靠与避让 | 主塔距离同样按柱体最近表面量取（敌方追踪弹与激光终点落在主塔最近表面点上）；理想停靠净距设为射程内侧缓冲位 `RangedIdealStandOff`，到达后法向平滑停稳，同时保留同伴间的三维连续软分离力使同批远程/激光怪自然散开不叠模，且法向受射程上界约束避免被同伴推出射程反复断火。 | 设计推导：统一三类敌人的表面量取与防叠模表现，防止激光因跨射程边界反复重置引导 | 无 |
| D8 | 群虫成簇出生与熔岩巨骸贴塔死亡分裂 | `ACastleMassEnemySpawner` 在 `MinClusterCount = MaxClusterCount = 1` 时保持精确原高度；多成员簇（如群虫 `6~8` 只）在 `ClusterSpreadRadius` 水平圆内采用均匀打散相位并附带不超过 `±R` 的微弱高度错落（不排成相隔百余厘米的上下刚性双层），初始轻微接近由首几帧连续软分离自然舒展。`UCastleMassDeathSplitProcessor` 在母体距主塔柱体过近（水平爆散圆环或弧顶会进入柱体外扩 `ChildRadius` 内）时，将爆散圆环所在平面与抛物线弧高方向转向主塔该处外法线并把起点推出墙外 `ChildRadius + 2 cm`（同时保证落点局部 `Z >= 0`）；普通飞行敌人在邻域查询中感知爆散中幼骸的实时位置并柔和避让。 | 设计推导：消除成簇机械分层感，并保证巨骸贴墙被击杀时 6 只幼骸不穿进塔内 | 分裂保持既有 Display |
| D9 | 目标切换（气球 > 护盾 > 主塔）与主塔扩层升级 | 支援气球或激活护盾生效时，敌人改朝气球或护盾目标点飞行（期间仍保持个体间三维 Boids 软分离，若途经主塔仍受主塔柱体滑移防穿约束）；护盾破灭或气球消失后当帧无缝切回主塔表面追击。主塔调用 `ExpandMainTowerLayers` 扩层时同步更新柱体高度，已在场敌人下一帧自动感知新高度，无需重置状态。 | 设计推导：保持既有仇恨优先级与扩层实时性 | 无 |
| D10 | 目标缺失，或主塔柱体尺寸/单位半径/分裂参数非法 | 世界无气球、无合格护盾且无存活主塔时，沿用既有规则输出单次 `Error` 并令敌人停止移动、退出攻击态。主塔柱体宽高 `<= 0` 或含 NaN、单位 `Radius < 0` 或含 NaN、分裂参数越界时，输出指明对象与原因的 `Error` 并返回失败，严禁回退默认尺寸或静默跳过。 | 设计推导及 `AGENTS.md` 严格无兜底规则 | Error |

需要用户确认的分支（兜底、默认值、静默过滤、自动修正等）：
- D5（塔底平面 `Local.Z >= 0` 防钻地高度下界约束）与 D6（塔面饱和时外圈物理平衡待命）：**用户已明确同意**（2026-11-26 问答确认）。除此之外无任何新增兜底、默认替代或静默过滤分支。

边界核对清单中不处理的项：
- **普通房间与城墙建筑的飞行绕障**：不处理，原因是项目玩法明确规定飞行敌人无视普通房间直接飞越，只有主塔塔身是飞行禁区（见 `Docs/技术文档-主塔侧面放置.md` 与既有设计）。
- **跨关卡持久化敌人运动中间态**：不处理，原因是战斗波次与 Mass 实体均为单局瞬态运行数据（见 `Docs/Mass/技术文档-Mass单位框架.md:47`）。

## 7. 数据与配置

以下参数均须有明确初值与合法范围校验（可定义在主塔组件与飞行解算配置中，不修改二进制 `.uasset`）：

| 字段 / 参数 | 含义 | 初值 | 合法范围 | 非法时 |
|---|---|---|---|---|
| `TowerShaftWidth` | 主塔战斗柱体底面局部 X/Y 边长（厘米） | `515.0f` | `(1.0, 100000.0]` 有限数 | `UE_LOG` Error 指明主塔路径并拒绝初始化/几何查询 |
| `TowerShaftHeight` | 主塔战斗柱体局部 Z 高度（厘米，由棋盘按层数 `× 560` 同步） | `1680.0f` | `(1.0, 100000.0]` 有限数 | `UE_LOG` Error 指明主塔路径并拒绝初始化/几何查询 |
| `MaxSubstepSeconds` | 飞行移动积分最大子步时长（秒） | `1.0 / 30.0` | `(0.001, 0.05]` | 编译期常量或校验拒绝 |
| `SeparationShellScale` | 连续三维软分离感知距离相对两球半径和 `(R_i + R_j)` 的倍数 | `2.6` | `[1.5, 5.0]` | 编译期常量或校验拒绝 |
| `FlockShellScale` | 行进中同向（Alignment）与微凝聚（Cohesion）感知距离相对 `(R_i + R_j)` 的倍数 | `4.5` | `[2.0, 8.0]` | 编译期常量或校验拒绝 |
| `AlignmentWeight` / `CohesionWeight` | 远途飞行时同向速度对齐权重 / 邻域质心微凝聚权重（近塔区内平滑衰减至 `0`） | `0.35` / `0.12` | `[0.0, 2.0]` | 编译期常量或校验拒绝 |
| `TowerFlowMargin` / `TangentialFlowWeight` | 主塔表面外侧触发三维切向绕流引导的距离（厘米） / 切向绕流权重 | `260.0` / `0.90` | `(0.0, 2000.0]` / `[0.0, 3.0]` | 编译期常量或校验拒绝 |
| `MeleeIdealStandOff` | 近战兵球心相对主塔柱体表面的理想贴靠净距公式 | `Min(R + 18.0, Max(R + 4.0, AttackRange * 0.7))`（群虫为 `40.0 cm`，巨骸为 `73.0 cm`） | `[R + 2.0, AttackRange]` | 若 `AttackRange <= 0` 按既有 Trait 校验拒绝模板 |
| `RangedIdealStandOff` | 远程与激光兵球心相对主塔表面的理想停靠净距公式 | `Max(R + 20.0, AttackRange - 40.0)` | `[R + 2.0, Max(R + 20.0, AttackRange)]` | 若 `AttackRange <= 0` 按既有 Trait 校验拒绝模板 |
| `AttackSlideFactor` | 已进射程单位受同伴三维软分离推动时的表面切向滑移响应系数 | `0.85` | `(0.0, 1.5]` | 编译期常量或校验拒绝 |

## 8. 验收场景

### 场景 1：多波幽影群虫斜角流式接近与三维自然围塔（核心视觉与手感验收）
- **场景**：打开 `/Game/MovingCastle/Maps/MainMap`（以及含地面棋盘格的测试场景或斜侧高处观察机位）进入 PIE。
- **初始布置**：主塔存活（Lv1 三层，`515 × 515 × 1680 cm`），无阻挡护盾。
- **步骤与期望**：
  1. 在控制台输入 `CastleSpawnEnemy ShadowSwarm 3`（一次刷出 3 波共 `18~24` 只幽影群虫）→ 远途飞来时每簇群虫呈现连贯的流束弧线，无上下双层刚性体操队平移感，两簇交汇时柔和错开。
  2. 观察群虫抵达主塔表面过程 → 前排紧贴主塔平整白墙（不再隔着半格地砖砍空气，身体也不穿入墙内）；随着后续群虫涌入，已在挥剑的前排沿墙面顺滑向左右及上下滑移腾位，外侧单位自然绕过塔角流向相邻空塔面并沿高度铺开，**外圈绝无呆立不动或原地抽搐微抖的单位**。
  3. 在控制台输入 `CastleSpawnEnemy LavaGoliath 2`，待巨骸贴靠主塔墙面后输入 `CastleKillAllEnemies` → 巨骸贴墙死亡时，6 只幼骸朝墙外半空间展开抛物线爆散，落地全在主塔外侧并随即开启持续激光。
- **必须成立的数值关系**：
  - 群虫有效半径 `R = 22 cm`；飞行与围塔全过程球心距 `515×515×H` 塔身柱体表面 `>= 21.5 cm`（实体原点在主塔局部空间 `Z >= -0.5 cm`）；
  - 稳定围塔攻击态下，前排群虫球心距柱体表面落在 `[22.0, 75.0] cm` 紧贴区间内（彻底消除旧版 `129 + 51.5 = 180.5 cm` 隔空挥剑）；
  - 稳定围塔阶段任意两只存活敌人的球心三维距离 `>= 0.85 * (R_i + R_j)`（对两只群虫即 `>= 37.4 cm`，无肉眼叠模，同时中心距集中在 `44 ~ 95 cm` 的紧凑区间，彻底消除旧版 `> 164 cm` 的巨球空隙）。
- **自动化测试必须证明**：
  1. **斜角拥挤围塔无死锁（必含 `210°` 斜对塔角与 `180°` 正对塔面双工况，不少于 `18` 只群虫同时从同一侧来袭）**：在 `15 秒` 游戏时间内，全部 `18+` 只群虫均进入各自 `AttackRange` 并各自完成至少 `2` 次有效攻击，且分布在主塔多个表面或不同高度（证明横向绕角与纵向铺开均实际生效，零死锁单位）。
  2. **前排攻击态切向滑移让位**：先让 1 只群虫在塔面正中心进入稳定近战攻击，随后从其正后方放出一簇群虫追塔；证明先到的那只前排在**不中断攻击状态且持续造成伤害**的前提下，沿塔面发生了 `> 15 cm` 的切向滑移位移以腾出空间。
  3. **Boids 连续流动平滑性（无折线横跳与极限环）**：两股迎面交叉飞行的群虫及斜向绕塔的后排群虫，在接近阶段速度方向连续变化，不存在连续 4 帧位移净和趋零却反复正反切换横移速度的原地极限环振荡。
  4. **旋转/扩层主塔防穿隧与贴塔分裂**：在低帧率大步长（如 `0.2 s` 单帧）及主塔带旋转角、扩层至 `5/7/9` 层条件下，高速敌人不会一步穿入柱体；巨骸贴墙分裂的 6 只幼骸全轨迹位于柱体外侧；护盾吸引与破盾转火主塔行为保持正确。
- **强制视觉截图验证（不可省略）**：
  - 通过 `worker-lock-coordinator` 申请**非无头 Editor MCP（`headless: false`）**，在 PIE 中将视口相机摆在能同时看清主塔两个相邻立面与来袭方向的斜侧俯视位，实拍并保存至少 **2 张高分辨率截图**到 `Saved/Screenshots/`（一张为多波群虫**飞行接近与分流中途**，一张为**围塔紧贴攻击稳定态**），用图像查看工具亲自核验画面无隔空挥剑、无外侧罚站单位、无穿墙叠模，并在完成报告中给出截图绝对路径。

## 9. 分期与提交点

按可独立通过构建的清晰提交推进（均在本地提交，严禁推送远端）：

| 期 | 内容 | 现有行为是否变化 | 提交 |
|---|---|---|---|
| 1 | 建立主塔正交方柱体几何与 `UCastleMainTowerComponent` / `UCastleMainTowerSubsystem` 表面查询契约，在 `ACastleBoard` 生成与扩层时同步真实柱体尺寸 | 否（仅新增几何与查询接口及单元校验） | `feat: 建立主塔真实塔身柱体表面契约` |
| 2 | 实现三维各向同性 Boids 飞行与塔面切向滑移模块，重构 `UCastleMassMovementProcessor` 与近战/激光/远程/敌方弹/群虫出场/贴塔分裂的协同逻辑 | 是：敌人改为连续 Boids 流式避让、贴墙攻击并允许沿塔面切向滑移让位 | `feat: 实现飞行敌人三维Boids流动与贴面滑移围塔` |
| 3 | 更新受零体积点塔假设影响的 4 处既有合成测试坐标，新增 Boids 流动、斜角多波围塔无死锁与贴面让位自动化测试，完成全量回归与非无头 PIE 实拍截图核验 | 否（测试与验证） | `test: 覆盖三维Boids流式围塔与贴面滑移回归` |
| 4 | 按 `tech-design-doc` skill 编写 `Docs/Mass/技术文档-飞行敌人避让与围塔.md` 并同步更新 `Docs/Mass/技术文档-Mass单位框架.md` 中两处旧停步描述 | 否（文档） | `docs: 编写飞行敌人Boids避让与围塔技术文档` |

## 10. 交付

严格按 `AO-WORKER.md` 第 7 节的 **12 项固定完成报告条目**输出最终报告。本功能另外必须在报告中附带：

- **UE 编译与 Editor 协调记录**：所有构建与 Editor 启动均须通过 `.agents/skills/worker-lock-coordinator/SKILL.md` 调用 `agent-lock-coordinator` 完成，严禁命令行直跑 UBT；收尾时必须调用 `cancel_ue_task` 关闭本次申请的全部 Editor 实例并确认无残留进程。
- **实拍视觉证据路径与自检结论**：列出 `Saved/Screenshots/` 下至少 2 张实拍截图（接近分流态 + 稳定围塔态）的绝对路径，以及实测记录的稳定态群虫「距 `515×515` 塔面净距范围」与「同伴间最近球心距范围」。
