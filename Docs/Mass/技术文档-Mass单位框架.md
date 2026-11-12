# Mass 单位框架

本文说明敌人、NPC 和子弹共用的实体模拟、显示及外部命令边界。NPC 共享巡逻的设计见 [NPC 导航](../Navigation/技术文档-NPC导航.md)，输入与生成接入见 [NPC 导航接入](../Navigation/技术文档-NPC导航接入.md)。

## 目标与核心方案

**单位以 Mass Entity 批量模拟，人形以原生骨骼实例批量显示。** 运行时不为每个单位创建 Character、AIController、Actor Tick 或 AnimInstance，避免规模增长时同时叠加对象、逻辑和动画更新成本。

**三种单位共享基础生命周期，通过 Trait、Fragment 和 Processor 区分能力。** 当前提供普通移动、敌人直线追踪主塔、主塔近身攻击、NPC 巡逻接入、普通及追踪子弹、连续命中、生命和回收；正式波次仍未接通。

## 结构与依赖边界

- **配置资产组合能力，Trait 建立并校验模板。** `CastleMassFragments` 保存运行数据和共享配置，`CastleMassProcessors` 执行批量运动、动画与回收；实现均在 `Source/Moving_Castle/Units/Mass/`。
- **`UCastleMassUnitSubsystem` 是外部批量入口。** 游戏线程的控制器、波次或交互层提交命令；Processor 直接查询 Fragment，不逐实体反向调用 UObject 接口。
- **`ACastleMassSpawner` 管理一批实体，不执行逐帧模拟。** 主动移除或流送卸载时清理所属单位，整个世界退出由 Mass 子系统释放，不能把销毁生成器等同于保留一支无人管理的队伍。
- **`ACastleMassEnemySpawner` 只负责在主塔周围生成一批敌人。** 它使用配置的内外半径均匀采样圆环；是否追塔或攻击由具体兵种装配的能力决定，生成器不注入战斗状态。
- **移动与动画只访问当前 Chunk 的值数据，可并行处理。** 引擎显示链路负责批量提交；显隐和模拟不互相代管，离屏不能停止寿命或弹道。

## 配置继承

**`UCastleMassEnemyConfig`、`UCastleMassNPCConfig` 和 `UCastleMassProjectileConfig` 用于建立基础资产，具体兵种使用普通 Mass 配置继承基础资产。** 基础类构造时自带完整 Trait；用它再建立子配置会使本地默认 Trait 覆盖父资产设置，导致改父资产却不影响子兵种。

**新能力通过专用 Fragment、Tag、Trait 和 Processor 接入。** 敌人与 NPC 共用人形标记，子弹使用自己的运动分支；不需要为每种单位新增一套生成 API，否则外部玩法会被迫了解内部组成。

**Mass 配置只按 Trait 精确类且有效目标标志有交集时覆盖，不按 C++ 继承关系替换。** `UCastleMassHomingProjectileTrait` 在代码中继承 `UCastleMassProjectileTrait`，但配置继承一个装配普通子弹 Trait 的资产仍会同时构建两种 Trait，重复写入子弹基础组成。箭矢使用无 Parent 的普通 `UMassEntityConfigAsset`，本地组合追踪 Trait、LOD Collector 和静态网格显示 Trait；以后新增的追踪子弹资产也不能继承含普通子弹 Trait 的配置。

**无效单位或动画配置必须拒绝模板，不能静默切换显示。** 人形动画提供器、序列和骨架必须兼容，网格需具备实例化所需的统一骨骼映射；只换网格而放过校验会在实际表示阶段才暴露错误。

## 命令与实体生命周期

**生成只能在游戏线程且 Mass 不在处理实体时进行，先验证整批再创建。** 创建上下文保持到位置、锁定目标、逐发伤害和初速度写完，防止 Observer 读到半初始化实体；Processor 内增删使用 Mass 延迟命令。

**追踪弹通过 `SpawnHomingProjectiles` 按整批提交。** 每发都显式提供发射点、当前世界的存活敌人句柄和非负伤害。配置、发射数据或任一目标失效时整批拒绝，不生成部分子弹，也不读取 Trait 伤害作为替代值。

**子弹真实初速度必须在创建 Observer 完成后写入。** 引擎随机速度初始化器会观察新 Velocity Fragment；过早写入的速度会被当作随机范围处理。当前在创建上下文释放后设置生成旋转的 +X 方向发射速度。

**外部写命令进入 Mass 安全边界，接收成功不等于状态已经可见。** 命令可能下一帧执行；空批次、错误世界或未设置句柄拒绝，执行时已经销毁的实体跳过。共享巡逻另有整批存活 NPC 校验，不能将它用于敌人或子弹。

**句柄同时携带世界身份与 Mass 序列号。** 不能跨世界复用，也不能作为跨关卡存档标识；否则实体索引被复用后会操作新单位。少量快照查询在 Mass 并行处理期间拒绝，高频全军读取应写 Mass Query。

**生命归零或寿命到期统一延迟销毁，由 Observer 清理显示实例与轨道。** 当前没有死亡等待和死亡动画，不能为了隐藏模型而跳过实体回收，否则模拟和实例资源仍会残留。

