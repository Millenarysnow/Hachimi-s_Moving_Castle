#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleBuildingTestUtilities.h"
#include "CastleEconomyComponent.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Components/Building/Effects/Attack/CastleTowerAttackEffectComponent.h"
#include "Components/Building/Effects/Farm/CastleFarmEffectComponent.h"
#include "Engine/Engine.h"
#include "MassEntitySubsystem.h"
#include "Misc/AutomationTest.h"

namespace CastleBuildingLevelTests
{
	/** 显式测试输入：真实棋盘、完整放置动画、独立账户，不修改生产资产或默认对象。 */
	struct FWorld
	{
		UWorld* World;
		ACastleBoard* Board;
		UCastleEconomyComponent* Economy;
		FAutomationTestBase* Test;
		TUniquePtr<CastleBuildingTests::FScopedNativeBuildingModels> Models;

		explicit FWorld(FAutomationTestBase* InTest) : Test(InTest)
		{
			const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
				.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Models = MakeUnique<CastleBuildingTests::FScopedNativeBuildingModels>(World);
			Board = World->SpawnActor<ACastleBoard>();
			Board->GridSize = FIntVector(12, 10, 10);
			CastleBuildingTests::ConfigureBoard(Board);
			Economy = NewObject<UCastleEconomyComponent>(Board);
			FCastleGroundGenerationResult Ground;
			for (int32 X = 0; X < 12; ++X)
			{
				for (int32 Y = 0; Y < 10; ++Y)
				{
					FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
					Cell.Coordinate = FIntVector(X, Y, 0);
					Cell.Color = ECastleTileColor::Red;
					Cell.Number = 1;
				}
			}
			FText Reason;
			if (!Board->ApplyGroundGenerationResult(Ground, Reason)) { Test->AddError(Reason.ToString()); }
		}

		~FWorld()
		{
			Models.Reset();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}

		/** 始终经过正式卡牌工厂、占用校验和飞入完成信号，确保查询真实登记的实例。 */
		ACastleBuildingBase* Place(ECastleBuildingType Type, int32 Level, int32 Number, FIntVector Cell,
			ECastleTileColor Color = ECastleTileColor::Red)
		{
			FCastleBuildingDescriptor Card;
			FText Reason;
			if (!Board->FindBuildingDefinition(Type)->CreateDescriptor(Level, Color, Number, Card, Reason))
			{
				Test->AddError(Reason.ToString());
				return nullptr;
			}
			ACastleBuildingBase* Result = Board->PlaceBuilding(Cell, Card, false, Reason);
			if (!Result || !CastleBuildingTests::CompletePlacement(Result))
			{
				Test->AddError(FString::Printf(TEXT("测试放置失败：类型=%d 等级=%d 格=%s，%s"), static_cast<int32>(Type), Level, *Cell.ToString(), *Reason.ToString()));
				return nullptr;
			}
			if (auto* Farm = Cast<UCastleFarmEffectComponent>(Result->BuildingEffect)) { Farm->EconomyOverride = Economy; }
			return Result;
		}

