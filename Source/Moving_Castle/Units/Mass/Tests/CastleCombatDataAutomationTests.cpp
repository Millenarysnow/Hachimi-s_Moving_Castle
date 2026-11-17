#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Combat/CastleDifficultyCurveDataAsset.h"
#include "Units/Mass/CastleMassTraits.h"
#include "Units/Mass/CastleMassFragments.h"
#include "Units/Mass/CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace CastleCombatDataTests
{
	/** 建立没有美术依赖的完整合法配置，每个反例都从新对象出发，避免反例之间串扰。 */
	UCastleCombatConfigDataAsset* MakeConfig()
	{
		auto* Config = NewObject<UCastleCombatConfigDataAsset>();
		auto* Mass = NewObject<UMassEntityConfigAsset>(Config);
		Mass->GetMutableConfig().AddTrait(*NewObject<UCastleMassEnemyTrait>(Mass));
		FCastleCombatEnemyEntry Entry;
		Entry.EnemyId = TEXT("TestEnemy");
		Entry.Configuration = Mass;
		Config->Roster.Add(Entry);
		Config->DifficultyCurve = NewObject<UCastleDifficultyCurveDataAsset>(Config);
		FCastleCombatSpawnGroup Group;
		Group.EnemyId = Entry.EnemyId;
		Group.Count = 3;
		FCastleCombatBattleDefinition Battle;
		Battle.Groups.Add(Group);
		Config->DifficultyCurve->NormalBattles.Add(Battle);
		Battle.Groups[0].Count = 5;
		Battle.Groups[0].Delay = 8;
		Config->DifficultyCurve->NormalBattles.Add(Battle);
		Config->DifficultyCurve->EliteBattles.Add(Battle);
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatDataValidationTest, "MovingCastle.Combat.Data.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatDataValidationTest::RunTest(const FString& Parameters)
{
	using namespace CastleCombatDataTests;
	FText Failure;
	TestTrue(TEXT("完整配置合法"), MakeConfig()->ValidateConfiguration(Failure));
	struct FInvalidCase
	{
		const TCHAR* ExpectedField;
		TFunction<void(UCastleCombatConfigDataAsset&)> Mutate;
	};
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const float Infinity = std::numeric_limits<float>::infinity();
	// 每种 D2 拒绝路径均验证资产路径与字段；数组字段同时验证下标。
	const TArray<FInvalidCase> Cases = {
		{TEXT("Roster[0]"), [](auto& C) { C.Roster.Reset(); }},
		{TEXT("Roster[0].EnemyId"), [](auto& C) { C.Roster[0].EnemyId = NAME_None; }},
		{TEXT("Roster[1].EnemyId"), [](auto& C) { const auto Copy = C.Roster[0]; C.Roster.Add(Copy); }},
		{TEXT("Roster[0].Configuration"), [](auto& C) { C.Roster[0].Configuration = nullptr; }},
		{TEXT("Roster[0].Configuration"), [](auto& C) { C.Roster[0].Configuration = NewObject<UMassEntityConfigAsset>(); }},
		{TEXT("Roster[0].MinClusterCount/MaxClusterCount"), [](auto& C) { C.Roster[0].MinClusterCount = 0; }},
		{TEXT("Roster[0].MinClusterCount/MaxClusterCount"), [](auto& C) { C.Roster[0].MinClusterCount = 2; }},
		{TEXT("Roster[0].ClusterSpreadRadius"), [](auto& C) { C.Roster[0].ClusterSpreadRadius = -1; }},
		{TEXT("Roster[0].ClusterSpreadRadius"), [NaN](auto& C) { C.Roster[0].ClusterSpreadRadius = NaN; }},
		{TEXT("Roster[0].ClusterSpreadRadius"), [](auto& C) { C.Roster[0].MaxClusterCount = 2; }},
		{TEXT("DifficultyCurve"), [](auto& C) { C.DifficultyCurve = nullptr; }},
		{TEXT("NormalBattles[0]"), [](auto& C) { C.DifficultyCurve->NormalBattles.Reset(); }},
		{TEXT("EliteBattles[0]"), [](auto& C) { C.DifficultyCurve->EliteBattles.Reset(); }},
		{TEXT("NormalBattles[1].Groups[0]"), [](auto& C) { C.DifficultyCurve->NormalBattles[1].Groups.Reset(); }},
		{TEXT("EliteBattles[0].Groups[0].EnemyId"), [](auto& C) { C.DifficultyCurve->EliteBattles[0].Groups[0].EnemyId = NAME_None; }},
		{TEXT("NormalBattles[1].Groups[0].EnemyId"), [](auto& C) { C.DifficultyCurve->NormalBattles[1].Groups[0].EnemyId = TEXT("Unknown"); }},
		{TEXT("EliteBattles[0].Groups[0].Count"), [](auto& C) { C.DifficultyCurve->EliteBattles[0].Groups[0].Count = 0; }},
		{TEXT("NormalBattles[0].Groups[0].Delay"), [](auto& C) { C.DifficultyCurve->NormalBattles[0].Groups[0].Delay = -1; }},
		{TEXT("NormalBattles[0].Groups[0].Delay"), [NaN](auto& C) { C.DifficultyCurve->NormalBattles[0].Groups[0].Delay = NaN; }},
		{TEXT("NormalBattles[0].Groups[0].Delay"), [Infinity](auto& C) { C.DifficultyCurve->NormalBattles[0].Groups[0].Delay = Infinity; }},
		{TEXT("CountGrowthPerOrdinal"), [](auto& C) { C.DifficultyCurve->CountGrowthPerOrdinal = -0.1f; }},
		{TEXT("CountGrowthPerOrdinal"), [](auto& C) { C.DifficultyCurve->CountGrowthPerOrdinal = 10.1f; }},
		{TEXT("CountGrowthPerOrdinal"), [NaN](auto& C) { C.DifficultyCurve->CountGrowthPerOrdinal = NaN; }},
		{TEXT("SpawnInnerRadius/SpawnOuterRadius"), [](auto& C) { C.SpawnInnerRadius = -1; }},
		{TEXT("SpawnInnerRadius/SpawnOuterRadius"), [](auto& C) { C.SpawnOuterRadius = C.SpawnInnerRadius; }},
		{TEXT("SpawnInnerRadius/SpawnOuterRadius"), [NaN](auto& C) { C.SpawnInnerRadius = NaN; }},
		{TEXT("SpawnInnerRadius/SpawnOuterRadius"), [Infinity](auto& C) { C.SpawnOuterRadius = Infinity; }},
		{TEXT("SpawnHeightOffset"), [NaN](auto& C) { C.SpawnHeightOffset = NaN; }}
	};
	for (int32 Index = 0; Index < Cases.Num(); ++Index)
	{
		auto* Config = MakeConfig();
		Cases[Index].Mutate(*Config);
		const FString Label = FString::Printf(TEXT("反例 %d %s"), Index, Cases[Index].ExpectedField);
		TestFalse(Label, Config->ValidateConfiguration(Failure));
		TestTrue(Label + TEXT(" 带资产名"), Failure.ToString().Contains(Config->GetPathName()));
		TestTrue(Label + TEXT(" 带字段与下标"), Failure.ToString().Contains(Cases[Index].ExpectedField));
	}
	// 合法边界必须接纳，不能把严格校验错误地写成自动夹紧或过度拒绝。
	auto* Config = MakeConfig();
	Config->SpawnInnerRadius = 0;
	Config->Roster[0].MaxClusterCount = 8;
	Config->Roster[0].ClusterSpreadRadius = 120;
	Config->DifficultyCurve->CountGrowthPerOrdinal = 0;
	TestTrue(TEXT("零内半径、零增长、多实体簇正半径合法"), Config->ValidateConfiguration(Failure));
	Config->DifficultyCurve->CountGrowthPerOrdinal = 10;
	TestTrue(TEXT("增长率上界合法"), Config->ValidateConfiguration(Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatExtrapolationTest, "MovingCastle.Combat.Data.Extrapolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatExtrapolationTest::RunTest(const FString& Parameters)
{
	auto* Curve = CastleCombatDataTests::MakeConfig()->DifficultyCurve.Get();
	TArray<FCastleCombatSpawnGroup> Plan;
	FText Failure;
	TestTrue(TEXT("表内第一场"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, 1, Plan, Failure));
	TestEqual(TEXT("直接取第一项"), Plan[0].Count, 3);
	TestTrue(TEXT("表内末场"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, 2, Plan, Failure));
	TestEqual(TEXT("直接取末项"), Plan[0].Count, 5);
	TestTrue(TEXT("表外第一场"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, 3, Plan, Failure));
	TestEqual(TEXT("ceil(5 * 1.25)"), Plan[0].Count, 7);
	TestEqual(TEXT("延迟保持不变"), Plan[0].Delay, 8.0f);
	TestTrue(TEXT("表外第二场"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, 4, Plan, Failure));
	TestEqual(TEXT("ceil(5 * 1.5)"), Plan[0].Count, 8);
	TestTrue(TEXT("精英独立表长"), Curve->BuildBattlePlan(ECastleBattleKind::Elite, 2, Plan, Failure));
	TestEqual(TEXT("精英第二场已在表外"), Plan[0].Count, 7);
	TestFalse(TEXT("零序号拒绝"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, 0, Plan, Failure));
	TestTrue(TEXT("失败不输出半份计划"), Plan.IsEmpty());
	TestFalse(TEXT("非法种类拒绝"), Curve->BuildBattlePlan(static_cast<ECastleBattleKind>(255), 1, Plan, Failure));
	Curve->NormalBattles.Last().Groups[0].Count = MAX_int32;
	TestFalse(TEXT("外推溢出拒绝"), Curve->BuildBattlePlan(ECastleBattleKind::Normal, MAX_int32, Plan, Failure));
	TestTrue(TEXT("溢出原因有组下标"), Failure.ToString().Contains(TEXT("Groups[0].Count")));
	TestTrue(TEXT("原曲线没有被外推改写"), Curve->NormalBattles[0].Groups[0].Count == 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCombatEnemyCountTest, "MovingCastle.Combat.Data.EnemyCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleCombatEnemyCountTest::RunTest(const FString& Parameters)
{
	const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	auto& Manager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	auto* Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
	int32 Count = -1;
	TestTrue(TEXT("空世界计数成功"), Units->CountEnemyEntities(Count));
	TestEqual(TEXT("空世界计数零"), Count, 0);
	// 直接建立 Tag-only 实体证明查询不依赖生命片段，更不排除死亡但尚未回收者。
	const auto EnemyArchetype = Manager.CreateArchetype({FCastleMassEnemyTag::StaticStruct(), FCastleMassUnitFragment::StaticStruct()});
	const auto TagOnlyArchetype = Manager.CreateArchetype({FCastleMassEnemyTag::StaticStruct()});
	const auto OtherArchetype = Manager.CreateArchetype({FCastleMassNPCTag::StaticStruct()});
	const auto Dead = Manager.CreateEntity(EnemyArchetype);
	Manager.GetFragmentDataChecked<FCastleMassUnitFragment>(Dead).Health = 0;
	Manager.CreateEntity(TagOnlyArchetype);
	Manager.CreateEntity(OtherArchetype);
	TestTrue(TEXT("敌人计数成功"), Units->CountEnemyEntities(Count));
	TestEqual(TEXT("只按敌人 Tag 计数，包含死亡和无生命片段实体"), Count, 2);
	{
		auto Processing = Manager.NewProcessingScope();
		AddExpectedError(TEXT("CountEnemyEntities："), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("Mass 处理期间明确拒绝"), Units->CountEnemyEntities(Count));
	}
	Manager.DestroyEntity(Dead);
	TestTrue(TEXT("回收后计数成功"), Units->CountEnemyEntities(Count));
	TestEqual(TEXT("回收后才减少计数"), Count, 1);
	World->DestroyWorld(false);
	return true;
}

#endif
