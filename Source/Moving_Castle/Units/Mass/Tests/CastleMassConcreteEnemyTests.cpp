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
#include "CastleMassEnemySpawner.h"
#include "CastleMassProcessors.h"
#include "Enemies/CastleMassMeleeAttackProcessor.h"
#include "Enemies/CastleMassMeleeAttackTrait.h"
#include "Enemies/CastleMassEnemyRangedAttackProcessor.h"
#include "Enemies/CastleMassEnemyRangedAttackTrait.h"
#include "Enemies/CastleMassDeathSplitProcessor.h"
#include "Enemies/CastleMassDeathSplitTrait.h"
#include "Enemies/CastleMassDeathSplitFragments.h"
#include "Projectiles/CastleMassEnemyProjectileProcessor.h"
#include "Projectiles/CastleMassEnemyProjectileTrait.h"
#include "Projectiles/CastleMassEnemyProjectileFragments.h"
#include "MassCommonFragments.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassLODTrait.h"
#include "Animation/AnimSequenceTransformProviderData.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceConstant.h"

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

	/** 从真实实体组成读取结果；不依赖生成命令返回的数量来证明分裂或发射成功。 */
	template<typename TTag> TArray<FCastleMassUnitHandle> Living(FWorld& Scope)
	{
		TArray<FCastleMassUnitHandle> Result;
		FMassEntityQuery Query(Scope.Manager().AsShared());
		Query.AddTagRequirement<TTag>(EMassFragmentPresence::All);
		Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly);
		FMassExecutionContext Context(Scope.Manager(), 0.0f, false);
		Query.ForEachEntityChunk(Context, [&Result, &Scope](FMassExecutionContext& Chunk)
		{
			const auto Units = Chunk.GetFragmentView<FCastleMassUnitFragment>();
			for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
			{
				if (Units[It].Health > 0.0f)
				{
					auto& Handle = Result.AddDefaulted_GetRef();
					Handle.World = Scope.World;
					Handle.Entity = Chunk.GetEntity(It);
				}
			}
		});
		return Result;
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
	{
		// 复现建筑触发分发所处的游戏线程处理期间：同帧跨农田后攻击塔必须立即读到新值。
		auto Processing = Scope.Manager().NewProcessingScope();
		TestTrue(TEXT("处理期间即时累加农田数字"), Scope.Units().AddNPCFarmBuff(NPCs[0], 2.5f));
		TestTrue(TEXT("处理期间同步读取农田数字"), Scope.Units().GetNPCFarmBuff(NPCs[0], Bonus));
		TestEqual(TEXT("同帧读到新累计值"), Bonus, 6.0f);
		TestTrue(TEXT("处理期间也允许敌人即时扣血"), Scope.Units().ApplyImmediateDamageToEnemy(Enemies[0], 1));
	}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMeleeShieldTest, "MovingCastle.Mass.MeleeShieldPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMeleeShieldTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassConcreteTests;
	FWorld Scope;
	auto* Asset = Config<UCastleMassEnemyTrait>();
	auto* Melee = NewObject<UCastleMassMeleeAttackTrait>(Asset);
	Melee->AttackDamage = 25;
	Asset->GetMutableConfig().AddTrait(*Melee);
	TArray<FCastleMassUnitHandle> Enemies;
	FText Reason;
	if (!TestTrue(TEXT("生成近战兵"), Scope.Units().SpawnUnits(Asset, {FTransform(FVector(100, 0, 0))}, Enemies, Reason))) { return false; }
	const uint64 Shield = Scope.Shields().UpsertActiveShield(Scope.Tower, FVector(100, 0, 0), 1500, 1, 7);
	Step<UCastleMassMeleeAttackProcessor>(Scope, 0);
	FCastleMassUnitSnapshot Snapshot;
	Scope.Units().GetUnitSnapshot(Enemies[0], Snapshot);
	TestEqual(TEXT("同在近战范围时优先盾而非主塔"), Scope.Tower->CurrentHealth, 1000.0f);
	TestEqual(TEXT("近战受击反伤即时可见"), Snapshot.Health, 93.0f);
	int64 Remaining;
	TestFalse(TEXT("一层盾被近战打破"), Scope.Shields().GetShieldRemainingBlocks(Scope.Tower, Remaining));
	TestEqual(TEXT("护盾攻击计数可观测"), Snapshot.AttackCount, 1);
	Step<UCastleMassMeleeAttackProcessor>(Scope, 1);
	TestEqual(TEXT("破盾后自动转火主塔"), Scope.Tower->CurrentHealth, 975.0f);
	TestTrue(TEXT("激活 ID 有效"), Shield != 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDeathSplitTest, "MovingCastle.Mass.DeathSplitAndReflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDeathSplitTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassConcreteTests;
	FWorld Scope;
	auto* Pup = Config<UCastleMassEnemyTrait>();
	CastChecked<UCastleMassEnemyTrait>(Pup->GetMutableConfig().FindMutableTrait(UCastleMassEnemyTrait::StaticClass()))->MaxHealth = 20;
	auto* Giant = Config<UCastleMassEnemyTrait>();
	CastChecked<UCastleMassEnemyTrait>(Giant->GetMutableConfig().FindMutableTrait(UCastleMassEnemyTrait::StaticClass()))->MaxHealth = 120;
	Giant->GetMutableConfig().AddTrait(*NewObject<UCastleMassMeleeAttackTrait>(Giant));
	auto* Split = NewObject<UCastleMassDeathSplitTrait>(Giant);
	Split->ChildConfiguration = Pup;
	Giant->GetMutableConfig().AddTrait(*Split);
	TArray<FCastleMassUnitHandle> Enemies;
	FText Reason;
	if (!Scope.Units().SpawnUnits(Giant, {FTransform(FVector(100, 0, 0))}, Enemies, Reason)) { AddError(Reason.ToString()); return false; }
	Scope.Shields().UpsertActiveShield(Scope.Tower, FVector(100, 0, 0), 1500, 1, 200);
	Step<UCastleMassMeleeAttackProcessor>(Scope, 0);
	TestEqual(TEXT("反伤击杀巨骸"), Scope.Manager().GetFragmentDataChecked<FCastleMassUnitFragment>(Enemies[0].Entity).Health, 0.0f);
	Step<UCastleMassDeathSplitProcessor>(Scope, 0);
	Step<UCastleMassDeathSplitProcessor>(Scope, 0);
	auto Children = Living<FCastleMassEnemyTag>(Scope);
	if (!TestEqual(TEXT("只分裂一次且恰好三只幼骸"), Children.Num(), 3)) { return false; }
	for (const auto& Child : Children)
	{
		FCastleMassUnitSnapshot Snapshot;
		Scope.Units().GetUnitSnapshot(Child, Snapshot);
		TestTrue(TEXT("幼骸在死亡位置周围 80 cm 圆周"), FMath::IsNearlyEqual(FVector::Dist2D(Snapshot.Transform.GetLocation(), FVector(100, 0, 0)), 80.0));
		TestEqual(TEXT("幼骸继承自己的生命"), Snapshot.Health, 20.0f);
		TestNull(TEXT("幼骸没有递归分裂状态"), Scope.Manager().GetFragmentDataPtr<FCastleMassDeathSplitFragment>(Child.Entity));
	}
	Step<UCastleMassLifetimeProcessor>(Scope, 0);
	TestFalse(TEXT("巨骸在分裂之后被生命周期回收"), Scope.Units().IsUnitValid(Enemies[0]));
	for (const auto& Child : Children) { Scope.Units().ApplyImmediateDamageToEnemy(Child, 20); }
	Step<UCastleMassDeathSplitProcessor>(Scope, 0);
	Step<UCastleMassLifetimeProcessor>(Scope, 0);
	TestEqual(TEXT("幼骸死亡不再分裂"), Living<FCastleMassEnemyTag>(Scope).Num(), 0);
	// 同一能力也必须响应普通伤害致死，而不是仅由护盾路径直接生成幼骸。
	if (!Scope.Units().SpawnUnits(Giant, {FTransform::Identity}, Enemies, Reason)) { return false; }
	Scope.Units().ApplyImmediateDamageToEnemy(Enemies[0], 120);
	Step<UCastleMassDeathSplitProcessor>(Scope, 0);
	TestEqual(TEXT("普通伤害死亡同样生成三只"), Living<FCastleMassEnemyTag>(Scope).Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleEnemyRangedTest, "MovingCastle.Mass.EnemyRangedShieldAndTower",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleEnemyRangedTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassConcreteTests;
	FWorld Scope;
	auto* Projectile = Config<UCastleMassEnemyProjectileTrait>();
	auto* ProjectileTrait = CastChecked<UCastleMassEnemyProjectileTrait>(Projectile->GetMutableConfig().FindMutableTrait(UCastleMassEnemyProjectileTrait::StaticClass()));
	ProjectileTrait->Movement.MaxSpeed = 1400;
	ProjectileTrait->Radius = 12;
	auto* Asset = Config<UCastleMassEnemyTrait>();
	auto* Attack = NewObject<UCastleMassEnemyRangedAttackTrait>(Asset);
	Attack->ProjectileConfiguration = Projectile;
	Asset->GetMutableConfig().AddTrait(*Attack);
	TArray<FCastleMassUnitHandle> Enemies;
	FText Reason;
	if (!Scope.Units().SpawnUnits(Asset, {FTransform(FVector(2000, 0, 0))}, Enemies, Reason)) { AddError(Reason.ToString()); return false; }
	Scope.Shields().UpsertActiveShield(Scope.Tower, FVector::ZeroVector, 1500, 2, 7);
	Step<UCastleMassEnemyRangedAttackProcessor>(Scope, 0);
	FCastleMassUnitSnapshot Snapshot;
	Scope.Units().GetUnitSnapshot(Enemies[0], Snapshot);
	TestTrue(TEXT("2000 cm 吸引圈外已在射程内"), Snapshot.bInAttackRange);
	TestFalse(TEXT("远程在射程内清空移动目标"), Snapshot.bHasMoveTarget);
	TestTrue(TEXT("远程在射程内速度为零"), Snapshot.Velocity.IsZero());
	TestEqual(TEXT("安全边界成功生成一次发射"), Snapshot.AttackCount, 1);
	auto Bullets = Living<FCastleMassProjectileTag>(Scope);
	if (!TestEqual(TEXT("发射实际追踪实体"), Bullets.Num(), 1)) { return false; }
	Step<UCastleMassProjectileProcessor>(Scope, 0.5f);
	Scope.Units().GetUnitSnapshot(Bullets[0], Snapshot);
	TestTrue(TEXT("普通子弹处理器不移动敌方追踪弹"), Snapshot.Transform.GetLocation().Equals(FVector(2000, 0, 0)));
	Step<UCastleMassEnemyProjectileProcessor>(Scope, 2);
	int64 Remaining;
	Scope.Shields().GetShieldRemainingBlocks(Scope.Tower, Remaining);
	TestEqual(TEXT("远程弹命中扣一次盾"), Remaining, int64(1));
	Scope.Units().GetUnitSnapshot(Enemies[0], Snapshot);
	TestEqual(TEXT("远程弹反伤原攻击者"), Snapshot.Health, 93.0f);
	TestEqual(TEXT("有盾时主塔不受伤"), Scope.Tower->CurrentHealth, 1000.0f);
	Step<UCastleMassEnemyRangedAttackProcessor>(Scope, 0.5f);
	TestEqual(TEXT("冷却未到不新增存活弹"), Living<FCastleMassProjectileTag>(Scope).Num(), 0);
	Step<UCastleMassEnemyRangedAttackProcessor>(Scope, 0.75f);
	Scope.Shields().DeactivateShield(Scope.Tower);
	AddExpectedError(TEXT("已提前消失，本发回收且不改打其他目标"), EAutomationExpectedErrorFlags::Contains, 1);
	Step<UCastleMassEnemyProjectileProcessor>(Scope, 2);
	TestEqual(TEXT("盾消失的弹回收且不穿透主塔"), Scope.Tower->CurrentHealth, 1000.0f);
	Step<UCastleMassLifetimeProcessor>(Scope, 0);
	TestEqual(TEXT("失效弹全部回收"), Living<FCastleMassProjectileTag>(Scope).Num(), 0);
	Step<UCastleMassEnemyRangedAttackProcessor>(Scope, 1.25f);
	Step<UCastleMassEnemyProjectileProcessor>(Scope, 2);
	TestEqual(TEXT("没有护盾时远程扣主塔十五点"), Scope.Tower->CurrentHealth, 985.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleSwarmClusterTest, "MovingCastle.Mass.ShadowSwarmClusters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleSwarmClusterTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassConcreteTests;
	FWorld Scope;
	auto* Asset = Config<UCastleMassEnemyTrait>();
	for (int32 Seed = 0; Seed < 12; ++Seed)
	{
		auto* Spawner = Scope.World->SpawnActor<ACastleMassEnemySpawner>();
		Spawner->Configuration = Asset;
		Spawner->Count = 1;
		Spawner->MinClusterCount = 6;
		Spawner->MaxClusterCount = 8;
		Spawner->ClusterSpreadRadius = 120;
		Spawner->InnerRadius = 1800;
		Spawner->OuterRadius = 2300;
		Spawner->RandomSeed = Seed;
		if (!TestTrue(TEXT("生成群虫单波"), Spawner->SpawnUnits())) { return false; }
		TestTrue(TEXT("每一波是六至八只"), Spawner->SpawnedUnits.Num() >= 6 && Spawner->SpawnedUnits.Num() <= 8);
		TArray<FVector> Locations;
		for (const auto& Handle : Spawner->SpawnedUnits)
		{
			FCastleMassUnitSnapshot Snapshot;
			Scope.Units().GetUnitSnapshot(Handle, Snapshot);
			Locations.Add(Snapshot.Transform.GetLocation());
		}
		for (const auto& A : Locations)
		{
			TestTrue(TEXT("簇散布仍贴近外圈基准"), A.Size2D() >= 1680.0 && A.Size2D() <= 2420.0);
			for (const auto& B : Locations) { TestTrue(TEXT("同一簇任意两只距离不超过直径"), FVector::Dist2D(A, B) <= 240.001); }
		}
		Spawner->Destroy();
	}
	auto* Invalid = Scope.World->SpawnActor<ACastleMassEnemySpawner>();
	Invalid->MinClusterCount = 8;
	Invalid->MaxClusterCount = 6;
	AddExpectedError(TEXT("簇数量范围无效"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("倒置簇范围明确拒绝"), Invalid->SpawnUnits());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleConcreteEnemyAssetTest, "MovingCastle.Mass.ConcreteEnemyAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleConcreteEnemyAssetTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassConcreteTests;
	FWorld Scope;
	TSet<UMaterialInterface*> Colors;
	const TCHAR* Names[] = {TEXT("LavaGoliath"), TEXT("LavaPup"), TEXT("MiasmaWelp"), TEXT("ShadowSwarm")};
	const float Healths[] = {120, 20, 55, 12};
	const float Speeds[] = {140, 300, 240, 420};
	const float Radii[] = {55, 28, 38, 22};
	const float Scales[] = {1.5f, 0.75f, 1.1f, 0.6f};
	const FLinearColor ExpectedColors[] = {{0.85f, 0.18f, 0.05f}, {1, 0.55f, 0.08f}, {0.12f, 0.8f, 0.45f}, {0.38f, 0.15f, 0.9f}};
	const float Damages[] = {40, 8, 15, 4};
	const float Intervals[] = {2, 1, 1.25f, 0.8f};
	const float Ranges[] = {180, 140, 2200, 130};
	auto* Base = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassEnemyBase"));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FString Path = FString(TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_")) + Names[Index];
		auto* Asset = LoadObject<UMassEntityConfigAsset>(nullptr, *Path);
		if (!TestNotNull(TEXT("正式敌人资产存在"), Asset)) { return false; }
		TestTrue(TEXT("正式兵种显式继承原敌人基类"), Asset->GetConfig().GetParent() == Base);
		const auto* Unit = CastChecked<UCastleMassEnemyTrait>(Asset->FindTrait(UCastleMassEnemyTrait::StaticClass()));
		const auto* Visual = CastChecked<UCastleMassHumanoidVisualizationTrait>(Asset->FindTrait(UCastleMassHumanoidVisualizationTrait::StaticClass()));
		TestEqual(TEXT("正式兵种生命"), Unit->MaxHealth, Healths[Index]);
		TestEqual(TEXT("正式兵种速度"), Unit->Movement.MaxSpeed, Speeds[Index]);
		TestEqual(TEXT("正式兵种半径"), Unit->Radius, Radii[Index]);
		TestTrue(TEXT("正式兵种模型比例"), Visual->MeshOffset.GetScale3D().Equals(FVector(Scales[Index])));
		const auto* Mesh = CastChecked<USkeletalMesh>(Visual->AnimationProvider->GetSkinnedAsset());
		if (!TestEqual(TEXT("完整覆盖网格所有材质槽"), Visual->MaterialOverrides.Num(), Mesh->GetMaterials().Num()) || Visual->MaterialOverrides.IsEmpty()) { return false; }
		for (const auto& Material : Visual->MaterialOverrides) { TestNotNull(TEXT("材质引用有效"), Material.Get()); }
		Colors.Add(Visual->MaterialOverrides[0]);
		FLinearColor ActualColor;
		TestTrue(TEXT("颜色参数存在"), Visual->MaterialOverrides[0]->GetVectorParameterValue(FMaterialParameterInfo(TEXT("EnemyColor")), ActualColor));
		TestTrue(TEXT("颜色符合正式配置"), ActualColor.Equals(ExpectedColors[Index]));
		if (Index == 2)
		{
			const auto* Attack = CastChecked<UCastleMassEnemyRangedAttackTrait>(Asset->FindTrait(UCastleMassEnemyRangedAttackTrait::StaticClass()));
			TestEqual(TEXT("远程伤害"), Attack->AttackDamage, Damages[Index]);
			TestEqual(TEXT("远程间隔"), Attack->AttackInterval, Intervals[Index]);
			TestEqual(TEXT("远程射程"), Attack->AttackRange, Ranges[Index]);
			TestNull(TEXT("远程兵不装配近战"), Asset->FindTrait(UCastleMassMeleeAttackTrait::StaticClass()));
		}
		else
		{
			const auto* Attack = CastChecked<UCastleMassMeleeAttackTrait>(Asset->FindTrait(UCastleMassMeleeAttackTrait::StaticClass()));
			TestEqual(TEXT("近战伤害"), Attack->AttackDamage, Damages[Index]);
			TestEqual(TEXT("近战间隔"), Attack->AttackInterval, Intervals[Index]);
			TestEqual(TEXT("近战射程"), Attack->AttackRange, Ranges[Index]);
		}
		TArray<FCastleMassUnitHandle> Handles;
		FText Reason;
		TestTrue(TEXT("正式资产通过模板校验并可生成"), Scope.Units().SpawnUnits(Asset, {FTransform(FVector(1000, Index * 200, 0))}, Handles, Reason));
		if (TestEqual(TEXT("构建出的骨骼批次描述有效"), Visual->SkinnedMeshInstanceDesc.Meshes.Num(), 1))
		{
			TestTrue(TEXT("Trait 把覆盖传递到引擎批次描述"), Visual->SkinnedMeshInstanceDesc.Meshes[0].MaterialOverrides == Visual->MaterialOverrides);
		}
	}
	TestEqual(TEXT("四色兵种材质互不相同"), Colors.Num(), 4);
	auto* Pup = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_LavaPup"));
	TestNull(TEXT("正式幼骸不再分裂"), Pup->FindTrait(UCastleMassDeathSplitTrait::StaticClass()));
	auto* Projectile = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassEnemyMiasmaProjectile"));
	if (!TestNotNull(TEXT("正式敌方投射物存在"), Projectile)) { return false; }
	TestNull(TEXT("敌方投射物没有含普通 Unit Trait 的 Parent"), Projectile->GetConfig().GetParent());
	const auto* Bullet = CastChecked<UCastleMassEnemyProjectileTrait>(Projectile->FindTrait(UCastleMassEnemyProjectileTrait::StaticClass()));
	TestEqual(TEXT("投射物速度"), Bullet->Movement.MaxSpeed, 1400.0f);
	TestEqual(TEXT("投射物半径"), Bullet->Radius, 12.0f);
	TestEqual(TEXT("投射物寿命"), Bullet->Lifetime, 5.0f);
	TestTrue(TEXT("正式敌方弹完整组合通过模板校验"), Projectile->GetMutableConfig().ValidateEntityTemplate(*Scope.World));
	// 空槽引用必须在正式使用前拒绝，不能显示默认材质而继续生成。
	auto* Invalid = Config<UCastleMassEnemyTrait>();
	Invalid->GetMutableConfig().AddTrait(*NewObject<UMassLODCollectorTrait>(Invalid));
	auto* InvalidVisual = NewObject<UCastleMassHumanoidVisualizationTrait>(Invalid);
	InvalidVisual->AnimationProvider = LoadObject<UAnimSequenceTransformProviderData>(nullptr, TEXT("/Game/MovingCastle/Mass/ATP_MassHumanoid"));
	InvalidVisual->MaterialOverrides.Add(nullptr);
	Invalid->GetMutableConfig().AddTrait(*InvalidVisual);
	AddExpectedError(TEXT("Mass 材质覆盖无效"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Mass 人形动画配置无效"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("CastleMassHumanoidVisualizationTrait_0 trait-specific validation failed"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Mass Entity Template validation:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("空材质槽引用明确拒绝"), Invalid->GetMutableConfig().ValidateEntityTemplate(*Scope.World));
	return true;
}

#endif
