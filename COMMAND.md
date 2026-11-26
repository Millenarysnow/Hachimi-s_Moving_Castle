# Moving Castle 调试命令

## 棋盘与主塔

| 可复制示例 | 参数与运行条件 | 效果与反馈 |
| --- | --- | --- |
| `CastleDamageMainTower 25` | 伤害量为有限正数，当前世界有存活的主塔 | 走正常受击流程，扣除主塔生命值并更新 HUD 和塔顶气球。可用于验证主塔毁坏和游戏结束。 |
| `CastleUpgradeMainTower 2` | 等级为 `2`～`4`，存在已登记的主塔和对应的棋盘等级配置 | 将主塔升到指定等级，开放对应建造层并补满该等级的生命与气球。屏幕显示升级结果。 |
| `CastleExpandBoard` | 有且只有一个已初始化的 `GroundBoard` 棋盘 | 沿 XY 四边各加一格，屏幕显示新尺寸和坐标范围。 |
| `CastleSetGroundHidden 1` / `CastleSetGroundHidden 0` | 有且只有一个 `GroundBoard` 棋盘，`1` 隐藏，`0` 显示 | 切换地块和数字的可见性，以及地块对 Visibility 射线的响应。 |
| `CastleBoardTestMainMapExpansion` | 开发验收。新 PIE，使用旧地面版 MainMap 配置，初始 XY 为 `(0,0)` 到 `(4,4)` | 测试隐藏期间扩张，逐圈扩到 `15×15`，检查原有地块、主塔及超限拒绝。成功日志为 `W2_MAINMAP_EXPANSION_PASS`。 |
| `CastleBoardTestMainMapOverhang` | 开发验收。新 PIE，使用旧地面版 MainMap 配置，棋盘位置为 `(0,0,1000)`，初始尺寸为 `5×5×8`，背包至少有一个空位 | 放置普通房并验证越出地面两格和叠到八层的上限。运行后可在视口检查已摆放的建筑，成功日志为 `W2_MAINMAP_OVERHANG_PASS`。 |
| `CastleBoardTestCreateNavigation` | 开发验收。编辑器世界已配置导航体积和导航系统，并且没有默认导航数据 | 创建默认导航数据，成功日志为 `W2_MAINMAP_NAV_CREATED`。命令自身不保存关卡。 |
| `CastleBoardTestRebuildNavigation` | 开发验收。当前世界已有导航系统和默认导航数据 | 完整重建现有导航数据并同步等待，在日志输出 `W2_NAV_REBUILD_SECONDS`。 |

注册位置见 [主塔全局命令](Source/Moving_Castle/Castle/Director/CastleDirectorDebugCommands.cpp)、[控制器 Exec](Source/Moving_Castle/Castle/Controllers/CastlePlayerController.h)、[棋盘命令](Source/Moving_Castle/Castle/Debug/CastleBoardDebugCommands.cpp)、[棋盘验收](Source/Moving_Castle/Castle/Debug/CastleBoardAcceptanceCommands.cpp)和[导航重建计时](Source/Moving_Castle/Castle/Automation/CastleBoardExpansionAutomationTests.cpp)。

## 战斗与敌人

| 可复制示例 | 参数与运行条件 | 效果与反馈 |
| --- | --- | --- |
| `CastleStartBattle Normal 1` | `Normal` 或 `Elite`，再填相应难度表从 1 起的表项序号。需要战斗配置和存活的主塔 | 直接启动指定表项的独立调试战斗。正式单局通过节点选择战斗，使用导演关号读取难度表。 |
| `CastleBattleStatus` | Game 或 PIE | 输出当前波次、剩余敌人数、待出场批次和击杀进度。空闲时提示当前没有战斗。 |
| `CastleUnlockEnemyTag Unlock.MiasmaWelp` | 一个非空标签，取战斗配置中突袭条目的 `RequiredUnlockTag` | 解锁标签，对后续开战时生成的突袭计划生效。 |
| `CastleSpawnEnemy LavaGoliath 1` | 类型为 `LavaGoliath`、`LavaPup`、`MiasmaWelp` 或 `ShadowSwarm`，波数为正整数，可省略，默认 `1`。需要项目玩家控制器和存活的主塔 | 在主塔外圈手动生成敌人。群虫每波 6～8 只，其余类型每波 1 只。 |
| `CastleKillAllEnemies` | Game 或 PIE | 对当前世界全部存活的 Mass 敌人结算致死伤害，输出本次处理数量。若产生分裂子代或后续波次，等它们出场后可以再次执行。 |

