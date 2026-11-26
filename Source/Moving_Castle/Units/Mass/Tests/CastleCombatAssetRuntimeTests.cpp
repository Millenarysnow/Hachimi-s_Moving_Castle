#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Combat/CastleDifficultyCurveDataAsset.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Units/Mass/CastleMassUnitSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "MassEntityConfigAsset.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatDefaultAssetTest, "MovingCastle.Combat.Assets.DefaultConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatDefaultAssetTest::RunTest(const FString& Parameters)
{
	auto* Config = LoadObject<UCastleCombatConfigDataAsset>(nullptr, TEXT("/Game/MovingCastle/Data/Combat/DA_CombatConfig"));
	if (!TestNotNull(TEXT("正式战斗配置存在"), Config)) { return false; }
	FText Failure;
	TestTrue(TEXT("正式配置整体合法：") + Failure.ToString(), Config->ValidateConfiguration(Failure));
	TestEqual(TEXT("四种敌人名册"), Config->Roster.Num(), 4);
	for (const TCHAR* Id : {TEXT("ShadowSwarm"), TEXT("LavaGoliath"), TEXT("MiasmaWelp"), TEXT("LavaPup")})
	{
		const auto* Entry = Config->Roster.FindByPredicate([Id](const auto& Item) { return Item.EnemyId == FName(Id); });
		if (!TestNotNull(FString::Printf(TEXT("名册含 %s"), Id), Entry)) { continue; }
		const FString Expected = FString(TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_")) + Id + TEXT(".DA_MassEnemy_") + Id;
		TestEqual(TEXT("引用原有正式敌人 DA"), GetPathNameSafe(Entry->Configuration), Expected);
		// 波次表按“只”配置数量，每个圆环基准点只展开一只，四种兵都必须保持单体。
		TestEqual(TEXT("簇下界"), Entry->MinClusterCount, 1);
		TestEqual(TEXT("簇上界"), Entry->MaxClusterCount, 1);
		TestEqual(TEXT("簇散布半径"), Entry->ClusterSpreadRadius, 0.0f);
	}
	TestEqual(TEXT("内半径"), Config->SpawnInnerRadius, 10000.0f);
	TestEqual(TEXT("外半径"), Config->SpawnOuterRadius, 12000.0f);
	TestEqual(TEXT("种子"), Config->SpawnSeed, 20261116);
	if (!TestNotNull(TEXT("难度曲线存在"), Config->DifficultyCurve.Get())) { return false; }
	const auto* Curve = Config->DifficultyCurve.Get();
	TestEqual(TEXT("曲线使用专用正式资产"), Curve->GetPathName(), FString(TEXT("/Game/MovingCastle/Data/Combat/DA_DifficultyCurve_Default.DA_DifficultyCurve_Default")));
	TestEqual(TEXT("普通表十场"), Curve->NormalBattles.Num(), 10);
	TestEqual(TEXT("精英表两场"), Curve->EliteBattles.Num(), 2);
	TestEqual(TEXT("初版增长率"), Curve->CountGrowthPerOrdinal, 0.25f);
	TArray<FCastleCombatWaveDefinition> Plan;
	if (TestTrue(TEXT("普通第一场"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, 1, Plan, Failure))
		&& TestEqual(TEXT("第一场一波"), Plan.Num(), 1))
	{
		TestTrue(TEXT("首波启用群虫曲线"), Plan[0].BugCurve.bEnabled);
		TestEqual(TEXT("首波虫群引用名册"), Plan[0].BugCurve.BugEnemyId, FName(TEXT("ShadowSwarm")));
		TestEqual(TEXT("首波五只虫"), Plan[0].BugCurve.BugBase, 5);
		if (TestEqual(TEXT("首波一条带门槛的突袭"), Plan[0].RaidEntries.Num(), 1))
		{
			TestEqual(TEXT("龙崽需要解锁门槛"), Plan[0].RaidEntries[0].RequiredUnlockTag, FName(TEXT("Unlock.MiasmaWelp")));
		}
	}
	if (TestTrue(TEXT("普通第四场"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, 4, Plan, Failure))
		&& TestEqual(TEXT("第四场两波"), Plan.Num(), 2))
	{
		TestEqual(TEXT("第四场首波六只虫"), Plan[0].BugCurve.BugBase, 6);
		TestEqual(TEXT("第四场次波六只虫"), Plan[1].BugCurve.BugBase, 6);
		if (TestEqual(TEXT("第四场次波一只幼骸"), Plan[1].RaidEntries.Num(), 1))
		{
			TestEqual(TEXT("幼骸无需门槛"), Plan[1].RaidEntries[0].EnemyId, FName(TEXT("LavaPup")));
		}
	}
	if (TestTrue(TEXT("普通第九场"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, 9, Plan, Failure)))
	{
		TestEqual(TEXT("第九场三波"), Plan.Num(), 3);
	}
	if (TestTrue(TEXT("普通第十一场外推"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, 11, Plan, Failure))
		&& TestEqual(TEXT("表外沿用末场三波"), Plan.Num(), 3))
	{
		TestEqual(TEXT("虫群首波基数继续来自虫群曲线"), Plan[0].BugCurve.BugBase, 18);
		TestEqual(TEXT("瘴翼 ceil(3*1.25)"), Plan[1].RaidEntries[0].Count, 4);
		TestEqual(TEXT("巨骸 ceil(2*1.25)"), Plan[2].RaidEntries[0].Count, 3);
		TestEqual(TEXT("瘴翼保留五秒"), Plan[1].RaidEntries[0].TriggerDelay, 5.0f);
		TestEqual(TEXT("巨骸保留四秒"), Plan[2].RaidEntries[0].TriggerDelay, 4.0f);
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (TestTrue(TEXT("精英示例能构建"), Curve->BuildBattlePlan(ECastleBattleKind::Elite, Index + 1, Plan, Failure))
			&& TestTrue(TEXT("精英示例含首批"), !Plan.IsEmpty() && !Plan[0].RaidEntries.IsEmpty()))
		{
			const auto& Raid = Plan[0].RaidEntries[0];
			TestEqual(TEXT("精英首批为龙崽或巨骸"), Raid.EnemyId,
				FName(Index == 0 ? TEXT("MiasmaWelp") : TEXT("LavaGoliath")));
			TestEqual(TEXT("精英为同模板生命五倍"), Raid.AttributeOverrides.HealthMultiplier, 5.0f);
			TestTrue(TEXT("精英自身无需解锁"), Raid.RequiredUnlockTag.IsNone());
		}
	}
	return true;
}

namespace CastleCombatRuntime
{
	/** 真实游戏世界通过注册的控制台入口，验证默认 DA、Mass 调度、分裂和屏幕事件链路。 */
	class FCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCommand(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
		~FCommand()
		{
			if (Combat.IsValid()) { Combat->OnBattleCleared.Remove(ClearHandle); Combat->OnBattleFailed.Remove(FailHandle); }
		}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 45) { Test->AddError(TEXT("真实战斗验收超过 45 秒。")); return true; }
			if (Stage == 0)
			{
				for (const auto& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
				}
				if (!Test->TestNotNull(TEXT("真实游戏世界存在"), World)) { return true; }
				Combat = World->GetSubsystem<UCastleCombatSubsystem>();
				Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
				FVector Target;
				if (!Test->TestTrue(TEXT("真实主塔已注册"), World->GetSubsystem<UCastleMainTowerSubsystem>()->GetMainTowerTargetLocation(Target))) { return true; }
				ClearHandle = Combat->OnBattleCleared.AddLambda([this](auto, auto) { ++Cleared; });
				FailHandle = Combat->OnBattleFailed.AddLambda([this](auto, auto, const auto&) { ++Failed; });
				// 先用真实伤害入口处理棋盘原型开局测试敌人，不改棋盘或旧配置。
				Execute(TEXT("CastleKillAllEnemies"));
				Stage = 1;
				return false;
			}
			int32 Count = -1;
			if (!Units->CountEnemyEntities(Count)) { Test->AddError(TEXT("真实帧边界敌人计数失败。")); return true; }
			if (Stage == 1)
			{
				if (Count != 0) { return false; }
				Execute(TEXT("CastleStartBattle Normal 1"));
				Test->TestTrue(TEXT("真实命令开启普通第一场"), Combat->IsBattleActive());
				Units->CountEnemyEntities(Count);
				Test->TestTrue(TEXT("普通首波第一批为一簇六至八只群虫"), Count >= 6 && Count <= 8);
				int32 Wave = 0, WaveCount = 0;
				Test->TestTrue(TEXT("正式普通关返回波次进度"), Combat->GetCurrentWaveProgress(Wave, WaveCount));
				Test->TestEqual(TEXT("正式普通关从第一波开始"), Wave, 1);
				Test->TestEqual(TEXT("正式普通关两波"), WaveCount, 2);
				Test->AddExpectedError(TEXT("已在战斗中"), EAutomationExpectedErrorFlags::Contains, 2);
				Execute(TEXT("CastleStartBattle Normal 2"));
				Execute(TEXT("CastleKillAllEnemies"));
				Stage = 2;
				return false;
			}
			if (Stage == 2)
			{
				if (Combat->IsBattleActive())
				{
					int32 Wave = 0, WaveCount = 0;
					Combat->GetCurrentWaveProgress(Wave, WaveCount);
					bSawSecondWave |= Wave == 2;
					if (Count > 0) { Execute(TEXT("CastleKillAllEnemies")); }
					return false;
				}
				Test->TestEqual(TEXT("真实第一场清场一次"), Cleared, 1);
				Test->TestTrue(TEXT("清场前确实进入第二波"), bSawSecondWave);
				Execute(TEXT("CastleStartBattle Elite 2"));
				BattleTime = World->GetTimeSeconds();
				Units->CountEnemyEntities(Count);
				Test->TestEqual(TEXT("精英第二场立即只生成一只巨骸"), Count, 1);
				Execute(TEXT("CastleKillAllEnemies"));
				Stage = 3;
				return false;
			}
			if (Stage == 3)
			{
				if (Count != 6)
				{
					if (World->GetTimeSeconds() - BattleTime > 3) { Test->AddError(TEXT("真实巨骸死亡后未看到六只幼骸。")); return true; }
					return false;
				}
				int32 Pending = 0;
				Combat->GetBattleProgress(Count, Pending);
				Test->TestEqual(TEXT("巨骸回收和分裂之间没有误清场"), Cleared, 1);
				Test->TestTrue(TEXT("子体和待出场组使战斗继续"), Combat->IsBattleActive());
				Test->TestEqual(TEXT("后续龙崽突袭仍在等待出场"), Pending, 1);
				Stage = 4;
				return false;
			}
			if (Stage == 4)
			{
				int32 Pending = 0;
				Combat->GetBattleProgress(Count, Pending);
				Test->TestEqual(TEXT("第二组出场前不通知清场"), Cleared, 1);
				if (Pending != 0) { return false; }
				Test->TestTrue(TEXT("后续突袭按六秒游戏时间出场"), World->GetTimeSeconds() - BattleTime >= 6.0);
				Execute(TEXT("CastleKillAllEnemies"));
				Stage = 5;
				return false;
			}
			if (Combat->IsBattleActive()) { return false; }
			Test->TestEqual(TEXT("巨骸、幼骸和延迟组结束后只多清场一次"), Cleared, 2);
			Test->TestEqual(TEXT("真实清场没有失败通知"), Failed, 0);
			Test->TestEqual(TEXT("真实世界敌人归零"), Count, 0);
			return true;
		}
	private:
		void Execute(const TCHAR* Command) { Test->TestTrue(FString(TEXT("控制台已注册：")) + Command, IConsoleManager::Get().ProcessUserConsoleInput(Command, *GLog, World)); }
		FAutomationTestBase* Test;
		UWorld* World = nullptr;
		UCastleMassUnitSubsystem* Units = nullptr;
		TWeakObjectPtr<UCastleCombatSubsystem> Combat;
		FDelegateHandle ClearHandle, FailHandle;
		double Started, BattleTime = 0;
		int32 Stage = 0, Cleared = 0, Failed = 0;
		bool bSawSecondWave = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatRuntimeTest, "MovingCastle.CombatRuntime.ConsoleAndSplit",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatRuntimeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleCombatRuntime::FCommand(this));
	return true;
}

#endif
