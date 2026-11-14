#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Subsystems/CastleShieldTowerSubsystem.h"
#include "CastleMainTowerComponent.h"
#include "CastleMassFragments.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/AutomationTest.h"

namespace CastleMassConcreteTests
{
	/** 各测试使用独立游戏世界，避免护盾 ID、主塔或实体句柄串到其他测试。 */
	struct FWorld
	{
		UWorld* World = nullptr;
		UCastleMainTowerComponent* Tower = nullptr;
		FWorld()
		{
			const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
				.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			AActor* Actor = World->SpawnActor<AActor>();
			Tower = NewObject<UCastleMainTowerComponent>(Actor);
			Actor->AddInstanceComponent(Tower);
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
		FMassEntityManager& Manager() const { return World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager(); }
		UCastleMassUnitSubsystem& Units() const { return *World->GetSubsystem<UCastleMassUnitSubsystem>(); }
		UCastleShieldTowerSubsystem& Shields() const { return *World->GetSubsystem<UCastleShieldTowerSubsystem>(); }
	};

	/** 按实际 Mass 执行边界运行单个处理器，验证延迟创建和生命周期的先后关系。 */
	template<typename T> void Step(FWorld& Scope, float DeltaTime)
	{
		Scope.Manager().FlushCommands();
		T* Processor = NewObject<T>(Scope.World);
		Processor->CallInitialize(Scope.World, Scope.Manager().AsShared());
		UE::Mass::FProcessingContext Context(Scope.Manager(), DeltaTime);
		UE::Mass::Executor::Run(*Processor, Context);
	}

