#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Support/CastleSupportFlightActors.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"
#include "CastleBoard.h"
#include "CastleBuildingDefinition.h"
#include "CastleMassFragments.h"
#include "CastlePlayerController.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "MassEntityQuery.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Tests/AutomationCommon.h"

namespace
{
	UWorld* FindSupportPIE()
	{
		for (const auto& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World()) { return Context.World(); }
		}
		return nullptr;
	}

	/** 使用实际 MainMap 航线、地面、配置和 Mass 实体，验证蓝色招募与在途人员接回。 */
	class FExerciseSupportMainMap final : public IAutomationLatentCommand
	{
	public:
		explicit FExerciseSupportMainMap(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(FString::Printf(TEXT("主关支援验证超时：阶段 %d"), Stage));
				return true;
			}
			UWorld* World = FindSupportPIE();
			if (!World) { return false; }
			if (Stage == 0)
			{
				TActorIterator<ACastleBoard> Board(World);
				if (!Test->TestTrue(TEXT("主关棋盘可用"), static_cast<bool>(Board))) { return true; }
				FText Reason;
				Test->TestTrue(TEXT("从已保存的主关加载两个定义并通过商店池校验"),
					Board->FindBuildingDefinition(ECastleBuildingType::BeeHive) && Board->FindBuildingDefinition(ECastleBuildingType::BalloonTower)
					&& Board->ValidateBuildingDefinitions(Reason) && Board->BuildingDefinitions.Num() == 7);
				FOutputDeviceNull Output;
				if (!Test->TestTrue(TEXT("执行用户验收命令"), IConsoleManager::Get().ProcessUserConsoleInput(
					TEXT("Castle.SupportDemo Balloon 4 Blue"), Output, World))) { return true; }
				Stage = 1;
				return false;
			}
			const auto* Nav = World->GetSubsystem<UCastleNavigationSubsystem>();
			if (Stage == 1)
			{
				TActorIterator<ACastleSupplyBalloon> Balloon(World);
				if (!Balloon || !Nav->IsNavigationReady()) { return false; }
				const FVector Location = Balloon->GetActorLocation();
				const uint64 TargetId = Balloon->GetTargetId();
				auto* Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
				if (!Test->TestNotNull(TEXT("正式玩家控制器"), Controller)) { return true; }
				bool bConsumed = false;
				// 无头 PIE 不创建有效视口，直接向正式左键射线入口提供确定的瞄准线；不绕过碰撞或命中判定。
				if (!Test->TestTrue(TEXT("左键 Visibility 射线锁定真实气球并释放货箱"),
					Controller->TryPopBalloonFromRay(Location + FVector(-650, 0, 180), Location, bConsumed) && bConsumed)) { return true; }
				FVector Unused;
				Test->TestFalse(TEXT("命中立即撤销气球目标"), World->GetSubsystem<UCastleSupportTowerSubsystem>()->GetBalloonTarget(TargetId, Unused));
				Stage = 2;
				return false;
			}
			FMassEntityManager& Manager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
			FMassEntityQuery Query(Manager.AsShared());
			Query.AddTagRequirement<FCastleMassNPCTag>(EMassFragmentPresence::All);
			Query.AddRequirement<FCastleMassPatrolFragment>(EMassFragmentAccess::ReadOnly);
			int32 NPCs = 0;
			int32 Diverting = 0;
			FMassExecutionContext Context(Manager, 0.0f, false);
			Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk)
			{
				NPCs += Chunk.GetNumEntities();
				for (const auto& Patrol : Chunk.GetFragmentView<FCastleMassPatrolFragment>()) { Diverting += Patrol.bSupportJourney ? 1 : 0; }
			});
			if (Stage == 2 && NPCs == 3)
			{
				Test->TestEqual(TEXT("32 的个位数招募 2 名，只调动原本在途的一人"), Diverting, 1);
				Stage = 3;
			}
			if (Stage == 3 && Diverting == 0)
			{
				Test->TestEqual(TEXT("临时路线走毕并解除，招募人员仍在场"), NPCs, 3);
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		double Deadline = FPlatformTime::Seconds() + 28.0;
		int32 Stage = 0;
	};

	/** 复用项目已有的编辑器停止接口，确保 PIE 释放后再继续执行其他测试。 */
	class FStopSupportPIE final : public IAutomationLatentCommand
	{
	public:
		explicit FStopSupportPIE(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (!FindSupportPIE()) { return true; }
			if (bRequested)
			{
				if (FPlatformTime::Seconds() <= Deadline) { return false; }
				Test->AddError(TEXT("支援塔验证 PIE 停止超时。"));
				return true;
			}
			UClass* Tool = FindObject<UClass>(nullptr, TEXT("/Script/EditorToolset.EditorAppToolset"));
			UFunction* Stop = Tool ? Tool->FindFunctionByName(TEXT("StopPIE")) : nullptr;
			if (!Stop) { Test->AddError(TEXT("缺少项目既有的 StopPIE 工具。")); return true; }
			struct FParams { UObject* ReturnValue = nullptr; } Params;
			Tool->GetDefaultObject()->ProcessEvent(Stop, &Params);
			bRequested = true;
			Deadline = FPlatformTime::Seconds() + 10.0;
			return false;
		}
	private:
		FAutomationTestBase* Test;
		bool bRequested = false;
		double Deadline = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleSupportMainMapTest, "MovingCastle.Support.MainMapBlueAirdrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleSupportMainMapTest::RunTest(const FString& Parameters)
{
	if (!AutomationOpenMap(TEXT("/Game/MovingCastle/Maps/MainMap"), true)) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FExerciseSupportMainMap(this));
	ADD_LATENT_AUTOMATION_COMMAND(FStopSupportPIE(this));
	return true;
}

#endif
