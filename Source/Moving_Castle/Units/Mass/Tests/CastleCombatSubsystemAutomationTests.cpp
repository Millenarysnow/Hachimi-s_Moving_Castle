#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Combat/CastleDifficultyCurveDataAsset.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Units/Mass/CastleMassEnemySpawner.h"
#include "Units/Mass/CastleMassFragments.h"
#include "Units/Mass/CastleMassProcessors.h"
#include "Units/Mass/CastleMassTraits.h"
#include "Units/Mass/CastleMassUnitSubsystem.h"
#include "Units/Mass/Enemies/CastleMassDeathSplitProcessor.h"
#include "Units/Mass/Enemies/CastleMassDeathSplitTrait.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/AutomationTest.h"

namespace CastleCombatTests
{
	/** 独立游戏世界和纯模拟兵种，保证状态机测试不会依赖关卡、渲染或攻击主塔的时机。 */
	struct FWorld
	{
		UWorld* World = nullptr;
		UCastleCombatConfigDataAsset* Config = nullptr;
		UCastleMainTowerComponent* Tower = nullptr;
		FWorld()
		{
			const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
				.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			auto* Actor = World->SpawnActor<AActor>();
			Tower = NewObject<UCastleMainTowerComponent>(Actor);
			Actor->AddInstanceComponent(Tower);
			Tower->TargetHeightOffset = 0;
			Tower->InitializeHealth();
			Tower->RegisterComponent();
			World->GetSubsystem<UCastleMainTowerSubsystem>()->RegisterMainTower(Tower);
			Config = NewObject<UCastleCombatConfigDataAsset>(World);
			Config->DifficultyCurve = NewObject<UCastleDifficultyCurveDataAsset>(Config);
			FCastleCombatEnemyEntry Entry;
			Entry.EnemyId = TEXT("Enemy");
			Entry.Configuration = NewObject<UMassEntityConfigAsset>(Config);
			Entry.Configuration->GetMutableConfig().AddTrait(*NewObject<UCastleMassEnemyTrait>(Entry.Configuration));
			Config->Roster.Add(Entry);
			SetGroups({Group(1, 0)});
		}
		~FWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		FMassEntityManager& Manager() const { return World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager(); }
		UCastleCombatSubsystem& Combat() const { return *World->GetSubsystem<UCastleCombatSubsystem>(); }
		UCastleMassUnitSubsystem& Units() const { return *World->GetSubsystem<UCastleMassUnitSubsystem>(); }
		static FCastleCombatSpawnGroup Group(const int32 Count, const float Delay)
		{
			FCastleCombatSpawnGroup Result;
			Result.EnemyId = TEXT("Enemy"); Result.Count = Count; Result.Delay = Delay;
			return Result;
		}
		void SetGroups(const TArray<FCastleCombatSpawnGroup>& Groups)
		{
			FCastleCombatBattleDefinition Battle;
			Battle.Groups = Groups;
			Config->DifficultyCurve->NormalBattles = {Battle};
			Config->DifficultyCurve->EliteBattles = {Battle};
		}
		void AddSplit()
		{
			auto* Parent = Config->Roster[0].Configuration.Get();
			auto* Child = NewObject<UMassEntityConfigAsset>(Parent);
			Child->GetMutableConfig().AddTrait(*NewObject<UCastleMassEnemyTrait>(Child));
			auto* Split = NewObject<UCastleMassDeathSplitTrait>(Parent);
			Split->ChildConfiguration = Child;
			Split->SpawnCount = 6;
			Parent->GetMutableConfig().AddTrait(*Split);
		}
		/** 两个处理器使用同一个 Context，在离开作用域时一起冲刷创建和销毁命令。 */
		void DeathFrame()
		{
			Manager().FlushCommands();
			auto* Split = NewObject<UCastleMassDeathSplitProcessor>(World);
			auto* Lifetime = NewObject<UCastleMassLifetimeProcessor>(World);
			Split->CallInitialize(World, Manager().AsShared());
			Lifetime->CallInitialize(World, Manager().AsShared());
			TArray<UMassProcessor*> Processors = {Split, Lifetime};
			UE::Mass::FProcessingContext Context(Manager(), 0.01f);
			UE::Mass::Executor::RunProcessorsView(Processors, Context);
		}
		/** 使用真实控制台注册和 DamageUnits 路径，不直接删除敌人来伪造清场。 */
		bool Kill() const { return IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleKillAllEnemies"), *GLog, World); }
		void Advance(const int32 Steps) const
		{
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				World->Tick(LEVELTICK_All, 0.1f);
				// 同步自动化命令不改变 GFrameCounter，Tickable 可能每帧只自动派发一次。
				// 显式调度真实 Tick，不改游戏时钟；暂停和到期判断仍全部读取真实 UWorld 状态。
				Combat().Tick(0.1f);
			}
		}
		int32 Count() const { int32 Result = -1; Units().CountEnemyEntities(Result); return Result; }
		int32 Spawners() const
		{
			int32 Result = 0;
			for (TActorIterator<ACastleMassEnemySpawner> It(World); It; ++It) { ++Result; }
			return Result;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatPreconditionsTest, "MovingCastle.Combat.State.Preconditions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatPreconditionsTest::RunTest(const FString& Parameters)
{
	CastleCombatTests::FWorld Scope;
	FText Failure;
	int32 Count = 1, Pending = 1;
	TestFalse(TEXT("空闲没有进度"), Scope.Combat().GetBattleProgress(Count, Pending));
	AddExpectedError(TEXT("StartBattle："), EAutomationExpectedErrorFlags::Contains, 5);
	TestFalse(TEXT("未初始化拒绝"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	TestTrue(TEXT("初始化成功"), Scope.Combat().InitializeCombat(Scope.Config, Failure));
	TestFalse(TEXT("零序号拒绝"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 0, Failure));
	TestFalse(TEXT("非法 Kind 拒绝"), Scope.Combat().StartBattle(static_cast<ECastleBattleKind>(255), 1, Failure));
	Scope.World->GetSubsystem<UCastleMainTowerSubsystem>()->UnregisterMainTower(Scope.Tower);
	TestFalse(TEXT("没有存活主塔拒绝"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	Scope.World->GetSubsystem<UCastleMainTowerSubsystem>()->RegisterMainTower(Scope.Tower);
	TestTrue(TEXT("空闲可重新初始化"), Scope.Combat().InitializeCombat(Scope.Config, Failure));
	TestTrue(TEXT("开战成功"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	TestFalse(TEXT("战斗中重复开战拒绝"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 2, Failure));
	AddExpectedError(TEXT("InitializeCombat："), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("战斗中重新初始化拒绝"), Scope.Combat().InitializeCombat(Scope.Config, Failure));
	TestEqual(TEXT("失败调用不额外生成"), Scope.Count(), 1);
	TestTrue(TEXT("原战斗继续进行"), Scope.Combat().IsBattleActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatAtomicStartTest, "MovingCastle.Combat.State.AtomicStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatAtomicStartTest::RunTest(const FString& Parameters)
{
	using namespace CastleCombatTests;
	FWorld Scope;
	Scope.AddSplit();
	Scope.SetGroups({FWorld::Group(1, 0), FWorld::Group(1, 0)});
	FText Failure;
	TestTrue(TEXT("初始化成功"), Scope.Combat().InitializeCombat(Scope.Config, Failure));
	// 先留一只场外实体；回滚必须只清掉本次开战的实体，不能清空世界或影响旧场次。
	TArray<FCastleMassUnitHandle> Existing;
	TestTrue(TEXT("预先生成场外敌人"), Scope.Units().SpawnUnits(Scope.Config->Roster[0].Configuration, {FTransform::Identity}, Existing, Failure));
	int32 Seen = 0, Cleared = 0, Failed = 0;
	Scope.Combat().OnBattleCleared.AddLambda([&](auto, auto) { ++Cleared; });
	Scope.Combat().OnBattleFailed.AddLambda([&](auto, auto, const auto&) { ++Failed; });
	const auto Hook = Scope.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
	{
		if (Actor->IsA<ACastleMassEnemySpawner>() && ++Seen == 2)
		{
			Scope.World->GetSubsystem<UCastleMainTowerSubsystem>()->UnregisterMainTower(Scope.Tower);
		}
	}));
	AddExpectedError(TEXT("Mass 敌人圆环生成失败"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("StartBattle："), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("第二个零延迟组实际生成失败"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	Scope.World->RemoveOnActorSpawnedHandler(Hook);
	TestEqual(TEXT("确实经过两个实际生成器"), Seen, 2);
	TestFalse(TEXT("失败回到空闲"), Scope.Combat().IsBattleActive());
	TestEqual(TEXT("已生成的本场巨骸全部回滚，保留场外敌人"), Scope.Count(), 1);
	TestTrue(TEXT("场外句柄仍有效"), Scope.Units().IsUnitValid(Existing[0]));
	Scope.DeathFrame();
	TestEqual(TEXT("回滚不走死亡分裂，不留下六只幼骸"), Scope.Count(), 1);
	TestEqual(TEXT("没有本场生成器残留"), Scope.Spawners(), 0);
	TestEqual(TEXT("原子开战失败不广播清场"), Cleared, 0);
	TestEqual(TEXT("开战调用返回失败，不冒充已开始战斗的延迟失败通知"), Failed, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatSplitClearTest, "MovingCastle.Combat.State.SplitClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatSplitClearTest::RunTest(const FString& Parameters)
{
	CastleCombatTests::FWorld Scope;
	Scope.AddSplit();
	FText Failure;
	int32 Cleared = 0, Failed = 0;
	Scope.Combat().OnBattleCleared.AddLambda([&](auto Kind, auto Ordinal)
	{
		TestEqual(TEXT("事件种类"), Kind, ECastleBattleKind::Normal);
		TestEqual(TEXT("事件序号"), Ordinal, 1);
		++Cleared;
	});
	Scope.Combat().OnBattleFailed.AddLambda([&](auto, auto, const auto&) { ++Failed; });
	TestTrue(TEXT("初始化"), Scope.Combat().InitializeCombat(Scope.Config, Failure));
	TestTrue(TEXT("只生成一只带分裂母体"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	TestTrue(TEXT("控制台杀敌命令存在"), Scope.Kill());
	Scope.Manager().FlushCommands();
	Scope.Combat().Tick(0);
	TestEqual(TEXT("死亡但尚未回收母体仍计数"), Scope.Count(), 1);
	TestEqual(TEXT("回收前不能清场"), Cleared, 0);
	Scope.DeathFrame();
	TestEqual(TEXT("分裂与母体销毁一起冲刷后恰好六只子体"), Scope.Count(), 6);
	Scope.Combat().Tick(0);
	TestTrue(TEXT("子体活着战斗继续"), Scope.Combat().IsBattleActive());
	TestEqual(TEXT("子体活着不能清场"), Cleared, 0);
	Scope.Kill();
	Scope.DeathFrame();
	Scope.Combat().Tick(0);
	Scope.Combat().Tick(0);
	TestEqual(TEXT("全部回收才清场且只通知一次"), Cleared, 1);
	TestEqual(TEXT("清场不会同时失败"), Failed, 0);
	TestEqual(TEXT("清场销毁生成器"), Scope.Spawners(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatGameTimeTest, "MovingCastle.Combat.State.GameTimeAndPendingGroups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatGameTimeTest::RunTest(const FString& Parameters)
{
	using namespace CastleCombatTests;
	FWorld Scope;
	Scope.SetGroups({FWorld::Group(1, 0), FWorld::Group(2, 0.5f)});
	FText Failure;
	int32 Cleared = 0, Count = 0, Pending = 0;
	Scope.Combat().OnBattleCleared.AddLambda([&](auto, auto) { ++Cleared; });
	TestTrue(TEXT("初始化"), Scope.Combat().InitializeCombat(Scope.Config, Failure));
	TestTrue(TEXT("开战"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	Scope.Kill(); Scope.DeathFrame(); Scope.Combat().Tick(0);
	TestTrue(TEXT("有待出场组时即使敌人归零也继续"), Scope.Combat().IsBattleActive());
	TestTrue(TEXT("读取进度"), Scope.Combat().GetBattleProgress(Count, Pending));
	TestEqual(TEXT("敌人已归零"), Count, 0);
	TestEqual(TEXT("仍有一组"), Pending, 1);
	Scope.Advance(2);
	const double BeforePause = Scope.World->GetTimeSeconds();
	auto* Pauser = Scope.World->SpawnActor<APlayerState>();
	Scope.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
	TestTrue(TEXT("世界真实暂停"), Scope.World->IsPaused());
	Scope.Advance(10);
	Scope.Combat().Tick(1.0f);
	TestEqual(TEXT("暂停不推进游戏时钟"), static_cast<double>(Scope.World->GetTimeSeconds()), BeforePause);
	Scope.Combat().GetBattleProgress(Count, Pending);
	TestEqual(TEXT("暂停不让延迟组出场"), Pending, 1);
	Scope.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
	Scope.Advance(4);
	Scope.Combat().GetBattleProgress(Count, Pending);
	TestEqual(TEXT("恢复后按游戏时间出场"), Pending, 0);
	TestEqual(TEXT("第二组数量"), Count, 2);
	Scope.Kill(); Scope.DeathFrame(); Scope.Combat().Tick(0);
	TestEqual(TEXT("所有组出场且归零后通知"), Cleared, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatDelayedFailureTest, "MovingCastle.Combat.State.DelayedFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatDelayedFailureTest::RunTest(const FString& Parameters)
{
	using namespace CastleCombatTests;
	FWorld Scope;
	Scope.SetGroups({FWorld::Group(1, 0), FWorld::Group(2, 0.5f), FWorld::Group(3, 1.0f)});
	FText Failure;
	int32 Cleared = 0, Failed = 0;
	Scope.Combat().OnBattleCleared.AddLambda([&](auto, auto) { ++Cleared; });
	Scope.Combat().OnBattleFailed.AddLambda([&](auto Kind, auto Ordinal, const FText& Reason)
	{
		++Failed;
		TestTrue(TEXT("失败原因带组下标和配置"), Reason.ToString().Contains(TEXT("Groups[1]")) && Reason.ToString().Contains(TEXT("配置")));
	});
	Scope.Combat().InitializeCombat(Scope.Config, Failure);
	TestTrue(TEXT("开战"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	Scope.World->GetSubsystem<UCastleMainTowerSubsystem>()->UnregisterMainTower(Scope.Tower);
	AddExpectedError(TEXT("Mass 敌人圆环生成失败"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("战斗失败："), EAutomationExpectedErrorFlags::Contains, 1);
	Scope.Advance(12);
	TestFalse(TEXT("延迟组失败回到空闲"), Scope.Combat().IsBattleActive());
	TestEqual(TEXT("失败只通知一次"), Failed, 1);
	TestEqual(TEXT("已有敌人不处理，后续组取消"), Scope.Count(), 1);
	Scope.Kill(); Scope.DeathFrame(); Scope.Combat().Tick(0); Scope.Combat().Tick(0);
	TestEqual(TEXT("失败后再归零也绝不通知清场"), Cleared, 0);
	TestEqual(TEXT("失败生成器在敌人归零后清理"), Scope.Spawners(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatSeedAndTeardownTest, "MovingCastle.Combat.State.SeedAndTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatSeedAndTeardownTest::RunTest(const FString& Parameters)
{
	using namespace CastleCombatTests;
	FWorld Scope;
	Scope.SetGroups({FWorld::Group(4, 0)});
	FText Failure;
	Scope.Combat().InitializeCombat(Scope.Config, Failure);
	TestTrue(TEXT("首次开战"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	TArray<FVector> Positions;
	for (TActorIterator<ACastleMassEnemySpawner> It(Scope.World); It; ++It)
	{
		for (const auto& Handle : It->SpawnedUnits) { Positions.Add(Scope.Manager().GetFragmentDataChecked<FTransformFragment>(Handle.Entity).GetTransform().GetLocation()); }
	}
	Scope.Kill(); Scope.DeathFrame(); Scope.Combat().Tick(0);
	TestTrue(TEXT("同场序号重新开战"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	for (TActorIterator<ACastleMassEnemySpawner> It(Scope.World); It; ++It)
	{
		TestEqual(TEXT("相同计划实体数相同"), It->SpawnedUnits.Num(), Positions.Num());
		for (int32 Index = 0; Index < It->SpawnedUnits.Num(); ++Index)
		{
			TestTrue(TEXT("四整数种子稳定复现出生位置"), Positions.IsValidIndex(Index)
				&& Scope.Manager().GetFragmentDataChecked<FTransformFragment>(It->SpawnedUnits[Index].Entity).GetTransform().GetLocation().Equals(Positions[Index]));
		}
	}
	int32 Events = 0;
	Scope.Combat().OnBattleCleared.AddLambda([&](auto, auto) { ++Events; });
	Scope.Combat().OnBattleFailed.AddLambda([&](auto, auto, const auto&) { ++Events; });
	Scope.Combat().OnWorldEndPlay(*Scope.World);
	TestFalse(TEXT("世界结束取消战斗"), Scope.Combat().IsBattleActive());
	TestFalse(TEXT("世界结束停止 Tick"), Scope.Combat().IsTickable());
	Scope.Combat().Tick(1);
	TestEqual(TEXT("世界结束无清场或失败通知"), Events, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatPreflightTest, "MovingCastle.Combat.State.WholePlanPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatPreflightTest::RunTest(const FString& Parameters)
{
	using namespace CastleCombatTests;
	FWorld Scope;
	Scope.SetGroups({FWorld::Group(1, 0), FWorld::Group(1, 2)});
	FText Failure;
	TestTrue(TEXT("初始化合法配置"), Scope.Combat().InitializeCombat(Scope.Config, Failure));
	// 初始化后资产仍可编辑，StartBattle 必须再次校验完整计划，不能生成第一组后才发现后组无效。
	Scope.Config->DifficultyCurve->NormalBattles[0].Groups[1].Count = 0;
	AddExpectedError(TEXT("StartBattle："), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("延迟组非法也整场拒绝"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	TestTrue(TEXT("拒绝原因带资产名和组下标"), Failure.ToString().Contains(Scope.Config->GetPathName())
		&& Failure.ToString().Contains(TEXT("Groups[1].Count")));
	TestEqual(TEXT("第一组尚未生成"), Scope.Count(), 0);
	TestEqual(TEXT("没有提前创建生成器"), Scope.Spawners(), 0);
	Scope.Config->DifficultyCurve->NormalBattles[0].Groups[1].Count = MAX_int32;
	Scope.Config->Roster[0].MaxClusterCount = 2;
	Scope.Config->Roster[0].ClusterSpreadRadius = 1;
	TestFalse(TEXT("计划内簇数量溢出也整场拒绝"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	TestEqual(TEXT("后组整型溢出不留下前组"), Scope.Count(), 0);
	TestFalse(TEXT("预检失败保持空闲"), Scope.Combat().IsBattleActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatReentrantClearTest, "MovingCastle.Combat.State.ReentrantClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatReentrantClearTest::RunTest(const FString& Parameters)
{
	CastleCombatTests::FWorld Scope;
	FText Failure;
	int32 Clears = 0;
	Scope.Combat().OnBattleCleared.AddLambda([&](auto Kind, auto Ordinal)
	{
		++Clears;
		if (Clears == 1)
		{
			TestEqual(TEXT("第一场事件为普通"), Kind, ECastleBattleKind::Normal);
			TestTrue(TEXT("清场委托中允许立即开下一场"), Scope.Combat().StartBattle(ECastleBattleKind::Elite, 1, Failure));
		}
		else { TestEqual(TEXT("第二场事件为精英"), Kind, ECastleBattleKind::Elite); }
	});
	Scope.Combat().InitializeCombat(Scope.Config, Failure);
	TestTrue(TEXT("开启第一场"), Scope.Combat().StartBattle(ECastleBattleKind::Normal, 1, Failure));
	Scope.Kill(); Scope.DeathFrame(); Scope.Combat().Tick(0);
	TestEqual(TEXT("第一场只广播一次"), Clears, 1);
	TestTrue(TEXT("旧 Tick 返回后新战斗仍在进行"), Scope.Combat().IsBattleActive());
	TestEqual(TEXT("新一场敌人未被旧场清理"), Scope.Count(), 1);
	Scope.Kill(); Scope.DeathFrame(); Scope.Combat().Tick(0); Scope.Combat().Tick(0);
	TestEqual(TEXT("两场分别只广播一次"), Clears, 2);
	TestFalse(TEXT("第二场清完回到空闲"), Scope.Combat().IsBattleActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatInvalidConsoleTest, "MovingCastle.Combat.State.InvalidConsoleInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatInvalidConsoleTest::RunTest(const FString& Parameters)
{
	CastleCombatTests::FWorld Scope;
	const TCHAR* Commands[] = {
		TEXT("CastleStartBattle"), TEXT("CastleStartBattle Unknown 1"), TEXT("CastleStartBattle Normal 0"),
		TEXT("CastleStartBattle Normal -1"), TEXT("CastleStartBattle Normal 1.5"),
		TEXT("CastleStartBattle Normal 2147483648"), TEXT("CastleStartBattle Normal 999999999999999999999999999999"),
		TEXT("CastleStartBattle Normal 1 extra")
	};
	AddExpectedError(TEXT("CastleStartBattle：需要"), EAutomationExpectedErrorFlags::Contains, UE_ARRAY_COUNT(Commands));
	for (const TCHAR* Command : Commands)
	{
		TestTrue(TEXT("非法参数仍进入已注册命令并明确报错"), IConsoleManager::Get().ProcessUserConsoleInput(Command, *GLog, Scope.World));
	}
	TestFalse(TEXT("非法参数不能触发默认配置初始化"), Scope.Combat().IsCombatInitialized());
	TestEqual(TEXT("非法参数不生成实体"), Scope.Count(), 0);
	AddExpectedError(TEXT("CastleKillAllEnemies：命令不接受参数"), EAutomationExpectedErrorFlags::Contains, 2);
	IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleKillAllEnemies unexpected"), *GLog, Scope.World);
	{
		auto Processing = Scope.Manager().NewProcessingScope();
		IConsoleManager::Get().ProcessUserConsoleInput(TEXT("CastleKillAllEnemies"), *GLog, Scope.World);
	}
	return true;
}

#endif
