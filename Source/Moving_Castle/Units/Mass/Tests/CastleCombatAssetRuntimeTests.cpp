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
		const bool bSwarm = Entry->EnemyId == TEXT("ShadowSwarm");
		TestEqual(TEXT("簇下界"), Entry->MinClusterCount, bSwarm ? 6 : 1);
		TestEqual(TEXT("簇上界"), Entry->MaxClusterCount, bSwarm ? 8 : 1);
		TestEqual(TEXT("簇散布半径"), Entry->ClusterSpreadRadius, bSwarm ? 120.0f : 0.0f);
	}
	TestEqual(TEXT("内半径"), Config->SpawnInnerRadius, 1800.0f);
	TestEqual(TEXT("外半径"), Config->SpawnOuterRadius, 2300.0f);
	TestEqual(TEXT("种子"), Config->SpawnSeed, 20261116);
	if (!TestNotNull(TEXT("难度曲线存在"), Config->DifficultyCurve.Get())) { return false; }
	const auto* Curve = Config->DifficultyCurve.Get();
	TestEqual(TEXT("曲线使用专用正式资产"), Curve->GetPathName(), FString(TEXT("/Game/MovingCastle/Data/Combat/DA_DifficultyCurve_Default.DA_DifficultyCurve_Default")));
	TestEqual(TEXT("普通表六场"), Curve->NormalBattles.Num(), 6);
	TestEqual(TEXT("精英表两场"), Curve->EliteBattles.Num(), 2);
	TestEqual(TEXT("初版增长率"), Curve->CountGrowthPerOrdinal, 0.25f);
	TArray<FCastleCombatSpawnGroup> Plan;
	if (TestTrue(TEXT("普通第四场"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, 4, Plan, Failure)) && TestEqual(TEXT("第四场两组"), Plan.Num(), 2))
	{
		TestEqual(TEXT("巨骸先出场"), Plan[0].EnemyId, FName(TEXT("LavaGoliath")));
		TestEqual(TEXT("巨骸一只"), Plan[0].Count, 1);
		TestEqual(TEXT("群虫延迟八秒"), Plan[1].Delay, 8.0f);
	}
	if (TestTrue(TEXT("普通第八场外推"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, 8, Plan, Failure)) && TestEqual(TEXT("第八场三组"), Plan.Num(), 3))
	{
		TestEqual(TEXT("群虫 ceil(3*1.5)"), Plan[0].Count, 5);
		TestEqual(TEXT("瘴翼 ceil(2*1.5)"), Plan[1].Count, 3);
		TestEqual(TEXT("巨骸 ceil(1*1.5)"), Plan[2].Count, 2);
		TestEqual(TEXT("第二组保留六秒"), Plan[1].Delay, 6.0f);
		TestEqual(TEXT("第三组保留十二秒"), Plan[2].Delay, 12.0f);
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
				Test->TestTrue(TEXT("默认第一场生成一簇六至八只群虫"), Count >= 6 && Count <= 8);
				Test->AddExpectedError(TEXT("已在战斗中"), EAutomationExpectedErrorFlags::Contains, 2);
				Execute(TEXT("CastleStartBattle Normal 2"));
				Execute(TEXT("CastleKillAllEnemies"));
				Stage = 2;
				return false;
			}
			if (Stage == 2)
			{
				if (Combat->IsBattleActive()) { return false; }
				Test->TestEqual(TEXT("真实第一场清场一次"), Cleared, 1);
				Execute(TEXT("CastleStartBattle Normal 4"));
				BattleTime = World->GetTimeSeconds();
				Units->CountEnemyEntities(Count);
				Test->TestEqual(TEXT("第四场立即只生成一只巨骸"), Count, 1);
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
				Test->TestEqual(TEXT("群虫仍在等待出场"), Pending, 1);
				Stage = 4;
				return false;
			}
			if (Stage == 4)
			{
				int32 Pending = 0;
				Combat->GetBattleProgress(Count, Pending);
				Test->TestEqual(TEXT("第二组出场前不通知清场"), Cleared, 1);
				if (Pending != 0) { return false; }
				Test->TestTrue(TEXT("第二组按八秒游戏时间出场"), World->GetTimeSeconds() - BattleTime >= 8.0);
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
