// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Actors/CastleBuildingBase.h"
#include "CastleMassSpawner.h"
#include "CastleMassTypes.h"
#include "CastleMassUnitSubsystem.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Moving_Castle.h"
#include "Units/BuildingInteraction/Demo/CastleBuildingTriggerDemo.h"

namespace CastleTowerAttackDemoTests
{
	/** 在真实游戏世界观察巡逻触发、箭塔攻击及敌人生命变化。 */
	class FRunTowerAttackDemoCommand : public IAutomationLatentCommand
	{
	public:
		explicit FRunTowerAttackDemoCommand(FAutomationTestBase* InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(TEXT("箭塔演示战斗验证超时；确认 L_BuildingTriggerDemo 已在 PIE 中运行。"));
				return true;
			}
			if (!Demo)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType != EWorldType::Game || !Context.World()) { continue; }
					for (TActorIterator<ACastleBuildingTriggerDemo> It(Context.World()); It; ++It)
					{
						Demo = *It;
						break;
					}
					if (Demo) { break; }
				}
				if (!Demo) { return false; }
			}
			if (Demo->Phase < 0)
			{
				Test->AddError(TEXT("箭塔演示初始化失败；请查看建筑触发演示错误日志。"));
				return true;
			}
			if (Demo->Phase != 1 || Demo->Buildings.IsEmpty() || !IsValid(Demo->EnemySpawner)
				|| Demo->EnemySpawner->SpawnedUnits.Num() != 1)
			{
				return false;
			}

			if (!bStarted)
			{
				Units = Demo->GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
				if (!Test->TestNotNull(TEXT("演示世界存在 Mass 单位子系统"), Units)) { return true; }
				Enemy = Demo->EnemySpawner->SpawnedUnits[0];
				if (!Test->TestTrue(TEXT("演示敌人初始状态可读取"), Units->GetUnitSnapshot(Enemy, InitialSnapshot)))
				{
					return true;
				}
				InitialTriggerCount = Demo->Buildings[0]->BuildingEffect->TriggerCount;
				ReadyTime = FPlatformTime::Seconds();
				bStarted = true;
			}

			if (FPlatformTime::Seconds() - ReadyTime < 20.0) { return false; }

			const int64 FinalTriggerCount = Demo->Buildings[0]->BuildingEffect->TriggerCount;
			Test->TestTrue(TEXT("20 秒内巡逻 NPC 触发箭塔"), FinalTriggerCount > InitialTriggerCount);
			FCastleMassUnitSnapshot FinalSnapshot;
			const bool bEnemyStillValid = Units->GetUnitSnapshot(Enemy, FinalSnapshot);
			const bool bEnemyWasHit = !bEnemyStillValid || FinalSnapshot.Health < InitialSnapshot.Health;
			Test->TestTrue(TEXT("追踪箭在演示期间降低敌人生命或击败敌人"), bEnemyWasHit);
			if (bEnemyWasHit)
			{
				const FString CombatResult = bEnemyStillValid
					? FString::Printf(TEXT("箭塔演示验证：20 秒内触发 %lld 次，追踪箭已命中，敌人生命从 %.0f 降至 %.0f。"),
						FinalTriggerCount - InitialTriggerCount, InitialSnapshot.Health, FinalSnapshot.Health)
					: FString::Printf(TEXT("箭塔演示验证：20 秒内触发 %lld 次，追踪箭已命中并击败敌人，初始生命 %.0f。"),
						FinalTriggerCount - InitialTriggerCount, InitialSnapshot.Health);
				UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *CombatResult);
				Test->AddInfo(CombatResult);
			}
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		ACastleBuildingTriggerDemo* Demo = nullptr;
		UCastleMassUnitSubsystem* Units = nullptr;
		FCastleMassUnitHandle Enemy;
		FCastleMassUnitSnapshot InitialSnapshot;
		int64 InitialTriggerCount = 0;
		double ReadyTime = 0.0;
		double Deadline = FPlatformTime::Seconds() + 90.0;
		bool bStarted = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleTowerAttackDemoRuntimeTest,
	"MovingCastle.BuildingTriggers.ArcherTowerDemoCombat20Seconds",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleTowerAttackDemoRuntimeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleTowerAttackDemoTests::FRunTowerAttackDemoCommand(this));
	return true;
}

#endif
