#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleBuildingTestUtilities.h"
#include "NavigationSystem.h"
#include "CastleEconomyComponent.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleMassFragments.h"
#include "CastleMainTowerComponent.h"
#include "Components/Building/Effects/Attack/CastleCannonTowerAttackEffectComponent.h"
#include "Components/Building/Effects/Shield/CastleShieldTowerEffectComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Subsystems/CastleShieldTowerSubsystem.h"
#include "Units/Mass/Enemies/CastleMassMeleeAttackProcessor.h"
#include "Units/Mass/Enemies/CastleMassEnemyRangedAttackProcessor.h"
#include "Units/Mass/Enemies/CastleMassDeathSplitProcessor.h"
#include "Units/Mass/Projectiles/CastleMassEnemyProjectileProcessor.h"
#include "Units/Mass/Projectiles/CastleMassHomingProjectileProcessor.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "Components/Building/Effects/Attack/CastleTowerAttackEffectComponent.h"
#include "Components/Building/Effects/Farm/CastleFarmEffectComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "MassEntitySubsystem.h"
#include "Misc/AutomationTest.h"

namespace CastleBuildingLevelTests
{
	/** 在真实 Mass 处理边界执行正式处理器，不复制敌人索敌、伤害或分裂算法。 */
	template<typename T> void Step(UWorld* World, float DeltaTime)
	{
		auto& Manager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
		Manager.FlushCommands();
		T* Processor = NewObject<T>(World);
		Processor->CallInitialize(World, Manager.AsShared());
		UE::Mass::FProcessingContext Context(Manager, DeltaTime);
		UE::Mass::Executor::Run(*Processor, Context);
	}
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
				.CreatePhysicsScene(true).CreateNavigation(true).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            // PCG上下邻接会注册真实梯子，夹具必须显式提供动态Recast数据。
            check(FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)->GetDefaultNavDataInstance(FNavigationSystem::Create));
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
			// 显式进入世界关闭状态，让梯子注销不再向已退出的Recast数据请求刷新。
            World->BeginTearingDown();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCannonLevelEffectsTest, "MovingCastle.BuildingEffects.Levels.CannonAmmunitionAndQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCannonLevelEffectsTest::RunTest(const FString& Parameters)
{
	using namespace CastleBuildingLevelTests;
	const int32 ExpectedCount[] = {0, 2, 3, 4, 4};
	const float ExpectedDamage[] = {30, 90, 120, 150, 190};
	for (int32 Level = 1; Level <= 5; ++Level)
	{
		FWorld Scope(this);
		const int32 Width = Level >= 2 && Level <= 4 ? 2 : 1;
		const int32 Depth = Level >= 3 && Level <= 4 ? 2 : 1;
		const int32 Height = Level == 4 ? 2 : 1;
		ACastleBuildingBase* Cannon = Scope.Place(ECastleBuildingType::CannonTower, Level, 3, FIntVector(3, 3, 0));
		ACastleBuildingBase* Ammo = Scope.Place(ECastleBuildingType::NormalRoom, 1, 3, FIntVector(3 + Width, 3, 0));
		if (!Cannon || !Ammo || !Scope.Place(ECastleBuildingType::Farm, 2, 3, FIntVector(3, 3 + Depth, 0))
			|| !Scope.Place(ECastleBuildingType::NormalRoom, 1, 3, FIntVector(3, 3, Height))
			|| !Scope.Place(ECastleBuildingType::NormalRoom, 1, 3, FIntVector(4 + Width, 3, 0))
			|| !Scope.Place(ECastleBuildingType::NormalRoom, 1, 4, FIntVector(2, 3, 0))) { return false; }
		auto* Effect = CastChecked<UCastleCannonTowerAttackEffectComponent>(Cannon->BuildingEffect);
		TArray<ACastleBuildingBase*> Ammunition;
		TestTrue(TEXT("炮塔查询当前弹药库"), Effect->GetAmmunitionBuildings(Ammunition));
		TestEqual(FString::Printf(TEXT("Lv%d 弹药库范围及同数字去重"), Level), Ammunition.Num(), ExpectedCount[Level - 1]);
		TestTrue(TEXT("炮塔本体触发只增加一发"), Cannon->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
		TestEqual(TEXT("单次触发只入队一发全额伤害"), Effect->PendingShotCount, int64(1));
		TestEqual(TEXT("Lv5 弹药库每座加一，自身不加一"), Effect->GetPendingShotDamages()[0], ExpectedDamage[Level - 1]);
		TestTrue(TEXT("普通房间仍允许主动调用基础效果"), Ammo->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
		TestEqual(TEXT("D10 附属弹药库 Manual 不联动炮塔"), Effect->PendingShotCount, int64(1));
		FCastleBuildingTriggerContext Context = Scope.MakeNPC();
		Scope.World->GetSubsystem<UCastleMassUnitSubsystem>()->AddNPCFarmBuff(Context.NPC, 0.6f);
		{
			auto Processing = Scope.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager().NewProcessingScope();
			TestTrue(TEXT("NPC 进入普通房间弹药库"), Ammo->TriggerBuildingEffect(Context));
		}
		TestEqual(TEXT("一级无联动，其余普通房间进入精确联动一次"), Effect->PendingShotCount, Level == 1 ? int64(1) : int64(2));
		if (Level > 1)
		{
			TestTrue(TEXT("炮击保留小数 NPC 增益，不做箭塔式取整"), FMath::IsNearlyEqual(Effect->GetPendingShotDamages()[1], ExpectedDamage[Level - 1] + 6.0f));
		}
		// 无目标是唯一授权的作废分支；第一发后即使队列仍有项，也必须等待完整一秒冷却。
		AddExpectedMessagePlain(TEXT("本次待射作废：攻击半径内没有存活敌人"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, Level == 1 ? 1 : 2);
		static_cast<UActorComponent*>(Effect)->TickComponent(0, LEVELTICK_All, nullptr);
		TestEqual(TEXT("首个 Tick 只消费一发"), Effect->PendingShotCount, Level == 1 ? int64(0) : int64(1));
		Scope.World->TimeSeconds += 0.9;
		static_cast<UActorComponent*>(Effect)->TickComponent(0, LEVELTICK_All, nullptr);
		TestEqual(TEXT("一秒内不提前消耗下发"), Effect->PendingShotCount, Level == 1 ? int64(0) : int64(1));
		Scope.World->TimeSeconds += 0.11;
		static_cast<UActorComponent*>(Effect)->TickComponent(0, LEVELTICK_All, nullptr);
		TestEqual(TEXT("冷却结束消费剩余项，伤害队列同步清空"), Effect->GetPendingShotDamages().Num(), 0);
	}
	// 两炮塔互为弹药库时，本体触发只让对方各入队一次，不再触发把对方收录的自身。
	FWorld Scope(this);
	ACastleBuildingBase* A = Scope.Place(ECastleBuildingType::CannonTower, 2, 3, FIntVector(3, 3, 0));
	ACastleBuildingBase* B = Scope.Place(ECastleBuildingType::CannonTower, 2, 3, FIntVector(5, 3, 0));
	if (!A || !B) { return false; }
	TestTrue(TEXT("互为弹药库的炮塔本体 NPC 进入正常完成"), A->TriggerBuildingEffect(Scope.MakeNPC()));
	TestEqual(TEXT("源炮塔只触发一次"), A->BuildingEffect->TriggerCount, int64(1));
	TestEqual(TEXT("联动炮塔只触发一次，没有递归"), B->BuildingEffect->TriggerCount, int64(1));
	FText Reason;
	TestTrue(TEXT("回收炮塔成功并解绑监听"), Scope.Board->RemoveBuilding(B, Reason));
	TestTrue(TEXT("回收后进入源炮塔不调用旧监听"), A->TriggerBuildingEffect(Scope.MakeNPC()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShieldLevelEffectsTest, "MovingCastle.BuildingEffects.Levels.ShieldConnectivityRefreshAndPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShieldLevelEffectsTest::RunTest(const FString& Parameters)
{
	using namespace CastleBuildingLevelTests;
	const int64 Expected[] = {3, 18, 18, 27, 108};
	for (int32 Level = 1; Level <= 5; ++Level)
	{
		FWorld Scope(this);
		const int32 Width = Level >= 2 && Level <= 4 ? 2 : 1;
		const int32 Depth = Level >= 3 && Level <= 4 ? 2 : 1;
		auto* Tower = Cast<ACastleShieldTowerBuilding>(Scope.Place(ECastleBuildingType::ShieldTower, Level, 3, FIntVector(3, 3, 0)));
		if (!Tower || !Scope.Place(ECastleBuildingType::NormalRoom, 1, 4, FIntVector(3 + Width, 3, 0))
			|| !Scope.Place(ECastleBuildingType::ArcherTower, 2, 5, FIntVector(4 + Width, 3, 0))
			|| !Scope.Place(ECastleBuildingType::CannonTower, 1, 6, FIntVector(6 + Width, 3, 0))
			|| !Scope.Place(ECastleBuildingType::Farm, 1, 1, FIntVector(3, 3 + Depth, 0), ECastleTileColor::Blue)
			|| !Scope.Place(ECastleBuildingType::Farm, 1, 8, FIntVector(3, 4 + Depth, 0))) { return false; }
		auto* Effect = CastChecked<UCastleShieldTowerEffectComponent>(Tower->BuildingEffect);
		int64 Computed = 0;
		TestTrue(TEXT("盾塔计算次数"), Effect->CalculateShieldBlocks(Computed));
		TestEqual(FString::Printf(TEXT("Lv%d 传递颜色和实例攻击塔倍率准确"), Level), Computed, Expected[Level - 1]);
		TestTrue(TEXT("盾塔真实入口激活世界护盾"), Tower->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
		TestEqual(TEXT("通知实时同步剩余次数"), Effect->RemainingBlocks, Computed);
		TestTrue(TEXT("球体和次数文字同时可见"), Tower->ShieldBubble->IsVisible() && Tower->ShieldRemainingText->IsVisible());
		TestTrue(TEXT("次数文字位于真实美术顶部上方，不能被高模型屋顶遮挡"), Tower->ShieldRemainingText->GetComponentLocation().Z > Tower->GetVisualBounds().Max.Z);
		TestEqual(TEXT("球体明确无碰撞"), Tower->ShieldBubble->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("球体不参与导航"), Tower->ShieldBubble->CanEverAffectNavigation());
		TestTrue(TEXT("球体排除合并"), Tower->ShieldBubble->ComponentTags.Contains(TEXT("MergeExclude")));
		UCastleShieldTowerSubsystem* Shields = Scope.World->GetSubsystem<UCastleShieldTowerSubsystem>();
		Shields->UpsertActiveShield(Tower, Tower->GetActorLocation(), 1500, Computed + 5, 0);
		TestTrue(TEXT("已有更大次数时刷新成功"), Tower->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
		TestEqual(TEXT("重复触发取 Max 不降低剩余次数"), Effect->RemainingBlocks, Computed + 5);
		Shields->UpsertActiveShield(Tower, Tower->GetActorLocation(), 1500, 1, 0);
		TestTrue(TEXT("已有更小次数时刷新成功"), Tower->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
		TestEqual(TEXT("较小剩余刷新到本次计算而非累加"), Effect->RemainingBlocks, Computed);
		Shields->DeactivateShield(Tower);
		TestTrue(TEXT("归零只隐藏表现，建筑和占用仍保留"), !Tower->ShieldBubble->IsVisible() && !Tower->ShieldRemainingText->IsVisible()
			&& IsValid(Tower) && ACastleBoard::FindBoardForBuilding(Tower) == Scope.Board);
		Tower->TriggerBuildingEffect(FCastleBuildingTriggerContext());
		FText Reason;
		Scope.Board->RemoveBuilding(Tower, Reason);
		TestFalse(TEXT("回收同步注销世界护盾"), Shields->GetShieldRemainingBlocks(Tower, Computed));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingConcreteCombatTest, "MovingCastle.BuildingEffects.Levels.ConcreteEnemyCombatLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingConcreteCombatTest::RunTest(const FString& Parameters)
{
	using namespace CastleBuildingLevelTests;
	FWorld Scope(this);
	// 主塔需要在各兵种场景之间移动；显式提供有根组件的 Actor，空 AActor 不能设置世界位置。
	AActor* MainActor = Scope.World->SpawnActor<AStaticMeshActor>();
	auto* Main = NewObject<UCastleMainTowerComponent>(MainActor);
	MainActor->AddInstanceComponent(Main);
	Main->TargetHeightOffset = 0;
	Main->InitializeHealth();
	Main->RegisterComponent();
	Scope.World->GetSubsystem<UCastleMainTowerSubsystem>()->RegisterMainTower(Main);
	UCastleMassUnitSubsystem* Units = Scope.World->GetSubsystem<UCastleMassUnitSubsystem>();
	auto& Manager = Scope.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	UCastleShieldTowerSubsystem* Shields = Scope.World->GetSubsystem<UCastleShieldTowerSubsystem>();
	auto* Shield = Cast<ACastleShieldTowerBuilding>(Scope.Place(ECastleBuildingType::ShieldTower, 3, 9, FIntVector(3, 3, 0)));
	if (!Shield) { return false; }
	const FVector Center = Shield->GetActorLocation();
	MainActor->SetActorLocation(Center - FVector(350.0f, 0.0f, 0.0f));
	FText Reason;
	const auto Spawn = [&](const TCHAR* Name, FVector Location, int32 Count, TArray<FCastleMassUnitHandle>& Out)
	{
		const FString Path = FString(TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_")) + Name;
		auto* Config = LoadObject<UMassEntityConfigAsset>(nullptr, *Path);
		TArray<FTransform> Transforms;
		for (int32 Index = 0; Index < Count; ++Index) { Transforms.Emplace(Location); }
		return TestNotNull(TEXT("真实敌人 DA 存在"), Config) && Units->SpawnUnits(Config, Transforms, Out, Reason);
	};
	TestTrue(TEXT("三级盾塔原生入口展开"), Shield->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
	TArray<FCastleMassUnitHandle> Giant;
	if (!Spawn(TEXT("LavaGoliath"), Center, 1, Giant)) { AddError(Reason.ToString()); return false; }
	// 通过正式伤害接口预先压低血量，使三级自身数字9的反伤成为真正的致死来源。
	FCastleMassUnitSnapshot Snapshot;
	Units->GetUnitSnapshot(Giant[0], Snapshot);
	TestTrue(TEXT("真实巨骸可预伤到个位血量"), Units->ApplyImmediateDamageToEnemy(Giant[0], Snapshot.Health - 8));
	Step<UCastleMassMeleeAttackProcessor>(Scope.World, 0);
	Units->GetUnitSnapshot(Giant[0], Snapshot);
	TestEqual(TEXT("真实巨骸优先攻击护盾，主塔未扣血"), Main->CurrentHealth, Main->MaxHealth);
	TestEqual(TEXT("三级盾塔自身数字9反伤击杀真实巨骸"), Snapshot.Health, 0.0f);
	TestEqual(TEXT("一次攻击只消耗一次护盾"), CastChecked<UCastleShieldTowerEffectComponent>(Shield->BuildingEffect)->RemainingBlocks, int64(8));
	Step<UCastleMassDeathSplitProcessor>(Scope.World, 0);
	int32 Pups = 0;
	FMassEntityQuery Query(Manager.AsShared());
	Query.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
	FMassExecutionContext QueryContext(Manager, 0, false);
	Query.ForEachEntityChunk(QueryContext, [&Pups](FMassExecutionContext& Chunk)
	{
		const auto Data = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (Data[It].Health == 20) { ++Pups; }
		}
	});
	TestEqual(TEXT("反伤死亡经正式分裂处理器按正式 SpawnCount 生成六只幼骸"), Pups, 6);
	// 关闭第一座护盾，远程敌人在1500吸引圈外、2200射程内仍会消耗第二座护盾。
	Shields->DeactivateShield(Shield);
	auto* RangedShield = Cast<ACastleShieldTowerBuilding>(Scope.Place(ECastleBuildingType::ShieldTower, 1, 2, FIntVector(8, 3, 0)));
	if (!RangedShield || !RangedShield->TriggerBuildingEffect(FCastleBuildingTriggerContext())) { return false; }
	TArray<FCastleMassUnitHandle> Welp;
	if (!Spawn(TEXT("MiasmaWelp"), RangedShield->GetActorLocation() + FVector(1800, 0, 0), 1, Welp)) { return false; }
	Step<UCastleMassEnemyRangedAttackProcessor>(Scope.World, 0);
	Units->GetUnitSnapshot(Welp[0], Snapshot);
	TestTrue(TEXT("瘴翼龙崽在吸引圈外实际发出远程弹"), Snapshot.AttackCount == 1 && Snapshot.bInAttackRange);
	Step<UCastleMassEnemyProjectileProcessor>(Scope.World, 2);
	TestEqual(TEXT("远程命中实时同步球体剩余次数"), CastChecked<UCastleShieldTowerEffectComponent>(RangedShield->BuildingEffect)->RemainingBlocks, int64(1));
	Shields->DeactivateShield(RangedShield);
	// 六只幼骸由前段真实分裂得到；先在其位置之外设置群虫护盾，避免它们消费本段次数。
	auto* SwarmShield = Cast<ACastleShieldTowerBuilding>(Scope.Place(ECastleBuildingType::ShieldTower, 1, 1, FIntVector(8, 7, 0)));
	if (!SwarmShield) { return false; }
	const FVector SwarmAttackLocation = Center;
	SwarmShield->SetActorLocation(SwarmAttackLocation);
	if (!SwarmShield->TriggerBuildingEffect(FCastleBuildingTriggerContext())) { return false; }
	TArray<FCastleMassUnitHandle> Swarm;
	if (!Spawn(TEXT("ShadowSwarm"), SwarmAttackLocation, 6, Swarm)) { return false; }
	Step<UCastleMassMeleeAttackProcessor>(Scope.World, 0);
	int64 Blocks = 0;
	TestFalse(TEXT("真实群虫围攻耗尽最后一次护盾"), Shields->GetShieldRemainingBlocks(SwarmShield, Blocks));
	TestFalse(TEXT("破盾通知隐藏球体"), SwarmShield->ShieldBubble->IsVisible());
	TestTrue(TEXT("同批后续群虫破盾后立即转火主塔"), Main->CurrentHealth < Main->MaxHealth);
	TestTrue(TEXT("破盾保留盾塔建筑"), IsValid(SwarmShield) && ACastleBoard::FindBoardForBuilding(SwarmShield) == Scope.Board);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleFarmCannonProjectileTest, "MovingCastle.BuildingEffects.Levels.FarmBoostedCannonHomingHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleFarmCannonProjectileTest::RunTest(const FString& Parameters)
{
	using namespace CastleBuildingLevelTests;
	FWorld Scope(this);
	Scope.Board->FindBuildingDefinition(ECastleBuildingType::CannonTower)->AttackData = LoadObject<UCastleTowerAttackDataAsset>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_CannonTowerAttack.DA_CannonTowerAttack"));
	ACastleBuildingBase* Farm = Scope.Place(ECastleBuildingType::Farm, 5, 3, FIntVector(3,3,0));
	ACastleBuildingBase* Cannon = Scope.Place(ECastleBuildingType::CannonTower, 5, 3, FIntVector(4,3,0));
	if (!Farm || !Cannon) { return false; }
	auto* Attack = CastChecked<UCastleCannonTowerAttackEffectComponent>(Cannon->BuildingEffect);
	FCastleBuildingTriggerContext Context = Scope.MakeNPC();
	UCastleMassUnitSubsystem* Units = Scope.World->GetSubsystem<UCastleMassUnitSubsystem>();
	{
		auto Processing = Scope.World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager().NewProcessingScope();
		TestTrue(TEXT("NPC 进入五级农田在处理期间产金并联动炮塔"), Farm->TriggerBuildingEffect(Context));
	}
	if (!TestEqual(TEXT("一次农田进入只联动一发"), Attack->GetPendingShotDamages().Num(), 1)) { return false; }
	TestTrue(TEXT("五级弹药库加一及本次新农田增益同帧计入"), FMath::IsNearlyEqual(Attack->GetPendingShotDamages()[0], 73.0f));
	Units->AddNPCFarmBuff(Context.NPC, 1.5f);
	UMassEntityConfigAsset* EnemyConfig = NewObject<UMassEntityConfigAsset>(Scope.World);
	auto* EnemyTrait = NewObject<UCastleMassEnemyTrait>(EnemyConfig);
	EnemyTrait->MaxHealth = 200;
	EnemyConfig->GetMutableConfig().AddTrait(*EnemyTrait);
	TArray<FCastleMassUnitHandle> Enemies;
	FText Reason;
	if (!Units->SpawnUnits(EnemyConfig, {FTransform(Cannon->GetActorLocation() + FVector(200,0,260))}, Enemies, Reason))
	{
		AddError(Reason.ToString()); return false;
	}
	static_cast<UActorComponent*>(Attack)->TickComponent(0, LEVELTICK_All, nullptr);
	Step<UCastleMassHomingProjectileProcessor>(Scope.World, 1);
	FCastleMassUnitSnapshot Snapshot;
	TestTrue(TEXT("真实追踪炮弹命中后敌人可查询"), Units->GetUnitSnapshot(Enemies[0], Snapshot));
	TestTrue(TEXT("NPC 后续增益不改写已入队炮击，实际扣除73点生命"), FMath::IsNearlyEqual(Snapshot.Health, 127.0f));
	TestEqual(TEXT("生成成功后两条队列一起消费"), Attack->PendingShotCount, int64(0));
	return true;
}

#endif
