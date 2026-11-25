# 主界面 HUD 与三态切换

本文说明主游戏 HUD 的三态界面布局、图标进度条映射、按钮状态归属及输入兼容边界。单局流程与战斗暂停/倍速归属见 [导演系统](技术文档-导演系统.md)，敌人总预算统计见 [战斗系统](技术文档-战斗系统.md)，建造栏选牌见 [经济与房间商店](技术文档-经济与房间商店.md)，巡逻草稿编辑见 [NPC 导航接入](Navigation/技术文档-NPC导航接入.md)。

## 目标与非目标

**目标是将主游戏 HUD 统一收敛为与美术三张草图一致的极简 WBP 架构，并保持 C++ 单一状态源。** 建造页面、路径编辑界面（按 `P` 进入）与战斗界面共享顶部常驻主塔血条和左下通用按钮，底部居中与右下动作栏随真实游戏状态即时切换。

**非目标包括不导入草图未使用的冗余图标，也不在蓝图图表中编写任何业务逻辑。** 主菜单、建造撤回/重做与路径重做的后端玩法逻辑不在本 HUD 文档范围内；左下角攻略（`?`）与设置（齿轮）按钮的业务面板分别见 [对话与新手教程](技术文档-对话与新手教程.md) 与 [操作模式与建筑拆除菜单](技术文档-操作模式与建筑拆除菜单.md)。

## 核心方案

**所有排版、锚点、尺寸和贴图由 WBP 控制，C++ 抽象父类只承担必需绑定校验、事件订阅和状态切换。** 公共 20 格图标条和图标按钮抽离为独立子 WBP 基类，避免在 `WBP_DirectorHUD` 中手写大量重复控件管理代码，同时让美术可以在 UMG 设计器内独立调整图标尺寸、间距与按钮位置。

## 结构与依赖边界

- **堡垒主题用于独立弹窗。** `CastleFortressUIPublisher` 在编辑器离线生成设置、三套领奖、结算及其他模态资产。共用的 UI 切图存放于 `UI/FortressKit`，保留源图透明留边。固定画布使用等比布局，可变长底框采用九宫格，避免边角变形。玩法层读取 WBP 绑定和按钮状态，导演继续管理领奖及结算时机。
- **公共控件基类位于 `Source/Moving_Castle/Castle/UI/Common/CastleHUDControls.h/.cpp`，对应资产位于 `Content/MovingCastle/UI/Common/`。** `UCastleIconBarSlotWidget` 与 `UCastleIconBarWidget` 承载 20 格离散图标进度条（`WBP_HeartBar`、`WBP_SkullBar`）；`UCastleHUDIconButtonWidget` 承载标准按压、含灰态禁用和双态切换三类按钮（`WBP_Btn_*`）。
- **主界面逻辑位于 `UCastleDirectorHUDWidget`（`WBP_DirectorHUD`），只读导演、战斗、主塔与巡逻入口状态。** `WBP_BuildBar`（`UCastleBuildBarWidget`）和 `WBP_EconomyHUD`（`UCastleEconomyHUDWidget`）仍由 `ACastleHUD` 独立挂载，前者订阅同一导演与路径编辑状态控制自身显隐。
- **红线：缺失必需 `BindWidget` 或贴图资源/绘制尺寸为零时，必须报错并拒绝启动或挂载。** 禁止在 C++ 中动态拼树、回退默认笔刷或静默跳过缺失槽位，否则美术资源损坏会被伪装成正常界面运行。
- **红线：HUD 控件与控制器不得维护第二份流程、暂停、倍速或编辑状态。** 战斗暂停、倍速与重开归 `UCastleDirectorSubsystem`，路径编辑态归 `ACastlePatrolRoute`；在 UI 中缓存独立开关会在快捷键与界面事件并发时产生状态分裂。

## 三态界面与布局约定

| 界面模式 | 触发条件（单一状态源） | 顶部居中 | 底部居中 | 左下角 | 右下角（`RightActionSwitcher`） |
| --- | --- | --- | --- | --- | --- |
| 建造页面（`Build`） | 导演处于 `Editing`（或无导演且未开战）且未编辑路径 | 主塔 20 心血条 | `WBP_BuildBar`（10 格） | 攻略 / 设置 / 主菜单 | 撤回 / 重做（灰态） / 商店 |
| 路径编辑（`PathEdit`） | `ACastlePatrolRoute::IsEditing()` 为真 | 主塔 20 心血条 | 留空（隐藏建造栏） | 攻略 / 设置 / 主菜单 | 撤回 / 重做（灰态） / 删除（`X`） |
| 战斗界面（`Battle`） | 导演处于 `Battle`（或无导演且战斗活跃） | 主塔 20 心血条 | 战斗 20 骷髅进度条（隐藏建造栏） | 攻略 / 设置 / 主菜单 | 暂停·继续 / 1x·2x 倍速 / 重试 |

**主塔血条固定在顶部居中，且在三个界面持续显示。** 美术战斗草图中血条与骷髅条上下画反，已按策划确认修正为血条在上、战斗骷髅条在下，原左上角 `WBP_DirectorHUD` 文本摘要与生命行一并移除，仅保留极简图形元素。