注册位置见 [战斗命令](Source/Moving_Castle/Castle/Combat/CastleCombatDebugCommands.cpp)和[控制器 Exec](Source/Moving_Castle/Castle/Controllers/CastlePlayerController.h)。

## NPC 巡逻与主塔出兵

| 可复制示例 | 参数与运行条件 | 效果与反馈 |
| --- | --- | --- |
| `Castle.ShowPatrolPath 1` / `Castle.ShowPatrolPath 0` | Game 或 PIE。`1` 开启，`0` 关闭 | 开关当前世界的 NPC 共享巡逻线。已发布的有效巡逻轨迹含可走路段时会画线。 |
| `CastleSpawnPatrolNPCs 32` | 数量为正整数，可省略，默认 `32`。需要项目玩家控制器、`DA_MassNPCBase` 和有效共享闭环 | 沿路线生成一批巡逻 NPC。先执行命令时会等待闭环就绪，重复执行会替换上一批调试 NPC。 |
| `CastleAddPatrolWaypoints 3` | 增减量为整数，可为负数，调整后的上限须在 `1`～`99`。当前世界需要巡逻路线 Actor | 调整草稿路点上限，屏幕显示新上限。处于路径编辑模式时，HUD 的 `剩余路径点：X / Y` 同步刷新。 |
| `CastleStartTowerDispatch` | 需要主塔出兵配置、允许编辑的状态和已提交的路点。按 `P` 进入路线编辑，放点后按 `Enter` 提交 | 开启主塔出兵会话，按配置设置 NPC 待命数量，屏幕显示自动出兵按键。 |
| `CastleEndTowerDispatch` | 已有主塔出兵会话 | 停止手动和自动派发，已出发的 NPC 继续按会话结束流程回收。 |

注册位置见 [巡逻线命令](Source/Moving_Castle/Units/Navigation/Visualization/CastlePatrolPathVisualizationSubsystem.cpp)和[控制器 Exec](Source/Moving_Castle/Castle/Controllers/CastlePlayerController.h)。

## 建筑触发与支援塔

| 可复制示例 | 参数与运行条件 | 效果与反馈 |
| --- | --- | --- |
| `Castle.BuildingTriggers.Debug 1` / `Castle.BuildingTriggers.Debug 0` | 调试 CVar。Game 或 PIE 中 `1` 开启，`0` 关闭，初始值为 `0` | 绘制建筑逻辑触发区的线框，在建筑上显示累计进入次数。可在 `/Game/MovingCastle/Maps/L_BuildingTriggerDemo` 观察。 |
| `Castle.SupportDemo Hive 3 Red` | 开发验收。新开的 `MainMap` PIE，处于构筑阶段，棋盘为空且未扩张。类型 `Hive` 或 `Balloon`，等级 `1`～`4`，颜色 `Red`、`Yellow`、`Blue`、`Green` | 放置指定等级的蜂巢或气球塔，生成演示敌人并主动触发建筑效果。可在视口观察效果，日志会给出放置位置或失败原因。 |

注册位置见 [建筑触发子系统](Source/Moving_Castle/Units/BuildingInteraction/CastleBuildingTriggerSubsystem.cpp)和[支援塔验收命令](Source/Moving_Castle/Castle/Support/Tests/CastleSupportDemoCommand.cpp)。

## 金币、背包与徽章