		/** NPC 使用真实 Mass Trait，才能检查处理期间同步增益和来源校验。 */
		FCastleBuildingTriggerContext MakeNPC()
		{
			auto* Config = NewObject<UMassEntityConfigAsset>(World);
			Config->GetMutableConfig().AddTrait(*NewObject<UCastleMassNPCTrait>(Config));
			TArray<FCastleMassUnitHandle> Handles;
			FText Reason;
			FCastleBuildingTriggerContext Context;
			Context.Source = ECastleBuildingTriggerSource::NPCEntry;
			if (!World->GetSubsystem<UCastleMassUnitSubsystem>()->SpawnUnits(Config, {FTransform::Identity}, Handles, Reason))
			{
				Test->AddError(Reason.ToString());
				return Context;
			}
			Context.NPC = Handles[0];
			return Context;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleArcherLevelNumbersTest, "MovingCastle.BuildingEffects.Levels.ArcherStackAndFarmBuff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleArcherLevelNumbersTest::RunTest(const FString& Parameters)
{
	using namespace CastleBuildingLevelTests;
	const int64 Expected[] = {2, 6, 9, 9, 11};
	for (int32 Level = 1; Level <= 5; ++Level)
	{
		FWorld Scope(this);
		// 每层 2x2 底面：同一个八格实例同时命中两层/多列，也只能贡献一次数字。
		if (!Scope.Place(ECastleBuildingType::CannonTower, 4, 2, FIntVector(3, 3, 0))
			|| !Scope.Place(ECastleBuildingType::Farm, 4, 3, FIntVector(3, 3, 2))
			|| !Scope.Place(ECastleBuildingType::Farm, 3, 4, FIntVector(3, 3, 4))) { return false; }
		ACastleBuildingBase* Tower = Scope.Place(ECastleBuildingType::ArcherTower, Level, 2, FIntVector(3, 3, 5));
		if (!Tower) { return false; }
		auto* Attack = CastChecked<UCastleTowerAttackEffectComponent>(Tower->BuildingEffect);
		TestTrue(TEXT("空间查询准确定位所属棋盘"), ACastleBoard::FindBoardForBuilding(Tower) == Scope.Board);
		TestTrue(TEXT("手动触发按等级堆叠装填"), Tower->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
		TestEqual(FString::Printf(TEXT("Lv%d 多格下层数字去重"), Level), Attack->PendingShotCount, Expected[Level - 1]);
		FCastleBuildingTriggerContext Context = Scope.MakeNPC();
		if (!Scope.World->GetSubsystem<UCastleMassUnitSubsystem>()->AddNPCFarmBuff(Context.NPC, 1.8f)) { return false; }
		{
			auto Processing = Scope.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager().NewProcessingScope();
			TestTrue(TEXT("Mass 处理中可以同步读 NPC 增益并入队"), Tower->TriggerBuildingEffect(Context));
		}
		TestEqual(TEXT("只对最终总和向下取整，1.8 增益增加一箭"), Attack->PendingShotCount, Expected[Level - 1] * 2 + 1);
		TestEqual(TEXT("两次进入均记录一次，箭数不充当触发次数"), Attack->TriggerCount, int64(2));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleFarmLevelNumbersTest, "MovingCastle.BuildingEffects.Levels.FarmConnectivityAndIncome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleFarmLevelNumbersTest::RunTest(const FString& Parameters)
{
	using namespace CastleBuildingLevelTests;
	const int32 Expected[] = {3, 12, 19, 21, 21};
	for (int32 Level = 1; Level <= 5; ++Level)
	{
		FWorld Scope(this);
		const int32 Width = Level >= 2 && Level <= 4 ? 2 : 1;
		const int32 Depth = Level >= 3 && Level <= 4 ? 2 : 1;
		const int32 Height = Level == 4 ? 2 : 1;
		ACastleBuildingBase* Farm = Scope.Place(ECastleBuildingType::Farm, Level, 3, FIntVector(3, 3, 0));
		if (!Farm || !Scope.Place(ECastleBuildingType::Farm, 2, 4, FIntVector(3 + Width, 3, 0))
			|| !Scope.Place(ECastleBuildingType::Farm, 1, 2, FIntVector(5 + Width, 3, 0))
			|| !Scope.Place(ECastleBuildingType::Farm, 1, 5, FIntVector(3, 3 + Depth, 0))
			|| !Scope.Place(ECastleBuildingType::Farm, 1, 1, FIntVector(2, 3, 0), ECastleTileColor::Blue)
			|| !Scope.Place(ECastleBuildingType::Farm, 1, 6, FIntVector(2, 2, 0))
			|| !Scope.Place(ECastleBuildingType::Farm, 1, 7, FIntVector(3, 3, Height))) { return false; }
		TestTrue(TEXT("农田手动触发一次入账"), Farm->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
		TestEqual(FString::Printf(TEXT("Lv%d 同色、距离与六面传递范围正确"), Level), Scope.Economy->GetGold(), Expected[Level - 1]);
		FCastleBuildingTriggerContext Context = Scope.MakeNPC();
		{
			auto Processing = Scope.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager().NewProcessingScope();
			TestTrue(TEXT("真实 NPC 连续进入农田两次均成功"), Farm->TriggerBuildingEffect(Context) && Farm->TriggerBuildingEffect(Context));
		}
		float Bonus = -1.0f;
		TestTrue(TEXT("NPC 增益可同步读取"), Scope.World->GetSubsystem<UCastleMassUnitSubsystem>()->GetNPCFarmBuff(Context.NPC, Bonus));
		TestTrue(TEXT("只有五级累加本座自身数字 10%，连通总和不参与"), FMath::IsNearlyEqual(Bonus, Level == 5 ? 0.6f : 0.0f));
		TestEqual(TEXT("每次触发独立产金，无动画锁或同帧去重"), Scope.Economy->GetGold(), Expected[Level - 1] * 3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingTopologyFailuresTest, "MovingCastle.BuildingEffects.Levels.TopologyAndFarmFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingTopologyFailuresTest::RunTest(const FString& Parameters)
{
	using namespace CastleBuildingLevelTests;
	FWorld Scope(this);
	ACastleBuildingBase* Source = Scope.Place(ECastleBuildingType::Farm, 2, 3, FIntVector(3, 3, 0));
	ACastleBuildingBase* Neighbor = Scope.Place(ECastleBuildingType::Farm, 2, 4, FIntVector(5, 3, 0));
	if (!Source || !Neighbor) { return false; }
	TArray<ACastleBuildingBase*> Found;
	TestTrue(TEXT("任一占用格命中外扩范围即收录整个多格建筑"), Scope.Board->GetBuildingsInExpandedBounds(Source, FIntVector(1, 1, 0), Found));
	TestEqual(TEXT("多格来源与邻居各出现一次"), Found.Num(), 2);
	Neighbor->SetPlacementComplete(false);
	TestTrue(TEXT("建造中实例不参与连通产金"), Source->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
	TestEqual(TEXT("未完成邻居不贡献数字"), Scope.Economy->GetGold(), 3);
	Neighbor->SetPlacementComplete(true);
	TestTrue(TEXT("完成建造后立即参与，无陈旧拓扑缓存"), Source->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
	TestEqual(TEXT("多格邻居只贡献一次数字"), Scope.Economy->GetGold(), 10);
	FText Reason;
	TestTrue(TEXT("明确制造余额边界"), Scope.Economy->TryAddGold(MAX_int32 - 10, Reason));
	AddExpectedError(TEXT("无法入账"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("金币溢出明确拒绝，不返回看似成功"), Source->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
	TestEqual(TEXT("失败不增加成功触发计数"), Source->BuildingEffect->TriggerCount, int64(2));
	TestEqual(TEXT("失败保留原余额"), Scope.Economy->GetGold(), MAX_int32);
	CastChecked<UCastleFarmEffectComponent>(Source->BuildingEffect)->EconomyOverride = nullptr;
	AddExpectedError(TEXT("缺少有效金币账户"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("缺账户明确报错，不能临时创建或改用其他账户"), Source->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
	AddExpectedError(TEXT("空间查询失败"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("无效源拒绝空间查询"), Scope.Board->GetBuildingsBelow(nullptr, 1, Found));
	return true;
}

#endif