**金币 HUD 仅保留左上角紧凑徽标与余额数字。** 已移除 `WBP_EconomyHUD` 内的长快捷键提示文本，避免在 1280×720 等窄视口下与顶部居中 20 心血条重叠；顶部血条与底部骷髅条均包裹 `ScaleBox (DownOnly)` 并保留对称安全边距（顶部左右各 220 px、底部左右各 290 px）。`WBP_BuildBar` 同步采用左右各 290 px 对称边距与向下等比缩放，保证在任意分辨率下不与左下/右下按钮组遮挡。

**只导入三张设计草图中实际出现的美术贴图及对应切换态。** `T_UI_EmptyHeart`、`T_UI_EmptySkull`、左右角功能按钮及其按下/禁用/切换态（`T_UI_Resume_*`、`T_UI_Speed2x_*`）统一存放于 `Content/MovingCastle/UI/Textures/`，采用 `TEXTUREGROUP_UI`、`TC_EDITOR_ICON`、`sRGB` 且禁用 Mipmap。草图未使用的 `Empty-coin`、更多、空白与音量按钮不导入项目，避免引入无引用的冗余资产。

## 20 格图标进度条规则

**顶部主塔血条固定 20 颗心，亮心代表剩余血量比例。** `UCastleDirectorHUDWidget::ComputeFilledHearts` 要求有效且有限的生命值（`MaxHealth > 0`、`0 <= CurrentHealth <= MaxHealth`），非法参数直接输出 Error 并返回失败。满血亮 20 颗，归零亮 0 颗，其余区间取 `max(1, floor(CurrentHealth / MaxHealth * 20))`：主塔受到任意伤害即可立即看到掉格反馈，同时存活残血时至少保留 1 颗亮心，避免误判为已死亡。

**底部战斗进度条固定 20 个骷髅，亮骷髅代表已击杀进度（开战全暗 0 颗，清场全亮 20 颗）。** `UCastleCombatSubsystem::GetBattleDefeatedProgress` 维护本场战斗总敌人数预算：开战时等于当前存活数加未出场组估算数（每组取簇上下限均值）；延迟组出场时用真实生成数与原估算的差值修正总预算；若熔岩巨骸分裂或调试刷兵导致剩余数高于原预算，则同步抬高总预算，保证已击杀数始终非负且清场时精确归满。换算为 20 格时，0 击杀为 0 颗，清场为 20 颗，中途至少击杀 1 名敌人后取 `max(1, floor(Defeated / Total * 20))`。

## 按钮状态与输入兼容

**按钮表现模式按 `ECastleHUDButtonPresentationMode` 严格校验切图槽数。** `Standard` 固定 2 态（普通/按下），`WithDisabledState` 固定 3 态（普通/按下/不可用，供建造与路径的重做按钮默认展示 `T_UI_Redo_Disabled`），`TwoStateToggle` 固定 4 态（状态 A 普通·按下 / 状态 B 普通·按下，供暂停·继续与 1x·2x 倍速使用）。所有按钮关闭键盘焦点（`IsFocusable = false`），避免点击后劫持视口按键。

**`WBP_DirectorHUD` 在游玩阶段使用 `SelfHitTestInvisible`，在奖励与游戏结束模态阶段切为 `HitTestInvisible`。** 如果根节点使用 `HitTestInvisible`，鼠标可见时将无法点击任何 HUD 按钮；使用 `SelfHitTestInvisible` 则既不拦截中心准星的空白区域点击，又能在鼠标出现时响应按钮事件。

**键盘快捷键与 UI 按钮共用同一业务入口，光标模式由控制器的操作模式统一仲裁。** 建造商店沿用 `B`，路径撤回/清空沿用 `Backspace` / `Delete`（`ACastlePatrolRoute::UndoDraftPoint` / `ClearPatrolRoute`）；战斗期 `T`（暂停/继续）、`F`（1x/2x 倍速）和 `R`（战斗重试）均在 `ACastlePlayerController` 上开启 `bExecuteWhenPaused = true`，确保战斗暂停期间仍可按键恢复或操作。

## 被否决的方案

- **按真实生命值或敌人数 1:1 动态生成图标。** 主塔最大生命当前为 1000、单场敌人可达数十只且含分裂子体，1:1 排布会撑爆屏幕并带来频繁的控件树重建，因此固定 20 格比例展示。
- **保留左上角导演多行摘要和金币栏快捷键文字。** 与美术三张极简草图冲突，且长提示条在 1280×720 下会直接压住顶部居中 20 心血条。
- **在战斗暂停时强制改写 `RefreshModalInput`。** 战斗暂停保持当前操作模式的常态鼠标体验，不与奖励/结算模态混为一谈。

## 未实现的部分与扩展准入

**未实现的按钮后端包括：左下角主菜单、建造页面撤回与重做，以及路径编辑重做。** 这些按钮目前仅完成 WBP 切图与按压/禁用态表现，点击只输出日志提示；攻略（`?`）与设置（齿轮）已分别接入图文百科与操作模式切换面板。

**后续接入撤回/重做或菜单系统的准入条件：** 业务栈与页面状态必须建立在独立 C++ 子系统或控制器组件中，禁止把历史栈或弹窗状态写进 `WBP_DirectorHUD` 蓝图图表；重做按钮在对应历史栈非空时才调用 `SetActionEnabled(true)`，清空后必须切回禁用态以保持 `T_UI_Redo_Disabled` 视觉语义准确。