| 可复制示例 | 参数与运行条件 | 效果与反馈 |
| --- | --- | --- |
| `CastleAddGold 100` | 金额为非负整数，结果在 `int32` 范围内。需要项目玩家控制器 | 增加金币，屏幕显示增加金额和余额。 |
| `CastleSpendGold 100` | 金额为非负整数，且余额足够。需要项目玩家控制器 | 扣减金币，屏幕显示新余额或金币不足。 |
| `Castle.Items.UI` | 非 Shipping。需要本地玩家视口、唯一棋盘与背包、徽章子系统 | 挂载临时建筑栏、徽章栏及替换界面。正式 HUD 已包含这些控件时会在日志提示已挂载。 |
| `Castle.Items.Fill` | 非 Shipping。需要唯一棋盘、背包和普通房定义 | 用一级红色 4 号普通房填满主建筑栏，日志显示主栏及临时栏数量。 |
| `Castle.Items.Group 10` | 非 Shipping。编号 `1`～`26`，需要本地城堡玩家控制器、唯一棋盘及背包、对应房间组资产。自定义项需在没有其他模态界面时运行 | 直接发放项优先入主栏，其余进入临时栏，屏幕和日志显示结果。自定义项打开原有属性选择弹窗，确认后入包。编号、资产或发放条件不合法时输出 Error。 |
| `Castle.Items.Badge 1` | 非 Shipping。编号 `1`～`25`，需要徽章子系统及配置完整的对应资产 | 发放指定徽章，栏位满时进入替换流程。徽章栏和替换弹窗展示对应的图标与效果摘要。日志记录发放、待替换或失败原因。 |
| `Castle.Items.Remove 1` | 非 Shipping。槽位从 `1` 到 `10`，需要唯一棋盘和背包 | 移除主建筑栏指定槽位的卡，失败原因写入日志。 |
| `Castle.Items.ResetBadges` | 非 Shipping。需要当前 GameInstance 的徽章子系统 | 清空已持有徽章和待替换徽章，刷新徽章栏。 |
| `Castle.Items.Reload` | 非 Shipping。Game 或 PIE | 重开当前地图，重置关卡运行状态，可观察 GameInstance 中徽章是否继承。 |

### 房间组编号

资产目录为 `/Game/MovingCastle/Data/Items/RoomGroups/`。

| 编号 | 房间组 | 资产名 |
| ---: | --- | --- |
| 1 | 随机补给 | `DA_RoomGroup_01_RandomSupply` |
| 2 | 旺财的补缝袋 | `DA_RoomGroup_02_WangcaiPatchBag` |
| 3 | 盾后小田 | `DA_RoomGroup_03_ShieldBehindField` |
| 4 | 自定义普通房间 | `DA_RoomGroup_04_CustomNormalRoom` |
| 5 | 双胞房间 | `DA_RoomGroup_05_TwinRooms` |
| 6 | 顶格补给 | `DA_RoomGroup_06_TopSupply` |
| 7 | 三三箭塔 | `DA_RoomGroup_07_TripleThreeArchers` |
| 8 | 幸运炮塔 | `DA_RoomGroup_08_LuckyCannons` |
| 9 | 连号农田 | `DA_RoomGroup_09_SequentialFarms` |
| 10 | 高速公路 | `DA_RoomGroup_10_Highway` |
| 11 | 红色风暴 | `DA_RoomGroup_11_RedStorm` |
| 12 | 双楼梯拼图 | `DA_RoomGroup_12_DoubleStairPuzzle` |
| 13 | 三色弹药班 | `DA_RoomGroup_13_TricolorAmmoSquad` |
| 14 | 三岔分工箱 | `DA_RoomGroup_14_ThreeWayToolbox` |
| 15 | 大数字房间 | `DA_RoomGroup_15_BigNumberRoom` |
| 16 | 随机功能自定义房间 | `DA_RoomGroup_16_RandomFuncCustomRoom` |
| 17 | 加入 3 个蓝 8 | `DA_RoomGroup_17_ThreeBlueEights` |
| 18 | 蜂房接龙匣 | `DA_RoomGroup_18_HiveChainBox` |
| 19 | 完全自定义房间 | `DA_RoomGroup_19_FullyCustomRoom` |
| 20 | 归一化 | `DA_RoomGroup_20_Normalize` |
| 21 | 传送门房间 | `DA_RoomGroup_21_PortalRoom` |
| 22 | 绿肥红瘦 | `DA_RoomGroup_22_GreenLushRedWither` |
| 23 | 自环绕炮塔 | `DA_RoomGroup_23_SelfOrbitCannon` |
| 24 | 功能塔买断 | `DA_RoomGroup_24_TowerBuyout` |
| 25 | 直接部署 | `DA_RoomGroup_25_DirectDeploy` |
| 26 | 颜色转变 | `DA_RoomGroup_26_ColorShift` |

### 徽章编号

资产目录为 `/Game/MovingCastle/Data/Items/Badges/`。