	template<typename T> UMassEntityConfigAsset* Config()
	{
		auto* Asset = NewObject<UMassEntityConfigAsset>();
		Asset->GetMutableConfig().AddTrait(*NewObject<T>(Asset));
		return Asset;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShieldContractTest, "MovingCastle.Mass.ShieldContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleShieldContractTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassConcreteTests;
	FWorld Scope;
	UObject* A = Scope.World->SpawnActor<AActor>();
	UObject* B = Scope.World->SpawnActor<AActor>();
	TArray<int64> Changes;
	Scope.Shields().OnShieldStateChanged.AddLambda([&Changes](UObject*, int64 Blocks) { Changes.Add(Blocks); });
	const uint64 Id = Scope.Shields().UpsertActiveShield(A, FVector::ZeroVector, 1500.0f, 2, 7.0f);
	TestTrue(TEXT("激活返回非零 ID"), Id != 0);
	TestEqual(TEXT("更新同一激活态保留 ID"), Scope.Shields().UpsertActiveShield(A, FVector::ZeroVector, 1500.0f, 3, 7.0f), Id);
	const uint64 Other = Scope.Shields().UpsertActiveShield(B, FVector(4000, 0, 0), 1500.0f, 1, 0.0f);
	uint64 Found = 0;
	FVector Location;
	TestFalse(TEXT("近战在吸引圈外找不到盾"), Scope.Shields().FindNearestActiveShield(FVector(1800, 0, 0), MAX_flt, true, Found, Location));
	TestTrue(TEXT("远程在 2200 内可锁定吸引圈外护盾"), Scope.Shields().FindNearestActiveShield(FVector(1800, 0, 0), 2200.0f, false, Found, Location));
	TestEqual(TEXT("选取最近护盾"), Found, Id);
	TestFalse(TEXT("最大射程同样约束近战候选"), Scope.Shields().FindNearestActiveShield(FVector(1400, 0, 0), 1300.0f, true, Found, Location));
	TestTrue(TEXT("吸引圈边界可命中"), Scope.Shields().FindNearestActiveShield(FVector(1500, 0, 0), 1500.0f, true, Found, Location));
	TArray<FCastleMassUnitHandle> Enemies;
	FText Reason;
	if (!TestTrue(TEXT("生成反伤攻击者"), Scope.Units().SpawnUnits(Config<UCastleMassEnemyTrait>(), {FTransform::Identity}, Enemies, Reason))) { return false; }
	int64 Blocks = 0;
	TestTrue(TEXT("第一次扣盾"), Scope.Shields().ApplyAttackToShield(Id, Enemies[0], Blocks));
	TestEqual(TEXT("每次只扣一层"), Blocks, int64(2));
	FCastleMassUnitSnapshot Snapshot;
	Scope.Units().GetUnitSnapshot(Enemies[0], Snapshot);
	TestEqual(TEXT("反伤即时扣血无需 Flush"), Snapshot.Health, 93.0f);
	Scope.Shields().ApplyAttackToShield(Id, Enemies[0], Blocks);
	Scope.Shields().ApplyAttackToShield(Id, Enemies[0], Blocks);
	TestEqual(TEXT("最后一次扣到零"), Blocks, int64(0));
	TestFalse(TEXT("破盾自动注销"), Scope.Shields().GetShieldRemainingBlocks(A, Blocks));
	TestFalse(TEXT("旧 ID 不能再次扣盾"), Scope.Shields().ApplyAttackToShield(Id, Enemies[0], Blocks));
	TestEqual(TEXT("通知包括注册更新和每次扣盾"), Changes.Num(), 6);
	TestEqual(TEXT("破盾通知次数为零"), Changes.Last(), int64(0));
	const uint64 Reactivated = Scope.Shields().UpsertActiveShield(A, FVector::ZeroVector, 1500, 1, 0);
	TestTrue(TEXT("重新激活不复用旧 ID"), Reactivated != Id && Reactivated != Other);
	Scope.Shields().DeactivateShield(A);
	Scope.Shields().DeactivateShield(A);
	TestFalse(TEXT("关闭幂等"), Scope.Shields().GetShieldRemainingBlocks(A, Blocks));
	AddExpectedError(TEXT("激活护盾失败"), EAutomationExpectedErrorFlags::Contains, 3);
	TestEqual(TEXT("拒绝空 Owner"), Scope.Shields().UpsertActiveShield(nullptr, FVector::ZeroVector, 1, 1, 0), uint64(0));
	TestEqual(TEXT("拒绝零次数"), Scope.Shields().UpsertActiveShield(A, FVector::ZeroVector, 1, 0, 0), uint64(0));
	TestEqual(TEXT("拒绝负反伤"), Scope.Shields().UpsertActiveShield(A, FVector::ZeroVector, 1, 1, -1), uint64(0));
	Scope.Shields().OnShieldStateChanged.Clear();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleNPCFarmContractTest, "MovingCastle.Mass.NPCFarmContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleNPCFarmContractTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassConcreteTests;
	FWorld Scope;
	TArray<FCastleMassUnitHandle> NPCs, Enemies;
	FText Reason;
	if (!TestTrue(TEXT("生成 NPC"), Scope.Units().SpawnUnits(Config<UCastleMassNPCTrait>(), {FTransform::Identity}, NPCs, Reason))
		|| !TestTrue(TEXT("生成敌人"), Scope.Units().SpawnUnits(Config<UCastleMassEnemyTrait>(), {FTransform::Identity}, Enemies, Reason))) { return false; }
	float Bonus = -1.0f;
	TestTrue(TEXT("NPC 默认装配增益"), Scope.Units().GetNPCFarmBuff(NPCs[0], Bonus));
	TestEqual(TEXT("初值为零"), Bonus, 0.0f);
	TestTrue(TEXT("累加小数"), Scope.Units().AddNPCFarmBuff(NPCs[0], 1.5f));
	TestTrue(TEXT("第二次累加"), Scope.Units().AddNPCFarmBuff(NPCs[0], 2.0f));
	Scope.Units().GetNPCFarmBuff(NPCs[0], Bonus);
	TestEqual(TEXT("增益累计准确"), Bonus, 3.5f);
	AddExpectedError(TEXT("AddNPCFarmBuff"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("拒绝零增量"), Scope.Units().AddNPCFarmBuff(NPCs[0], 0.0f));
	AddExpectedError(TEXT("GetNPCFarmBuff"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("敌人不能获得 NPC 增益"), Scope.Units().AddNPCFarmBuff(Enemies[0], 1.0f));
	FCastleMassUnitHandle WrongWorld = NPCs[0];
	WrongWorld.World.Reset();
	TestFalse(TEXT("拒绝错误世界"), Scope.Units().GetNPCFarmBuff(WrongWorld, Bonus));
	AddExpectedError(TEXT("ApplyImmediateDamageToEnemy"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("NPC 不是反伤目标"), Scope.Units().ApplyImmediateDamageToEnemy(NPCs[0], 1));
	TestFalse(TEXT("拒绝负伤害"), Scope.Units().ApplyImmediateDamageToEnemy(Enemies[0], -1));
	TestTrue(TEXT("致死伤害成功"), Scope.Units().ApplyImmediateDamageToEnemy(Enemies[0], 1000));
	FCastleMassUnitSnapshot Snapshot;
	Scope.Units().GetUnitSnapshot(Enemies[0], Snapshot);
	TestEqual(TEXT("生命归零"), Snapshot.Health, 0.0f);
	return true;
}

#endif