## 移动职责

**普通目标移动是三维直达，不是寻路或自动落地。** 它负责加减速、限速、到达停止和大时间步防越界；用它代替导航会直线穿越障碍。

**同一实体只能有一个位移所有者。** 显式移动、设速度或停止会解除 NPC 巡逻，普通移动 Processor 跳过巡逻中的实体；新增行为若同时改 Transform，就会重复位移或互相覆盖。

**普通子弹按速度和可选重力推进，保留上一帧位置。** 停止只清零当前速度，带重力的子弹之后仍会下落。追踪子弹使用专属 Processor 按目标当前位置重算方向，速度大小固定取 Trait 的 `Movement.MaxSpeed`；通用子弹 Processor 通过追踪 Fragment 排除它，避免一帧推进两次。

## 追踪子弹与命中结算

**选敌是三维最近距离查询，锁定后不再改选目标。** `FindNearestEnemy` 仅接受半径内带敌人 Tag 且 Health 大于零的单位；空结果是正常状态。追踪弹只追踪生成时锁定的句柄，目标死亡或实体消失时输出 Warning 并将弹体生命归零，不造成伤害、不重新索敌。

**命中使用运动线段而不是帧末点。** 专属 Processor 对比 `PreviousLocation` 到本帧位置的线段，球形碰撞半径等于子弹半径加敌人半径；高速弹不会因跨帧越过敌人而漏判。每发的伤害存在子弹 `FCastleMassProjectileFragment` 中，命中后只通过现有单位 Health 归零规则触发死亡及实体、显示回收。

**同一帧内按序结算目标生命。** Processor 先建立敌人位置和半径快照，生命 Fragment 保持可写；多发命中先前已经归零的目标时按死亡目标处理，不重复扣血。这里不引入 Actor 碰撞或第二套伤害回收路径。

## 敌人基类与近战测试兵

**`DA_MassEnemyBase` 仅承载敌人共有身份、生命、普通移动和批量显示。** `UCastleMassEnemyTrait` 不添加攻击状态，基类实体不会自动追塔或攻击。把一种测试兵的行为写入基类会让后续远程等兵种默认继承近战逻辑，不能以修改基类代替新增兵种。

**`DA_MassMeleeTest` 是继承基类的独立近战测试兵，使用生命 10、伤害 0。** 它显式添加 `UCastleMassMeleeAttackTrait`，并在本地覆盖测试用生命、速度和模型比例；主塔开局生成入口和圆环生成器默认引用此资产。其他兵种可以继承同一基类而不装配近战能力。

**近战行为仍使用 Mass，不创建逐敌人 Actor。** `Units/Mass/Enemies/` 中的近战 Trait 建立独立 Fragment，`UCastleMassMeleeAttackProcessor` 只查询具备该 Fragment 的敌人。它读取当前世界唯一主塔目标并设置直线移动；进入攻击距离后清除移动目标、停速并按实体冷却结算。零伤害也递增攻击次数并广播生命事件，使测试攻击有可观测反馈。

**主塔由棋盘 BeginPlay 在中心格的底面中心生成。** `DA_CastleCombat` 配置主塔初始生命 1000、具体敌人资产、初始数量和圆环范围；数量、范围和兵种速度属于可调测试配置，不代表正式波次或性能预算。

## 骨骼实例与动画

**所有可见表示 LOD 都使用 `SkinnedMeshInstance`，Off 才隐藏。** 骨骼网格自身选择渲染 LOD，近距离也不切换逐单位 Actor。恢复 Actor 表示会改变规模成本，必须先明确哪些单位需要交互及其预算。

**速度驱动待机与行走，动画只是模拟状态的表现。** `UCastleMassAnimationProcessor` 在运动后写表示动画 Fragment，再由 `UMassConsumeInstancedSkinnedMeshAnimationProcessor` 消费；单位相位独立，避免整批同步踏步。Root Motion 和 AnimNotify 不承担玩法位移或战斗事件。

**当前行走播放倍率在切入该序列时提交，不承诺同序列每帧变速。** 当前 UE 消费链路在序列索引变化时重新设置轨道；只逐帧改 Fragment 的倍率不会成为可靠的速度同步机制，扩展前需修改并验证实际消费行为。

**网格实例化准备与 Processor 注册都是显示链路的必要部分。** 网格各 LOD 的 Optimize for Instancing 提供统一骨骼映射，`Config/DefaultMass.ini` 显式启用引擎默认关闭的 LODCollector、VisualizationLOD 和 Visualization Processor；删除这些条件会出现实体正常模拟但没有可见实例。

## 未实现的部分与扩展准入

- **正式波次、范围查询优化、StateTree、死亡动画、局部避让、网络和持久化需独立设计。** 当前敌人攻击仍只面向已注册主塔，且沿直线移动；命中使用敌人实体快照而不是空间索引，扩展仍需保留批量入口、线程边界和位移唯一所有者。
- **示例数量与默认速度不是性能承诺或正式平衡。** 任何表示升级都应按实体数、骨骼、材质与阴影预算决定，不能由演示配置直接推出可承载的规模。
