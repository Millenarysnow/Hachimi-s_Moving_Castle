# Mass 单位框架（UE 5.8）

敌人、NPC、子弹都作为 Mass Entity 运行。三种基础配置和 Trait 分别作为三类单位的扩展起点；运行时没有逐单位 Character、AIController、Actor Tick 或 AnimInstance。

## 直接运行

打开 `/Game/MovingCastle/Mass/L_MassUnitsDemo` 并 Play。关卡包含 256 个敌人、256 个 NPC、64 颗子弹，使用引擎默认飞行相机，WASD 与鼠标可移动观察。关卡独立指定 GameModeBase，不加载需要棋盘的城堡 UI。敌人和 NPC 持续前进，子弹沿生成变换的 +X 发射，5 秒后回收。重新 Play 可重置；它是功能示例，未代表特定硬件的性能上限。

也可以在自己的关卡放置一个 `CastleMassSpawner`，指定 Configuration、Count、Columns、Spacing。每个生成器管理一整批实体，自身不 Tick。`bStartMoving` 可让人形沿 `LocalMovementDirection` 按配置速度移动。默认以脚底为实体原点。

NPC 共享巡逻已接通：给生成器配置 NPC 并勾选 **Follow Shared Patrol Route**、关闭 **Start Moving**，它会等整条 1→2→…→N→1 路线统一算好，再沿路线生成整批实体。所有 NPC 共用一份路径和台阶采样，只有距离、路段和圈数独立。可直接运行 `/Game/MovingCastle/Maps/L_MassPatrolDemo`；玩家路径点编辑与 NavMesh 更新说明见 [NPC 共享导航](../Navigation/NPC共享导航.md)。

| 类别 | 配置基类 | 行为基类 | 已配置资产 |
| --- | --- | --- | --- |
| 敌人 | `UCastleMassEnemyConfig` | `UCastleMassEnemyTrait` | `DA_MassEnemyBase` |
| NPC | `UCastleMassNPCConfig` | `UCastleMassNPCTrait` | `DA_MassNPCBase` |
| 子弹 | `UCastleMassProjectileConfig` | `UCastleMassProjectileTrait` | `DA_MassProjectileBase` |

上述资产均在 `/Game/MovingCastle/Mass/`。敌人默认 Team=1、速度 350；NPC 默认 Team=0、速度 200；子弹默认速度 1800、半径 8、寿命 5 秒。

## 创建具体单位

新建普通 **Mass Entity Config Asset**，将 Parent 指向对应的基础资产，即可继承全部 Trait。只对需要改变的同类 Trait 添加覆盖，例如添加 `CastleMassEnemyTrait` 修改速度、生命值、阵营。修改网格或动画时覆盖 `CastleMassHumanoidVisualizationTrait`，其余组成继续继承父资产。

三种 `CastleMass*Config` C++ 类用于创建初始基础资产，构造时自带完整 Trait。创建继承父资产的具体兵种时使用普通 `MassEntityConfigAsset`，避免本地默认 Trait 覆盖父资产设置。

需要新增功能时添加 Fragment、Tag、Trait、Processor，查询 `FCastleMassEnemyTag` / `FCastleMassNPCTag` / `FCastleMassProjectileTag`。两类人形共享 `FCastleMassHumanoidTag`。新增复杂行为不需要改变现有生成入口。

## 调用入口

蓝图使用 **Get World Subsystem → CastleMassUnitSubsystem**：

- `SpawnUnits`：传入配置和一组世界变换，返回一组值类型句柄。先校验整批，再一次创建；子弹初速度为生成旋转的 +X × 配置速度。
- `MoveUnitsTo`：一批人形直达世界目标点，带加速、减速、到达停止及防越界钳制。支持三维移动。
- `FollowSharedPatrolRoute`：整批 NPC 沿已发布的全局完整闭环移动，不触发寻路；推荐沿路线生成，已有单位需要在路线附近。直接移动、设速度和停止命令会解除这些单位的巡逻。
- `SetUnitsVelocity`：人形设置持续期望速度，受 MaxSpeed 限制；子弹直接设置弹道速度。
- `StopUnits`：清除目标和速度。带重力的子弹随后仍会继续下落。
- `DamageUnits` / `DespawnUnits`：降低生命或标记死亡，生命周期 Processor 统一回收。
- `IsUnitValid` / `GetUnitSnapshot`：用于选中、调试和少量交互查询。高频全军查询应写 Mass Query。

写接口按批次将命令提交到 Mass 安全边界，返回 true 代表已接收，可能下一帧才反映在状态中。句柄包含世界标识及 Mass 序列号；其他世界的句柄拒绝整批操作，已销毁实体在命令执行时被忽略。句柄不用于跨关卡存档。

`SpawnUnits` 在游戏线程且 Mass 未处理实体时调用，适合波次系统、Actor BeginPlay、蓝图事件。Processor 内增删实体应使用 `Context.Defer()`；工作线程不要调用该蓝图子系统。`GetUnitSnapshot` 在 Mass 并行处理期间返回 false，避免读取正在更新的 Fragment。

```cpp
UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
TArray<FCastleMassUnitHandle> Handles;
FText Failure;
if (Units->SpawnUnits(EnemyConfig, SpawnTransforms, Handles, Failure))
{
    Units->MoveUnitsTo(Handles, MainTowerLocation);
}
```

## 批量骨骼动画

使用 UE 5.8 原生链路：

