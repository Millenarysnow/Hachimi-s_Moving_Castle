#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Combat/CastleDifficultyCurveDataAsset.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Units/Mass/CastleMassFragments.h"
#include "Units/Mass/CastleMassProcessors.h"
#include "Units/Mass/CastleMassTraits.h"
#include "Units/Mass/CastleMassUnitSubsystem.h"
#include "Units/Mass/Enemies/CastleMassEnemyLaserAttackFragments.h"
#include "Units/Mass/Enemies/CastleMassEnemyLaserAttackProcessor.h"
#include "Units/Mass/Enemies/CastleMassEnemyRangedAttackFragments.h"
#include "Units/Mass/Enemies/CastleMassEnemyRangedAttackProcessor.h"
#include "Units/Mass/Enemies/CastleMassMeleeAttackFragments.h"
#include "Units/Mass/Enemies/CastleMassMeleeAttackProcessor.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace CastleWaveCombatTests
{
	/** 每个场景用独立游戏世界检验真实 Mass 查询、子系统和主塔契约。 */
	struct FWorld
	{
		UWorld* World;

		FWorld()
		{
			const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
				.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			auto* Owner = World->SpawnActor<AActor>();
			auto* Tower = NewObject<UCastleMainTowerComponent>(Owner);
			Owner->AddInstanceComponent(Tower);
			Tower->TargetHeightOffset = 0.0f;
			Tower->InitializeHealth();
			Tower->RegisterComponent();
			World->GetSubsystem<UCastleMainTowerSubsystem>()->RegisterMainTower(Tower);
		}

		~FWorld()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		UCastleMassUnitSubsystem& Units() const { return *World->GetSubsystem<UCastleMassUnitSubsystem>(); }
		FMassEntityManager& Manager() const { return World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager(); }
		UCastleCombatSubsystem& Combat() const { return *World->GetSubsystem<UCastleCombatSubsystem>(); }

		template<class TProcessor>
		void Step(const float DeltaSeconds) const
		{
			// 远程攻击的 Create 命令在真实执行上下文退出时冲刷。
			Manager().FlushCommands();
			auto* Processor = NewObject<TProcessor>(World);
			Processor->CallInitialize(World, Manager().AsShared());
			UE::Mass::FProcessingContext Context(Manager(), DeltaSeconds);
			UE::Mass::Executor::Run(*Processor, Context);
		}

		void Advance(const int32 Frames = 1) const
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				World->Tick(LEVELTICK_All, 0.1f);
				// 同步测试不会推进 GFrameCounter；显式派发 Tick，仍使用真实世界时钟。
				Combat().Tick(0.1f);
			}
		}

		void KillAll() const
		{
			IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleKillAllEnemies"), *GLog, World);
			Step<UCastleMassLifetimeProcessor>(0.0f);
			Combat().Tick(0.0f);
		}
	};

	/** 测试名册每个圆环点恰好一只，直接通过实体数核对批次，无需依赖随机簇大小。 */
	UCastleCombatConfigDataAsset* MakeWaveConfig(const FWorld& Scope, const TArray<FCastleCombatWaveDefinition>& Waves)
	{
		auto* Config = NewObject<UCastleCombatConfigDataAsset>(Scope.World);
		auto* Template = NewObject<UMassEntityConfigAsset>(Config);
		Template->GetMutableConfig().AddTrait(*NewObject<UCastleMassEnemyTrait>(Template));
		auto& Enemy = Config->Roster.AddDefaulted_GetRef();
		Enemy.EnemyId = TEXT("TestEnemy");
		Enemy.Configuration = Template;
		Config->DifficultyCurve = NewObject<UCastleDifficultyCurveDataAsset>(Config);
		FCastleCombatBattleDefinition Battle;
		Battle.Waves = Waves;
		Config->DifficultyCurve->NormalBattles.Add(Battle);
		Config->DifficultyCurve->EliteBattles.Add(Battle);
		return Config;
	}

	FCastleRaidSpawnEntry MakeRaid(const float Delay = 0.0f)
	{
		FCastleRaidSpawnEntry Raid;
		Raid.EnemyId = TEXT("TestEnemy");
		Raid.TriggerDelay = Delay;
		Raid.SpawnInterval = 0.0f;
		return Raid;
	}

	template<class TAttack, class TProcessor>
	bool CheckAttackOverrides(FAutomationTestBase& Test, const FWorld& Scope, const TCHAR* Path)
	{
		auto* Config = LoadObject<UMassEntityConfigAsset>(nullptr, Path);
		if (!Test.TestNotNull(FString(TEXT("正式敌人资产 ")) + Path, Config)) { return false; }
		FText Failure;
		FCastleMassEnemyAttributeOverrides Overrides;
		Overrides.HealthMultiplier = 5.0f;
		Overrides.MoveSpeedMultiplier = 0.5f;
		Overrides.DamageMultiplier = 1.5f;
		Overrides.AttackIntervalMultiplier = 2.0f;
		Overrides.ScaleMultiplier = 1.5f;
		int32 SpawnIndex = 0;
		const auto Spawn = [&](const FCastleMassEnemyAttributeOverrides& Attributes, TArray<FCastleMassUnitHandle>& Out)
		{
			// 三个实例沿塔外同一射线隔开，避免测试移速倍率时引入主塔投影与 Boids 分离力。
			const FVector SpawnLocation(-600.0f - static_cast<float>(SpawnIndex++) * 1200.0f, 0.0f, 50.0f);
			const FTransform SpawnTransform(FQuat::Identity, SpawnLocation, FVector(1.25));
			if (Scope.Units().SpawnEnemies(Config, {SpawnTransform}, Attributes, Out, Failure) && Out.Num() == 1) { return true; }
			Test.AddError(FString::Printf(TEXT("%s：生成失败或数量不符。%s"), Path, *Failure.ToString()));
			return false;
		};

		auto& Manager = Scope.Manager();
		TArray<FCastleMassUnitHandle> Before, Elite, After;
		if (!Spawn({}, Before) || !Spawn(Overrides, Elite) || !Spawn({}, After)) { return false; }
		const auto* BeforeState = Manager.GetFragmentDataPtr<TAttack>(Before[0].Entity);
		const auto* EliteState = Manager.GetFragmentDataPtr<TAttack>(Elite[0].Entity);
		const auto* AfterState = Manager.GetFragmentDataPtr<TAttack>(After[0].Entity);
		if (!Test.TestNotNull(TEXT("原始攻击片段存在"), BeforeState)
			|| !Test.TestNotNull(TEXT("覆写攻击片段存在"), EliteState)
			|| !Test.TestNotNull(TEXT("后续普通攻击片段存在"), AfterState)) { return false; }

		FCastleMassUnitSnapshot BaseSnapshot, EliteSnapshot, AfterSnapshot;
		Scope.Units().GetUnitSnapshot(Before[0], BaseSnapshot);
		Scope.Units().GetUnitSnapshot(Elite[0], EliteSnapshot);
		Scope.Units().GetUnitSnapshot(After[0], AfterSnapshot);
		Test.TestEqual(TEXT("精英生命为基准五倍"), EliteSnapshot.Health, BaseSnapshot.Health * 5.0f);
		Test.TestEqual(TEXT("后续普通实例保留模板生命"), AfterSnapshot.Health, BaseSnapshot.Health);
		Test.TestTrue(TEXT("模型倍数乘到生成变换上"), EliteSnapshot.Transform.GetScale3D().Equals(FVector(1.875)));
		Test.TestTrue(TEXT("后续普通实例保留输入缩放"), AfterSnapshot.Transform.GetScale3D().Equals(FVector(1.25)));
		// Mass 表示向该引擎组件提交实体完整变换，读回实际存储以验证实例缩放未被丢弃。
		auto* MeshInstances = NewObject<UInstancedSkinnedMeshComponent>(Scope.World);
		const auto InstanceId = MeshInstances->AddInstance(EliteSnapshot.Transform, 0);
		FTransform StoredInstanceTransform;
		Test.TestTrue(TEXT("原生骨骼实例组件保留提交的实例缩放"),
			MeshInstances->GetInstanceTransform(InstanceId, StoredInstanceTransform)
			&& StoredInstanceTransform.GetScale3D().Equals(EliteSnapshot.Transform.GetScale3D()));
		Test.TestEqual(TEXT("伤害覆写写入对应实例"), EliteState->AttackDamage, BeforeState->AttackDamage * 1.5f);
		Test.TestEqual(TEXT("攻击间隔覆写写入对应实例"), EliteState->AttackInterval, BeforeState->AttackInterval * 2.0f);
		Test.TestEqual(TEXT("共享模板伤害未变"), AfterState->AttackDamage, BeforeState->AttackDamage);
		Test.TestEqual(TEXT("共享模板攻击间隔未变"), AfterState->AttackInterval, BeforeState->AttackInterval);

		Manager.GetFragmentDataChecked<FCastleMassUnitFragment>(Elite[0].Entity).SupportMovementMultiplier = 0.5f;
		Scope.Units().MoveUnitsTo({Before[0], Elite[0], After[0]}, FVector(-100000, 0, 50));
		Scope.Step<UCastleMassMovementProcessor>(2.0f);
		Scope.Units().GetUnitSnapshot(Before[0], BaseSnapshot);
		Scope.Units().GetUnitSnapshot(Elite[0], EliteSnapshot);
		Scope.Units().GetUnitSnapshot(After[0], AfterSnapshot);
		Test.TestTrue(TEXT("实例移速与实时支援倍率叠乘"),
			FMath::IsNearlyEqual(EliteSnapshot.Velocity.Size(), BaseSnapshot.Velocity.Size() * 0.25, 0.01));
		Test.TestTrue(TEXT("后续普通实例使用原移速"), AfterSnapshot.Velocity.Equals(BaseSnapshot.Velocity));

		// 将所有实例置于攻击距离内；经过实际攻击处理器和远程安全发射边界后读取冷却。
		for (const FMassEntityHandle Entity : {Before[0].Entity, Elite[0].Entity, After[0].Entity})
		{
			Manager.GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(FVector(350, 0, 0));
		}
		Test.TestTrue(TEXT("本世界可接受攻速 0.5"), Scope.Units().SetGlobalEnemyAttackSpeedMultiplier(0.5f));
		Scope.Step<TProcessor>(0.0f);
		for (const FMassEntityHandle Entity : {Before[0].Entity, Elite[0].Entity, After[0].Entity})
		{
			const auto& Attack = Manager.GetFragmentDataChecked<TAttack>(Entity);
			Test.TestEqual(TEXT("真实攻击已执行一次"), Attack.AttackCount, 1);
			Test.TestEqual(TEXT("世界攻速减半使每个实例冷却加倍"), Attack.AttackCooldown, static_cast<double>(Attack.AttackInterval) * 2.0);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleWaveMassOverridesTest, "MovingCastle.Combat.Waves.MassOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleWaveMassOverridesTest::RunTest(const FString& Parameters)
{
	using namespace CastleWaveCombatTests;
	{
		FWorld Scope;
		CheckAttackOverrides<FCastleMassMeleeAttackFragment, UCastleMassMeleeAttackProcessor>(
			*this, Scope, TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_ShadowSwarm"));
	}
	{
		FWorld Scope;
		CheckAttackOverrides<FCastleMassEnemyRangedAttackFragment, UCastleMassEnemyRangedAttackProcessor>(
			*this, Scope, TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_MiasmaWelp"));
	}
	{
		FWorld Scope;
		CheckAttackOverrides<FCastleMassEnemyLaserAttackFragment, UCastleMassEnemyLaserAttackProcessor>(
			*this, Scope, TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_LavaPup"));
	}
	FWorld Scope;
	TestEqual(TEXT("新世界攻速为中性值"), Scope.Units().GetGlobalEnemyAttackSpeedMultiplier(), 1.0f);
	TestTrue(TEXT("攻速上界 10 合法"), Scope.Units().SetGlobalEnemyAttackSpeedMultiplier(10.0f));
	AddExpectedError(TEXT("SetGlobalEnemyAttackSpeedMultiplier："), EAutomationExpectedErrorFlags::Contains, 4);
	for (const float Invalid : {0.0f, -1.0f, 10.1f, std::numeric_limits<float>::quiet_NaN()})
	{
		TestFalse(TEXT("非法攻速倍率明确拒绝"), Scope.Units().SetGlobalEnemyAttackSpeedMultiplier(Invalid));
		TestEqual(TEXT("拒绝后保留原攻速倍率"), Scope.Units().GetGlobalEnemyAttackSpeedMultiplier(), 10.0f);
	}
	auto* Config = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_MiasmaWelp"));
	if (!TestNotNull(TEXT("绝对血量验收使用正式龙崽模板"), Config)) { return false; }
	FCastleMassEnemyAttributeOverrides Absolute;
	Absolute.HealthOverride = 1234.0f;
	TArray<FCastleMassUnitHandle> Entities;
	FText Failure;
	if (!TestTrue(TEXT("合法的绝对血量覆写可生成"), Scope.Units().SpawnEnemies(Config, {FTransform::Identity}, Absolute, Entities, Failure))
		|| !TestEqual(TEXT("绝对血量批次生成一只"), Entities.Num(), 1)) { return false; }
	FCastleMassUnitSnapshot Snapshot;
	TestTrue(TEXT("绝对血量实体可查询"), Scope.Units().GetUnitSnapshot(Entities[0], Snapshot));
	TestEqual(TEXT("绝对血量覆盖模板"), Snapshot.Health, 1234.0f);
	Absolute.HealthMultiplier = 2.0f;
	AddExpectedError(TEXT("SpawnEnemies："), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("不能同时写绝对血量与血量倍率"),
		Scope.Units().SpawnEnemies(Config, {FTransform::Identity}, Absolute, Entities, Failure));
	TestTrue(TEXT("拒绝清空本次输出"), Entities.IsEmpty());
	int32 Count = 0;
	Scope.Units().CountEnemyEntities(Count);
	TestEqual(TEXT("拒绝未生成额外实体或删除旧实体"), Count, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleTwoWaveScheduleTest, "MovingCastle.Combat.Waves.ClearAdvanceAndBatchTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleTwoWaveScheduleTest::RunTest(const FString& Parameters)
{
	using namespace CastleWaveCombatTests;
	FWorld Scope;
	FCastleCombatWaveDefinition FirstWave, NextWave;
	FirstWave.BugCurve.BugEnemyId = TEXT("TestEnemy");
	FirstWave.BugCurve.CurveK = 0.0f;
	FirstWave.BugCurve.BatchSize = 2;
	NextWave.BugCurve.bEnabled = false;
	NextWave.RaidEntries = {MakeRaid(1.0f)};
	auto* Config = MakeWaveConfig(Scope, {FirstWave, NextWave});
	int32 Clears = 0;
	int32 Fails = 0;
	auto& Combat = Scope.Combat();
	Combat.OnBattleCleared.AddLambda([&Clears](auto, auto) { ++Clears; });
	Combat.OnBattleFailed.AddLambda([&Fails](auto, auto, const auto&) { ++Fails; });
	FText Failure;
	if (!TestTrue(TEXT("初始化两波配置"), Combat.InitializeCombat(Config, Failure))
		|| !TestTrue(TEXT("零延时批次随开战生成"), Combat.StartBattle(ECastleBattleKind::Normal, 1, Failure))) { return false; }
	int32 Alive = 0, Pending = 0, Wave = 0, Total = 0;
	Combat.GetBattleProgress(Alive, Pending);
	TestEqual(TEXT("首批两个基准点"), Alive, 2);
	TestEqual(TEXT("待出场计入本波与下一波的批次"), Pending, 2);
	TestTrue(TEXT("开战有波次进度"), Combat.GetCurrentWaveProgress(Wave, Total));
	TestEqual(TEXT("开始于第 1 波"), Wave, 1);
	TestEqual(TEXT("总波次为 2"), Total, 2);
	Scope.Advance(4);
	Combat.GetBattleProgress(Alive, Pending);
	TestEqual(TEXT("半秒前第二批未到期"), Alive, 2);
	Scope.Advance(2);
	Combat.GetBattleProgress(Alive, Pending);
	TestEqual(TEXT("半秒后第二批出场"), Alive, 4);

	// 开战后的表变更既不提前原本延迟的突袭，也不修改它的数量。
	Config->DifficultyCurve->NormalBattles[0].Waves[1].RaidEntries[0].TriggerDelay = 0.0f;
	Config->DifficultyCurve->NormalBattles[0].Waves[1].RaidEntries[0].Count = 7;
	Scope.KillAll();
	TestEqual(TEXT("第一波清完未发整场清场事件"), Clears, 0);
	TestTrue(TEXT("第一波归零立即进入第二波"), Combat.GetCurrentWaveProgress(Wave, Total));
	TestEqual(TEXT("下一波索引为 2"), Wave, 2);
	int32 Defeated = 0, Budget = 0;
	TestTrue(TEXT("波间仍有已击杀进度"), Combat.GetBattleDefeatedProgress(Defeated, Budget));
	TestEqual(TEXT("第一波击杀四只"), Defeated, 4);
	TestEqual(TEXT("全场预算含第二波"), Budget, 5);
	Scope.Advance(9);
	Combat.GetBattleProgress(Alive, Pending);
	TestEqual(TEXT("第二波开始未满一秒时突袭仍等待"), Alive, 0);
	TestEqual(TEXT("未来批次阻止清场"), Pending, 1);
	Scope.Advance(2);
	Combat.GetBattleProgress(Alive, Pending);
	TestEqual(TEXT("第二波达到冻结延迟只生成冻结数量"), Alive, 1);
	TestEqual(TEXT("所有批次均已出场"), Pending, 0);
	Scope.KillAll();
	Combat.Tick(0.0f);
	TestEqual(TEXT("末波归零仅通知一次"), Clears, 1);
	TestEqual(TEXT("没有战斗失败"), Fails, 0);
	TestFalse(TEXT("结束后波次查询返回 false"), Combat.GetCurrentWaveProgress(Wave, Total));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleWaveUnlockSnapshotTest, "MovingCastle.Combat.Waves.UnlockAndRaidInterval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleWaveUnlockSnapshotTest::RunTest(const FString& Parameters)
{
	using namespace CastleWaveCombatTests;
	FWorld Scope;
	FCastleCombatWaveDefinition Wave;
	Wave.BugCurve.bEnabled = false;
	auto Raid = MakeRaid(0.5f);
	Raid.RequiredUnlockTag = TEXT("Unlock.TestRaid");
	Raid.Count = 2;
	Raid.SpawnInterval = 0.5f;
	Wave.RaidEntries = {MakeRaid(), Raid};
	auto* Config = MakeWaveConfig(Scope, {Wave});
	auto& Combat = Scope.Combat();
	FText Failure;
	if (!TestTrue(TEXT("含合法门槛的配置可初始化"), Combat.InitializeCombat(Config, Failure))
		|| !TestTrue(TEXT("未解锁开战仍可生成无门槛条目"), Combat.StartBattle(ECastleBattleKind::Normal, 1, Failure))) { return false; }
	int32 Alive = 0, Pending = 0;
	TestFalse(TEXT("新世界没有测试解锁项"), Combat.HasUnlockedEnemyTag(Raid.RequiredUnlockTag));
	Combat.GetBattleProgress(Alive, Pending);
	TestEqual(TEXT("未解锁突袭不进入待出场预算"), Pending, 0);
	TestTrue(TEXT("有效标签可以解锁"), Combat.UnlockEnemyTag(Raid.RequiredUnlockTag));
	Scope.Advance(12);
	Combat.GetBattleProgress(Alive, Pending);
	TestEqual(TEXT("本场冻结后解锁不会添加突袭"), Alive, 1);
	Scope.KillAll();
	if (!TestTrue(TEXT("带已解锁标签开启新场"), Combat.StartBattle(ECastleBattleKind::Normal, 1, Failure))) { return false; }
	Combat.ResetUnlockedEnemyTags();
	TestFalse(TEXT("重置后查询不到该标签"), Combat.HasUnlockedEnemyTag(Raid.RequiredUnlockTag));
	Scope.Advance(4);
	Combat.GetBattleProgress(Alive, Pending);
	TestEqual(TEXT("相对本波的触发延迟到期前不新增敌人"), Alive, 1);
	Scope.Advance(2);
	Combat.GetBattleProgress(Alive, Pending);
	TestEqual(TEXT("解锁后按时逐只生成第一批，本场途中重置也不改变快照"), Alive, 2);
	TestEqual(TEXT("还有一只按照间隔等待"), Pending, 1);
	Scope.Advance(5);
	Combat.GetBattleProgress(Alive, Pending);
	TestEqual(TEXT("组内间隔到期后生成第二只"), Alive, 3);
	TestEqual(TEXT("两只均已刷出"), Pending, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleWaveTimedAdvanceTest, "MovingCastle.Combat.Waves.TimedAdvance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleWaveTimedAdvanceTest::RunTest(const FString& Parameters)
{
	using namespace CastleWaveCombatTests;
	FWorld Scope;
	FCastleCombatWaveDefinition FirstWave, NextWave;
	FirstWave.BugCurve.bEnabled = false;
	FirstWave.RaidEntries = {MakeRaid(), MakeRaid(0.75f)};
	FirstWave.bAdvanceOnClear = false;
	FirstWave.WaveDuration = 0.5f;
	NextWave.BugCurve.bEnabled = false;
	NextWave.RaidEntries = {MakeRaid()};
	auto& Combat = Scope.Combat();
	FText Failure;
	int32 Clears = 0;
	Combat.OnBattleCleared.AddLambda([&Clears](auto, auto) { ++Clears; });
	if (!TestTrue(TEXT("初始化定时推进关卡"), Combat.InitializeCombat(MakeWaveConfig(Scope, {FirstWave, NextWave}), Failure))
		|| !TestTrue(TEXT("开战成功"), Combat.StartBattle(ECastleBattleKind::Normal, 1, Failure))) { return false; }
	int32 Wave = 0, Total = 0, Alive = 0, Pending = 0;
	Scope.Advance(6);
	Combat.GetCurrentWaveProgress(Wave, Total);
	TestEqual(TEXT("定时时长届满但仍有出场批次时保持本波"), Wave, 1);
	Scope.Advance(3);
	Combat.GetCurrentWaveProgress(Wave, Total);
	Combat.GetBattleProgress(Alive, Pending);
	TestEqual(TEXT("定时时长届满且批次刷完后可带存活敌人进入下一波"), Wave, 2);
	TestEqual(TEXT("换波不删除第一波敌人，第二波首批立即出场"), Alive, 3);
	TestEqual(TEXT("存活敌人继续阻止整场结算"), Clears, 0);
	Scope.KillAll();
	TestEqual(TEXT("世界归零后才通知清场"), Clears, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleWaveCurveTest, "MovingCastle.Combat.Waves.CurveAndGlobalIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleWaveCurveTest::RunTest(const FString& Parameters)
{
	using namespace CastleWaveCombatTests;
	const FCastleBugCurveConfig Curve;
	const int32 ExpectedCounts[] = {4, 7, 8, 10, 10, 11, 12, 12, 13, 13, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpectedCounts); ++Index)
	{
		TestEqual(FString::Printf(TEXT("第 %d 波虫群基准点数"), Index + 1),
			Curve.GetSpawnPointCount(Index + 1), ExpectedCounts[Index]);
	}
	TestEqual(TEXT("第 50 波仍保持 16 个点"), Curve.GetSpawnPointCount(50), 16);
	auto HighCurve = Curve;
	HighCurve.CurveK = MAX_flt;
	TestEqual(TEXT("极大曲线系数先应用硬上限，避免整数溢出"), HighCurve.GetSpawnPointCount(2), 16);

	FWorld Scope;
	FCastleCombatWaveDefinition Wave;
	Wave.BugCurve.BugEnemyId = TEXT("TestEnemy");
	Wave.BugCurve.BatchSize = 16;
	auto Raid = MakeRaid();
	Raid.MinWaveIndex = 5;
	Wave.RaidEntries = {Raid};
	auto* Config = MakeWaveConfig(Scope, {Wave, Wave});
	Config->DifficultyCurve->EliteBattles[0].Waves.SetNum(1);
	Config->DifficultyCurve->CountGrowthPerOrdinal = 0.0f;
	auto& Combat = Scope.Combat();
	FText Failure;
	if (!TestTrue(TEXT("初始化连续索引测试配置"), Combat.InitializeCombat(Config, Failure))) { return false; }
	const auto CheckStart = [&](ECastleBattleKind Kind, int32 Ordinal, int32 Expected)
	{
		if (!TestTrue(TEXT("按指定序号开战"), Combat.StartBattle(Kind, Ordinal, Failure))) { return false; }
		int32 Count = 0, Pending = 0;
		Combat.GetBattleProgress(Count, Pending);
		TestEqual(TEXT("首批按对应静态表索引与突袭门槛出场"), Count, Expected);
		while (Combat.IsBattleActive())
		{
			int32 Previous = 0, Total = 0;
			Combat.GetCurrentWaveProgress(Previous, Total);
			Scope.KillAll();
			int32 Current = 0;
			Combat.GetCurrentWaveProgress(Current, Total);
			if (Combat.IsBattleActive() && Current <= Previous)
			{
				AddError(TEXT("全部零延迟的波次清空后应立即推进。"));
				return false;
			}
		}
		return true;
	};
	// 普通场次 3 从第 5 波开始，精英场次 3 从第 3 波开始，与实际开战历史无关。
	return CheckStart(ECastleBattleKind::Normal, 3, 11)
		&& CheckStart(ECastleBattleKind::Elite, 3, 8)
		&& CheckStart(ECastleBattleKind::Normal, 3, 11);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleWaveEmptyAfterGateTest, "MovingCastle.Combat.Waves.EmptyAfterGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleWaveEmptyAfterGateTest::RunTest(const FString& Parameters)
{
	using namespace CastleWaveCombatTests;
	FWorld Scope;
	FCastleCombatWaveDefinition FirstWave, NextWave;
	FirstWave.BugCurve.bEnabled = false;
	FirstWave.RaidEntries = {MakeRaid()};
	NextWave = FirstWave;
	NextWave.RaidEntries[0].RequiredUnlockTag = TEXT("Unlock.BeforeFinalWave");
	auto& Combat = Scope.Combat();
	FText Failure;
	if (!TestTrue(TEXT("仅含门槛的波次可以配置"), Combat.InitializeCombat(MakeWaveConfig(Scope, {FirstWave, NextWave}), Failure)))
	{
		return false;
	}
	AddExpectedError(TEXT("StartBattle："), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("后续波次过滤为空也整场拒绝"), Combat.StartBattle(ECastleBattleKind::Normal, 1, Failure));
	TestTrue(TEXT("拒绝原因带第二波下标"), Failure.ToString().Contains(TEXT("Waves[1]")));
	int32 Count = -1;
	Scope.Units().CountEnemyEntities(Count);
	TestEqual(TEXT("第一波也未生成"), Count, 0);
	TestFalse(TEXT("开战前拒绝保持空闲"), Combat.IsBattleActive());
	return true;
}

#endif