| 编号 | 徽章 | 资产名 |
| ---: | --- | --- |
| 1 | 旺财的火力牌 | `DA_Badge_01_FirepowerCard` |
| 2 | 管家的零钱袋 | `DA_Badge_02_CoinPurse` |
| 3 | 充足的启动资金 | `DA_Badge_03_StartingFunds` |
| 4 | 双门打卡牌 | `DA_Badge_04_DoubleDoorPass` |
| 5 | 门后备用弦 | `DA_Badge_05_SpareBowstring` |
| 6 | 备用气球挂架 | `DA_Badge_06_BalloonRack` |
| 7 | 商店折扣 | `DA_Badge_07_ShopDiscount` |
| 8 | 商店返利 | `DA_Badge_08_ShopRebate` |
| 9 | 值班汽笛 | `DA_Badge_09_ShiftWhistle` |
| 10 | 摘绳手套 | `DA_Badge_10_StringGlove` |
| 11 | 快弦工牌 | `DA_Badge_11_QuickstringPass` |
| 12 | 翻货免单章 | `DA_Badge_12_FreeRefresh` |
| 13 | 快速梯子 | `DA_Badge_13_QuickLadder` |
| 14 | 环路采购签 | `DA_Badge_14_LoopPurchase` |
| 15 | 后手优势 | `DA_Badge_15_LateAdvantage` |
| 16 | 虹吸箭簇 | `DA_Badge_16_SiphonArrow` |
| 17 | 蓝釉算盘 | `DA_Badge_17_BlueAbacus` |
| 18 | 工坊返料章 | `DA_Badge_18_WorkshopRefund` |
| 19 | 应急压舱扣 | `DA_Badge_19_EmergencyBallast` |
| 20 | 急速行军 | `DA_Badge_20_RapidMarch` |
| 21 | 狂暴工蜂 | `DA_Badge_21_BerserkBee` |
| 22 | 连锁反应 | `DA_Badge_22_ChainReaction` |
| 23 | 破球警钟 | `DA_Badge_23_BurstAlarm` |
| 24 | 一圈调岗令 | `DA_Badge_24_LapReassignment` |
| 25 | 最后一颗气球扣 | `DA_Badge_25_LastBalloonClasp` |

注册位置见 [道具命令](Source/Moving_Castle/Castle/Items/CastleItemsDebugCommands.cpp)。

## 导演、空岛与对话

以下命令以 `MainMap` PIE 为验收入口。登岛前处于导演的构筑阶段，先关闭弹窗并处理待领取的奖励。

| 可复制示例 | 参数与运行条件 | 效果与反馈 |
| --- | --- | --- |
| `CastleEnterSkyIsland Rest` | 非 Shipping。岛类型严格使用 `Rest`、`Shop`、`NPC` 或 `Gamble`，需要正式导演配置 | 直接进入指定正式空岛，使用导演的进岛、离岛和步数推进流程。当前步可以没有空岛节点。 |
| `CastleTestSkyIsland` | 非 Shipping。当前步需要奖励或休息空岛节点 | 选中首个相应节点并进入测试空岛，验证基础进出岛流程。 |
| `CastleTestRestSkyIsland` | 非 Shipping。当前步需要休息节点 | 通过当前休息节点进入休息空岛。缺少节点时，屏幕提示推进关卡后重试。 |
| `CastleTestShopSkyIsland` | 非 Shipping。当前步需要实际对应商店空岛的奖励节点 | 通过当前商店节点进入商店空岛，可测试购买和返回。 |
| `CastleTestNPCPinballIsland` | 非 Shipping。单机 PIE，需要本地可移动 Pawn 和有效交互键 | 传送到弹珠机验收岛，瞄准投币台交互。使用离开传送门回到原位置。 |
| `CastleTestGambleSkyIsland` | 非 Shipping。PIE，需要可移动 Pawn 和有效主塔 | 传送到赌博验收岛，可测试三台机台。使用离开传送门回到原位置。 |
| `CastleResetSkyIslandDialogues` | 需要项目玩家控制器和当前 GameInstance 的对话子系统 | 清除商店、NPC、赌博三类奖励岛的首次登岛对话记录。下一次登岛可重测首次播放，重置结果写入日志。 |

