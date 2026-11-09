# 建筑零件合并动画

`UBuildingMergeComponent` 的编辑器名称为 **Building Merge**。表现围绕建筑自身的独立 Static Mesh 组件展开：旧建筑的屋檐、墙板、柱子分批松开、抬升，旋转着向共同中心汇聚；新建筑的零件从汇聚处交错展开，逐件转正、咬合到最终位置。默认整轮 3.2 秒，保留各零件原有网格和材质。

## 直接观察

在内容浏览器打开 `/Game/MovingCastle/Maps/BuildingMergeDemo`，点击 **Play**。相机会自动对准真实箭塔；约两秒后放入相邻的第二座一级箭塔，自动合并为二级。完成后停留两秒，随后重新开始，便于反复观察拆件与重组。

这个关卡中的 `MergeDemo_PlayToPreview` 是原生 `ABuildingMergeDemo`，通过正式 `CastleBoard`、`DA_ArcherTower` 和放置接口运行，没有合并用蓝图逻辑。更换它的 **Building Definition** 可以观察其他已经配置一级、二级的建筑定义。

实际游戏中，继续使用原来的建筑放置操作：将同定义、同等级、同颜色、同数字的建筑按规定形状摆在一起，合成结果会自动播放本效果。原有单建筑 `Assembly Flight` 组件仍独立使用。

## 组件复用与参数

所有 `CastleBuildingBase` 派生建筑原生携带 **BuildingMerge** 组件。可以在等级建筑蓝图的 Components 面板选择它，修改默认参数并保存；逻辑合成时直接使用最终等级的配置。普通 Actor 也可以添加 **Building Merge**，在关卡实例上指定至少两个 **Source Actors**，勾选 **Auto Play** 后运行或模拟，即可无需事件图连线地观察合并。

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| Duration | 3.2 秒 | 整轮时长；内部错开时间按比例计算，不随零件数量无限增长 |
| Lift Height | 160 厘米 | 拆件抬升与重组弧线的高度 |
| Separation Ratio | 0.16 | 零件向外松开的幅度，相对于参与建筑的整体水平半径 |
| Swirl Angle | 115° | 旧零件向中心汇聚时的旋转量 |
| Excluded Component Tag | MergeExclude | 排除不应飞行的辅助静态网格 |
| Show Status On Screen | 开启 | 显示零件数、开始、完成与失败消息 |

等级标签的底牌已标记 `MergeExclude`。结果建筑的数字与等级标签在全部零件完成后恢复，辅助标签不会作为建筑结构卷入旋流。来源建筑在组件成功抓取后隐藏并关闭碰撞；正式棋盘随后销毁旧建筑。普通 Actor 自动播放只处理表现，源 Actor 的实际消费规则仍由所属玩法管理；取消或结束运行会恢复仍存活的来源状态。

## 运行时规则

组件使用逐件瞬态网格副本，围绕每个零件的几何中心运动，适配共享原点的导入模型、网格根组件及嵌套层级。原模型的局部变换、组件层级、Mobility、材质和结果碰撞保持原样。结果 Actor 整体移动或转向时，动画路径一起跟随；每件落定后恢复原网格，动画结束清理全部副本并停止 Tick。

棋盘仍同步完成占用索引与等级替换，动画中可继续选择、回收或合并结果。一次放置连续升多级时，收集真正参与的原建筑，只重组到最终等级。新的合并如果消费了仍在动画中的建筑，就抓取它当时可见的零件副本，旧动画随旧 Actor 销毁，不留下重叠建筑或中间等级。

每个参与 Actor 须直接拥有可见、已注册、有网格、非零缩放且没有物理模拟的独立 StaticMeshComponent。ISM/HISM、Child Actor 内部的网格不属于支持对象。`Assembly Flight` 正在控制同一 Actor 时会明确报错，需先完成或停止该动画。参数或模型错误通过 `LogBuildingMerge` Error、`LastError` 及屏幕红字反馈；抓取失败会清理已创建副本，原模型保持可见。表现失败时棋盘已提交的合法合成仍保留，同时向调用方返回具体错误。

代码调用入口为 `StartMerge(Sources)`，必须在游戏世界、旧 Actor 销毁前执行；`ResetMerge()` 用于取消和恢复，`OnMergeCompleted` 在全部原件恢复后广播。运行中可查看 `SourcePartCount`、`TargetPartCount`、`CompletedPartCount`、`ElapsedTime` 和 `LastError`。

## 验证与维护

原生代码位于 `Source/Moving_Castle/Components/Animation/BuildingMergeComponent.h/.cpp`，演示 Actor 位于同目录的 `Demo/`。棋盘接入位于 `CastleBoard::TryMergePlacedBuilding`，旧建筑销毁延迟到整轮连续合成抓取完成后。

Win64 Development Editor 构建通过。自动化组 `MovingCastle` 的 16 项测试通过，包含合并组件的生命周期、输入校验、真实拆件箭塔、连续升级与动画中回收，以及原有飞入动画和建筑/经济逻辑。报告位于 `Saved/Tests/BuildingMergeRegression/`。真实箭塔测试逐一检查原零件局部变换与最终可见性；生命周期测试还验证了轨迹随所属 Actor 的俯仰、横滚和移动整体变化。

`Tools/CreateBuildingMergeDemo.py` 使用 UE 原生 Python API 生成演示关卡和展台材质；已有演示关卡时拒绝覆盖。用于全新副本时可用 UE Python 命令或 `-run=pythonscript` 运行它，脚本路径使用正斜杠。

演示 Actor 支持开发验收参数 `-MergeDemoCapture`：以 `UnrealEditor.exe <项目> /Game/MovingCastle/Maps/BuildingMergeDemo -game -RenderOffScreen -Unattended -MergeDemoCapture -ForceRes -ResX=1280 -ResY=720` 启动后，自动请求 25 个阶段截图到 `Saved/Tests/BuildingMergeVisual/` 并退出。普通编辑器 Play 不受该参数影响。

已用真实引擎渲染检查拆件、汇聚、展开及最终模型，全部 25 张阶段截图已生成。运行 `Tools/MakeBuildingMergePreview.py` 可以将截图整理为 `Saved/Tests/BuildingMergeVisual/BuildingMergePreview.gif`，需要本机 Python 的 Pillow。动图中的字幕仅用于说明阶段，建筑画面来自引擎截图。