```text
Mass Velocity → CastleMassAnimationProcessor → FMassRepresentationAnimationFragment
  → MassConsumeInstancedSkinnedMeshAnimationProcessor → InstancedSkinnedMeshComponent
  → AnimSequenceTransformProviderData → GPU 骨骼动画
```

敌人和 NPC 的 High / Medium / Low 三个可见表示 LOD 均为 `SkinnedMeshInstance`，Off 才隐藏。近距离也保持实例化；骨骼网格自身选择渲染 LOD。移动和动画状态计算按 Chunk 并行执行；引擎负责在游戏线程批量提交显示。离屏不会停止模拟和子弹寿命。

`ATP_MassHumanoid` 共享两段序列：0 为待机，1 为行走。每个实体按速度独立选择待机/行走，切换使用 BlendTime；每个实体具有独立相位，避免整批同步踏步。`MovePlayRate` 是切入行走序列时提交的固定倍率。UE 5.8.2 内置 Mass 消费链路只在序列索引改变时重设轨道，因此这里不把每帧修改播放速率作为受支持功能。

示例两类单位共享同一个网格和动画提供器，能合并相同显示描述。`SKM_MassHumanoid` 是现有 Manny Simple 的专用副本，所有 LOD 的 Build Settings 均开启 **Optimize for Instancing**，原角色网格未被修改。

替换美术时：

1. 准备带合适渲染 LOD 的骨骼网格，在各 LOD 构建设置中开启 Optimize for Instancing 并重新构建。
2. 创建 **Transform Provider Data → Anim Sequence Transform Provider Data**，设置 Skinned Asset 和有效的 Sequences，序列骨架需要与网格兼容。
3. 在人形显示 Trait 设置 AnimationProvider、IdleSequence、MoveSequence、MovePlayRate、MeshOffset。
4. 使用引擎的“验证资产”检查提供器。骨骼数量、GPU/RHI 能力和材质复杂度仍受引擎渲染限制。

`AnimationMinScreenSize=0` 使用引擎的屏幕尺寸动画截止阈值，负数允许远处继续播放。示例设为 -1 便于观察，正式压力场景建议调整阈值。阴影默认关闭；按需要启用并用 Unreal Insights / stat gpu 测量。Entity 数量、屏幕上的三角形、骨骼数量、材质和阴影都会影响实际性能。

## 当前实现边界

- 移动提供三维目标和持续速度，符合飞行敌人的基础需求。已另建全局 NavMesh 更新与共享寻路服务，见 [NPC共享导航](../Navigation/NPC共享导航.md)；Mass NPC 尚未接入路线跟随、地面吸附或局部避让，`MoveUnitsTo` 仍会直达目标，不能直接当作寻路使用。
- 子弹有直线/重力弹道、上一帧位置、半径、阵营、伤害和自动过期。尚未执行命中检测或自动造成伤害；后续在运动后添加批量空间查询/连续线段检测 Processor，并通过目标健康 Fragment 结算。
- 生命归零立即进入回收流程；未加入死亡动画、攻击动作、StateTree、布娃娃、网络同步和持久化。后续可增加动画状态 Fragment 或对少量近处交互单位使用 Actor 表示。
- 当前基础资产使用相同模型作为框架示例。动画播放采用 GPU 骨骼序列，不执行逐单位 AnimBlueprint、Root Motion 位移或 AnimNotify 游戏逻辑；战斗事件应由 Mass 模拟驱动。

## 文件与验证

代码目录：`Source/Moving_Castle/Units/Mass/`。`CastleMassFragments` 定义实体数据，`CastleMassTraits` 定义组成，`CastleMassProcessors` 执行运动/弹道/动画/回收，`CastleMassUnitSubsystem` 提供外部接口。

自动化测试筛选 `MovingCastle.Mass`，覆盖 1024 实体跨 Chunk 运动、三维目标防越界、停止、重力弹道、寿命/主动回收、旧句柄与世界隔离、敌人/NPC 的真实配置和独立动画状态。

`Config/DefaultMass.ini` 显式启用引擎默认关闭的 LODCollector、VisualizationLOD、Visualization Processor。普通模拟测试使用 `MovingCastle.Mass.` 筛选；在示例关卡用 `-game` 和真实 RHI 运行 `MovingCastle.MassDemo`，另会验证默认 Processor 自动调度、512 个人形共享一个实际骨骼实例组件、待机/行走轨道各 256 条、轨道时间推进和销毁后零残留实例，并将截图写到 `Saved/Mass/RuntimeRepresentation.png`。

`CreateMassExamples.py` 可在 UE Python 环境重建示例资产；已存在的示例关卡会保留。脚本会重新写入列出的基础配置、动画提供器和专用网格设置，手动定制前建议复制资产。命令行路径使用正斜杠，避免 Python 命令行解析反斜杠转义：

```powershell
& 'E:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'F:/Ue5 Project/Moving_Castle-feature-gameplay/Moving_Castle.uproject' `
  -run=pythonscript '-script=F:/Ue5 Project/Moving_Castle-feature-gameplay/Docs/Mass/CreateMassExamples.py' `
  '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' -unattended -nullrhi -nosound -nop4
```

官方 API 参考：[MassRepresentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/MassRepresentation)、[批量骨骼动画消费者](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/MassRepresentation/UMassConsumeInstancedSkinnedMesh-)。实际接口以本机 UE 5.8.2 头文件及源码验证。