注册位置见 [导演命令](Source/Moving_Castle/Castle/Director/CastleDirectorDebugCommands.cpp)、[控制器 Exec](Source/Moving_Castle/Castle/Controllers/CastlePlayerController.h)、[休息岛验收](Source/Moving_Castle/Castle/SkyIslands/Rest/CastleSkyIslandAcceptance.cpp)、[商店岛验收](Source/Moving_Castle/Castle/SkyIslands/Shop/CastleShopSkyIsland.cpp)、[弹珠机验收](Source/Moving_Castle/Castle/SkyIslands/Pinball/CastlePinballNPCAcceptance.cpp)和[赌博岛验收](Source/Moving_Castle/Castle/SkyIslands/Gamble/CastleGambleAcceptanceIsland.cpp)。

## 编辑器连接与界面焦点

| 可复制示例 | 效果 |
| --- | --- |
| `ModelContextProtocol.StartServer 9876` | 在编辑器启动官方 MCP 服务。端口可省略，项目连接配置使用 `9876`。查看 `LogModelContextProtocol` 确认状态。 |
| `ModelContextProtocol.StopServer` | 停止当前编辑器的 MCP 服务。 |
| `ModelContextProtocol.RefreshTools` | 刷新 MCP 工具列表及缓存的参数定义。 |
| `CommonUI.CheckKeyboardFocusAndParentage 1` | 设置焦点前检查键盘焦点和控件父子关系。 |
| `CommonUI.DisallowUserFocusedWidgetForPendingFocusRecipient 1` | 只有在无待接收焦点的控件时才检查当前获得焦点的控件。 |
| `CommonUI.FallbackToDesiredOnAutoRestoreFailure 1` | 自动恢复焦点失败时，将焦点设到期望控件。 |

三个 CommonUI CVar 均由项目配置设为 `1`。排查焦点时可在控制台输入变量名查询当前值，设置 `0` 可暂时关闭对应行为。见 [DefaultGame.ini](Config/DefaultGame.ini)和 [UE 5.8 官方 MCP 使用指南](Docs/UE5.8官方MCP使用指南.md)。

## 自动化与资产验收

在编辑器控制台输入 `Automation List` 可列出当前环境的测试。用 `Automation RunTests MovingCastle.Board` 运行按名称匹配的棋盘测试，也可换成完整测试名，例如 `Automation RunTests MovingCastle.Director.MainMapSmoke`。测试结果见 **Output Log** 或 **Session Frontend → Automation**。需要在游戏世界验收的测试应按测试要求加载对应地图。

以下参数加在 Unreal 启动命令行上。

| 启动参数 | 场景与结果 |
| --- | --- |
| `-NavigationDemoAutomation` | 加载 `L_NPCNavigationDemo` 时保持初始场景，关闭演示就绪后的自动合并计时，让自动化测试控制合并时机。 |
| `-MergeDemoCapture` | 运行 `BuildingMergeDemo` 地图并开启渲染。请求 25 张逐帧截图，写入 `Saved/Tests/BuildingMergeVisual/`，结束后进程退出。 |
| `-HachimisV4Capture` | 开启渲染并加载空棋盘的 `TestMap1`。运行 `MovingCastle.Castle.PCG.HachimisV4.StackInterfaces` 时捕获场景图，输出到 `Saved/HachimisV4/`。 |
| `-CastleCardScreenshots` | 使用真实 RHI 运行 `MovingCastle.Shop.CardPresentation`。测试加载 `MainMap`，把普通牌面与镭射牌面截图写入 `Saved/CardFace/`。 |

实现见 [导航演示](Source/Moving_Castle/Units/Navigation/Demo/CastleNavigationDemo.cpp)、[合并演示](Source/Moving_Castle/Components/Animation/Demo/BuildingMergeDemo.cpp)、[V4 堆叠测试](Source/Moving_Castle/Castle/PCG/HachimisV4/CastleHachimisV4NavigationTests.cpp)和[商店卡牌测试](Source/Moving_Castle/Castle/UI/Cards/Tests/CastleShopCardRenderingTests.cpp)。

在项目根目录的 PowerShell 可运行以下只读资产校验。校验通过会在 `LogHachimisV4Bind` 输出已检查的装配、蓝图和网格数量。末尾加 `-Shield` 可检查盾塔批次。见 [V4 绑定校验 Commandlet](Source/Moving_Castle/Castle/PCG/HachimisV4/HachimisV4BindCommandlet.cpp)。

```powershell
& 'E:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' (Join-Path $PWD 'Moving_Castle.uproject') -run=HachimisV4Bind -Verify -unattended -stdout
```
