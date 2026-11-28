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
#include "Enemies/CastleMassEnemyLaserAttackProcessor.h"
#include "Enemies/CastleMassEnemyLaserAttackTrait.h"
#include "Enemies/CastleMassEnemyLaserAttackFragments.h"
#include "Enemies/CastleMassEnemyLaserBeamSubsystem.h"
#include "Enemies/CastleMassEnemyLaserVisualizationProcessor.h"
#include "Projectiles/CastleMassEnemyProjectileProcessor.h"
#include "Projectiles/CastleMassEnemyProjectileTrait.h"
#include "Projectiles/CastleMassEnemyProjectileFragments.h"
#include "MassCommonFragments.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassLODTrait.h"
#include "Animation/AnimSequence.h"
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
	// 主塔按真实 515 cm 塔身计算，测试兵与护盾都放在塔面外侧的合法近战距离。
	if (!TestTrue(TEXT("生成近战兵"), Scope.Units().SpawnUnits(Asset, {FTransform(FVector(320, 0, 0))}, Enemies, Reason))) { return false; }
	const uint64 Shield = Scope.Shields().UpsertActiveShield(Scope.Tower, FVector(320, 0, 0), 1500, 1, 7);
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
	auto* PupUnit = CastChecked<UCastleMassEnemyTrait>(Pup->GetMutableConfig().FindMutableTrait(UCastleMassEnemyTrait::StaticClass()));
	PupUnit->MaxHealth = 20;
	PupUnit->Radius = 28.0f;
	auto* PupLaser = NewObject<UCastleMassEnemyLaserAttackTrait>(Pup);
	PupLaser->BeamMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	PupLaser->BeamMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MovingCastle/Mass/M_MassEnemyTint"));
	Pup->GetMutableConfig().AddTrait(*PupLaser);
	auto* Giant = Config<UCastleMassEnemyTrait>();
	auto* GiantUnit = CastChecked<UCastleMassEnemyTrait>(Giant->GetMutableConfig().FindMutableTrait(UCastleMassEnemyTrait::StaticClass()));
	GiantUnit->MaxHealth = 120;
	GiantUnit->Radius = 55.0f;
	Giant->GetMutableConfig().AddTrait(*NewObject<UCastleMassMeleeAttackTrait>(Giant));
	auto* Split = NewObject<UCastleMassDeathSplitTrait>(Giant);
	Split->ChildConfiguration = Pup;
	Split->SpawnCount = 6;
	Split->SpawnSpreadRadius = 180.0f;
	Split->BurstDuration = 0.35f;
	Split->BurstArcHeight = 60.0f;
	Giant->GetMutableConfig().AddTrait(*Split);
	TArray<FCastleMassUnitHandle> Enemies;
	FText Reason;
	if (!Scope.Units().SpawnUnits(Giant, {FTransform(FVector(600, 0, 0))}, Enemies, Reason)) { AddError(Reason.ToString()); return false; }
	Scope.Shields().UpsertActiveShield(Scope.Tower, FVector(600, 0, 0), 1500, 1, 200);
	Step<UCastleMassMeleeAttackProcessor>(Scope, 0);
	TestEqual(TEXT("反伤击杀巨骸"), Scope.Manager().GetFragmentDataChecked<FCastleMassUnitFragment>(Enemies[0].Entity).Health, 0.0f);
	Step<UCastleMassDeathSplitProcessor>(Scope, 0);
	Step<UCastleMassDeathSplitProcessor>(Scope, 0);
	auto Children = Living<FCastleMassEnemyTag>(Scope);
	if (!TestEqual(TEXT("只分裂一次且恰好六只幼骸"), Children.Num(), 6)) { return false; }
	Step<UCastleMassSpawnBurstProcessor>(Scope, 0.175f);
	for (const auto& Child : Children)
	{
		const auto BurstBefore = Scope.Manager().GetFragmentDataChecked<FCastleMassSpawnBurstFragment>(Child.Entity);
		const FVector Before = Scope.Manager().GetFragmentDataChecked<FTransformFragment>(Child.Entity).GetTransform().GetLocation();
		Scope.Manager().GetFragmentDataChecked<FCastleMassMoveTargetFragment>(Child.Entity).bHasTarget = true;
		Scope.Manager().GetFragmentDataChecked<FCastleMassMoveTargetFragment>(Child.Entity).Target = FVector(9999,0,0);
		Step<UCastleMassMovementProcessor>(Scope,0.1f);
		Step<UCastleMassEnemyLaserAttackProcessor>(Scope,0.1f);
		TestTrue(TEXT("爆散期间普通移动不抢占位置"), Scope.Manager().GetFragmentDataChecked<FTransformFragment>(Child.Entity).GetTransform().GetLocation().Equals(Before));
		TestEqual(TEXT("爆散期间激光不结算伤害"), Scope.Manager().GetFragmentDataChecked<FCastleMassEnemyLaserAttackFragment>(Child.Entity).AttackCount, 0);
		TestTrue(TEXT("爆散期间激光不改写移动目标"), Scope.Manager().GetFragmentDataChecked<FCastleMassMoveTargetFragment>(Child.Entity).Target.Equals(FVector(9999,0,0)));
		TestTrue(TEXT("爆散目标仍为原圆周落点"), Scope.Manager().GetFragmentDataChecked<FCastleMassSpawnBurstFragment>(Child.Entity).Target.Equals(BurstBefore.Target));
		FCastleMassUnitSnapshot Snapshot;
		Scope.Units().GetUnitSnapshot(Child, Snapshot);
		TestTrue(TEXT("半程爆散已达到最大弧高"), FMath::IsNearlyEqual(Snapshot.Transform.GetLocation().Z, 60.0, 0.01));
		TestTrue(TEXT("半程水平位移到半径一半"), FMath::IsNearlyEqual(FVector::Dist2D(Snapshot.Transform.GetLocation(), FVector(600, 0, 0)), 90.0, 0.01));
		TestEqual(TEXT("幼骸继承自己的生命"), Snapshot.Health, 20.0f);
		TestNull(TEXT("幼骸没有递归分裂状态"), Scope.Manager().GetFragmentDataPtr<FCastleMassDeathSplitFragment>(Child.Entity));
	}
	Step<UCastleMassSpawnBurstProcessor>(Scope, 0.175f);
	for (const auto& Child : Children)
	{
		const FVector Location = Scope.Manager().GetFragmentDataChecked<FTransformFragment>(Child.Entity).GetTransform().GetLocation();
		TestTrue(TEXT("爆散结束精确到达 180 cm 水平圆环"), FMath::IsNearlyEqual(FVector::Dist2D(Location, FVector(600, 0, 0)), 180.0, 0.01));
		TestEqual(TEXT("爆散落地回到母体高度"), Location.Z, 0.0);
		TestNull(TEXT("落地后移除位移独占状态"), Scope.Manager().GetFragmentDataPtr<FCastleMassSpawnBurstFragment>(Child.Entity));
	}
	Step<UCastleMassLifetimeProcessor>(Scope, 0);
	TestFalse(TEXT("巨骸在分裂之后被生命周期回收"), Scope.Units().IsUnitValid(Enemies[0]));
	for (const auto& Child : Children) { Scope.Units().ApplyImmediateDamageToEnemy(Child, 20); }
	Step<UCastleMassDeathSplitProcessor>(Scope, 0);
	Step<UCastleMassLifetimeProcessor>(Scope, 0);
	TestEqual(TEXT("幼骸死亡不再分裂"), Living<FCastleMassEnemyTag>(Scope).Num(), 0);
	// 普通伤害击杀贴近塔底的巨骸，覆盖扇向外墙的弧线和爆散低点的塔底约束。
	const FVector NearWall(257.5f + 73.0f, 0.0f, 35.0f);
	Enemies.Reset();
	if (!Scope.Units().SpawnUnits(Giant, {FTransform(NearWall)}, Enemies, Reason)) { AddError(Reason.ToString()); return false; }
	Scope.Units().ApplyImmediateDamageToEnemy(Enemies[0], 120);
	Step<UCastleMassDeathSplitProcessor>(Scope, 0);
	Children = Living<FCastleMassEnemyTag>(Scope);
	if (!TestEqual(TEXT("贴墙巨骸普通伤害死亡同样生成六只"), Children.Num(), 6)) { return false; }
	FCastleTowerPrism Tower;
	FString GeometryFailure;
	if (!Scope.Tower->GetTowerShaftPrism(Tower, GeometryFailure)) { AddError(GeometryFailure); return false; }
	bool bAllTrajectoriesOutside = true;
	bool bArcGoesOutward = true;
	for (int32 Sample = 0; Sample <= 7; ++Sample)
	{
		Step<UCastleMassSpawnBurstProcessor>(Scope, Sample == 0 ? 0.0f : 0.05f);
		for (const auto& Child : Children)
		{
			FCastleMassUnitSnapshot Snapshot;
			if (!TestTrue(TEXT("贴墙分裂全程可读取幼骸状态"), Scope.Units().GetUnitSnapshot(Child, Snapshot))) { return false; }
			const FVector Root = Snapshot.Transform.GetLocation();
			FVector Point, Normal;
			float Distance = 0.0f;
			bAllTrajectoriesOutside &= Tower.QuerySurface(Root + Tower.GetUpVector() * PupUnit->Radius, Point, Normal, Distance)
				&& Distance >= PupUnit->Radius - 0.5f && Tower.ToLocalPoint(Root).Z >= -0.5f;
			if (Sample == 3) { bArcGoesOutward &= Root.X >= NearWall.X + 50.0; }
		}
	}
	TestTrue(TEXT("六只幼骸的爆散弧线与落点全在塔身外且根节点在塔底以上"), bAllTrajectoriesOutside);
	TestTrue(TEXT("贴墙爆散的抛物线弧顶朝向塔外半空间"), bArcGoesOutward);
	Step<UCastleMassEnemyLaserAttackProcessor>(Scope, 0.1f);
	for (const auto& Child : Children)
	{
		TestNull(TEXT("贴墙爆散落地后释放移动写入权"), Scope.Manager().GetFragmentDataPtr<FCastleMassSpawnBurstFragment>(Child.Entity));
		TestTrue(TEXT("贴墙分裂幼骸落地后即可对塔面引导持续激光"),
			Scope.Manager().GetFragmentDataChecked<FCastleMassEnemyLaserAttackFragment>(Child.Entity).AttackCount > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleAttackAnimationTest, "MovingCastle.Mass.OptionalAttackAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleAttackAnimationTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassConcreteTests;
	FWorld Scope;
	// 复用既有测试骨架，令攻击和普通移动使用不同索引，隔离正式怪物资产装配的影响。
	for (bool bHasAttackSequence : {false, true})
	{
		auto* Asset = Config<UCastleMassEnemyTrait>();
		Asset->GetMutableConfig().AddTrait(*NewObject<UMassLODCollectorTrait>(Asset));
		auto* Visual = NewObject<UCastleMassHumanoidVisualizationTrait>(Asset);
		Visual->AnimationProvider = LoadObject<UAnimSequenceTransformProviderData>(nullptr, TEXT("/Game/MovingCastle/Mass/ATP_MassHumanoid"));
		Visual->MoveSequence = 0;
		Visual->AttackSequence = bHasAttackSequence ? 1 : INDEX_NONE;
		Visual->AttackPlayRate = 1.5f;
		Asset->GetMutableConfig().AddTrait(*Visual);
		TArray<FCastleMassUnitHandle> Handles;
		FText Reason;
		if (!Scope.Units().SpawnUnits(Asset, {FTransform::Identity}, Handles, Reason)) { AddError(Reason.ToString()); return false; }
		const auto Entity = Handles[0].Entity;
		Scope.Manager().GetFragmentDataChecked<FMassVelocityFragment>(Entity).Value = FVector(100,0,0);
		Scope.Manager().GetFragmentDataChecked<FCastleMassAttackAnimationFragment>(Entity).bInAttackState = true;
		Step<UCastleMassAnimationProcessor>(Scope, 0.1f);
		const auto& Data = Scope.Manager().GetFragmentDataChecked<FMassRepresentationAnimationFragment>(Entity).AnimData;
		TestEqual(TEXT("可选攻击槽优先于速度，无攻击槽仍走普通移动"), Data.SequenceIndex, bHasAttackSequence ? 1 : 0);
		if (bHasAttackSequence) { TestEqual(TEXT("攻击倍率提交到表示"), Data.PlayRate, 1.5f); }
		TestTrue(TEXT("攻击动作循环播放"), Data.LoopMode == EAnimSequenceTrackLoopMode::Loop);
		Scope.Manager().GetFragmentDataChecked<FCastleMassAttackAnimationFragment>(Entity).bInAttackState = false;
		Step<UCastleMassAnimationProcessor>(Scope, 0.1f);
		TestEqual(TEXT("退出攻击态恢复移动"), Data.SequenceIndex, 0);
		Scope.Manager().GetFragmentDataChecked<FMassVelocityFragment>(Entity).Value = FVector::ZeroVector;
		Step<UCastleMassAnimationProcessor>(Scope, 0.1f);
		TestEqual(TEXT("停止后恢复待机"), Data.SequenceIndex, Visual->IdleSequence);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleNPCSpeedAnimationTest, "MovingCastle.Mass.NPCSpeedAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleNPCSpeedAnimationTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassConcreteTests;
	FWorld Scope;
	auto* Formal = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassNPCBase"));
	auto* Lord = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassNPC_TutorialLord"));
	auto* Enemy = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassEnemyBase"));
	if (!TestNotNull(TEXT("正式 NPC 配置"), Formal) || !TestNotNull(TEXT("教程城主配置"), Lord)
		|| !TestNotNull(TEXT("旧敌人配置"), Enemy)) { return false; }
	TestTrue(TEXT("正式 NPC 通过实体模板校验"), Formal->GetMutableConfig().ValidateEntityTemplate(*Scope.World));
	TestTrue(TEXT("教程城主通过实体模板校验"), Lord->GetMutableConfig().ValidateEntityTemplate(*Scope.World));
	const auto* Visual = Cast<UCastleMassHumanoidVisualizationTrait>(Formal->FindTrait(UCastleMassHumanoidVisualizationTrait::StaticClass()));
	const auto* OldVisual = Cast<UCastleMassHumanoidVisualizationTrait>(Enemy->FindTrait(UCastleMassHumanoidVisualizationTrait::StaticClass()));
	if (!TestNotNull(TEXT("正式 NPC 人形动画配置"), Visual) || !TestNotNull(TEXT("旧敌人人形动画配置"), OldVisual)
		|| !TestNotNull(TEXT("正式 NPC 动画提供器"), Visual->AnimationProvider.Get())) { return false; }
	TestEqual(TEXT("旧敌人不启用跑步"), OldVisual->RunSequence, INDEX_NONE);
	const auto& Sequences = Visual->AnimationProvider->GetSequences();
	if (!TestTrue(TEXT("正式 NPC 的走跑使用提供器中的两个完整序列"), Sequences.IsValidIndex(Visual->MoveSequence)
		&& Sequences.IsValidIndex(Visual->RunSequence) && Sequences[Visual->MoveSequence].Sequence
		&& Sequences[Visual->RunSequence].Sequence)) { return false; }
	TestEqual(TEXT("正式 NPC 行走素材"), Sequences[Visual->MoveSequence].Sequence->GetFName(), FName(TEXT("NormalWalk_noWeaponAnim")));
	TestEqual(TEXT("正式 NPC 跑步素材"), Sequences[Visual->RunSequence].Sequence->GetFName(), FName(TEXT("NormalRun_noWeaponAnim")));
	TArray<FCastleMassUnitHandle> FormalHandles, EnemyHandles;
	FText Reason;
	if (!Scope.Units().SpawnUnits(Formal, {FTransform::Identity}, FormalHandles, Reason)
		|| !Scope.Units().SpawnUnits(Enemy, {FTransform::Identity}, EnemyHandles, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	// 直接喂入实体在本帧的实际速度，验证下游 GPU 轨道选择，而非复制判定条件。
	const auto UpdateAtSpeed = [&](const FVector& Velocity) -> const FAnimSequenceTrackAutoPlayData&
	{
		for (const FMassEntityHandle Entity : {FormalHandles[0].Entity, EnemyHandles[0].Entity})
		{
			Scope.Manager().GetFragmentDataChecked<FMassVelocityFragment>(Entity).Value = Velocity;
		}
		Step<UCastleMassAnimationProcessor>(Scope, 0.1f);
		return Scope.Manager().GetFragmentDataChecked<FMassRepresentationAnimationFragment>(FormalHandles[0].Entity).AnimData;
	};
	TestEqual(TEXT("默认 200 cm/s 行走"), UpdateAtSpeed(FVector(200, 0, 200)).SequenceIndex, Visual->MoveSequence);
	TestEqual(TEXT("正好 250 cm/s 仍行走"), UpdateAtSpeed(FVector(250, 0, 0)).SequenceIndex, Visual->MoveSequence);
	const auto RunData = UpdateAtSpeed(FVector(350, 0, 0));
	TestEqual(TEXT("增益超过阈值切入跑步"), RunData.SequenceIndex, Visual->RunSequence);
	TestEqual(TEXT("走跑以 0.2 秒过渡"), RunData.BlendTime, 0.2f);
	TestTrue(TEXT("跑步步频随实际移速增加"), RunData.PlayRate > 1.0f);
	const auto& OldData = Scope.Manager().GetFragmentDataChecked<FMassRepresentationAnimationFragment>(EnemyHandles[0].Entity).AnimData;
	TestEqual(TEXT("未配置跑步的敌人继续使用原移动序列"), OldData.SequenceIndex, OldVisual->MoveSequence);
	TestEqual(TEXT("未配置跑步的敌人继续使用固定倍率"), OldData.PlayRate, OldVisual->MovePlayRate);
	TestEqual(TEXT("NPC 停止水平移动后待机"), UpdateAtSpeed(FVector(0, 0, 350)).SequenceIndex, Visual->IdleSequence);
	TestEqual(TEXT("敌人仍采用原来的三维移速判定"), OldData.SequenceIndex, OldVisual->MoveSequence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMonsterInvalidConfigurationTest, "MovingCastle.Mass.MonsterInvalidConfigurations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMonsterInvalidConfigurationTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassConcreteTests;
	FWorld Scope;
	// 配置错误必须在模板校验时明确拒绝；每个案例使用独立配置，避免模板缓存影响结论。
	AddExpectedError(TEXT("trait-specific validation failed"), EAutomationExpectedErrorFlags::Contains, 6);
	AddExpectedError(TEXT("Mass Entity Template validation:"), EAutomationExpectedErrorFlags::Contains, 6);
	AddExpectedError(TEXT("死亡分裂配置无效"), EAutomationExpectedErrorFlags::Contains, 4);
	for (int32 Case = 0; Case < 4; ++Case)
	{
		auto* Asset = Config<UCastleMassEnemyTrait>();
		auto* Split = NewObject<UCastleMassDeathSplitTrait>(Asset);
		Split->ChildConfiguration = Config<UCastleMassEnemyTrait>();
		if (Case == 0) { Split->SpawnCount = 65; }
		if (Case == 1) { Split->SpawnSpreadRadius = 0; }
		if (Case == 2) { Split->BurstDuration = 0; }
		if (Case == 3) { Split->BurstArcHeight = -1; }
		Asset->GetMutableConfig().AddTrait(*Split);
		TestFalse(TEXT("非法圆环或爆散参数拒绝模板"), Asset->GetMutableConfig().ValidateEntityTemplate(*Scope.World));
	}
	AddExpectedError(TEXT("敌方激光配置无效"), EAutomationExpectedErrorFlags::Contains, 1);
	auto* LaserAsset = Config<UCastleMassEnemyTrait>();
	LaserAsset->GetMutableConfig().AddTrait(*NewObject<UCastleMassEnemyLaserAttackTrait>(LaserAsset));
	TestFalse(TEXT("缺激光网格与材质明确拒绝"), LaserAsset->GetMutableConfig().ValidateEntityTemplate(*Scope.World));
	AddExpectedError(TEXT("Mass 人形动画配置无效"), EAutomationExpectedErrorFlags::Contains, 1);
	auto* VisualAsset = Config<UCastleMassEnemyTrait>();
	VisualAsset->GetMutableConfig().AddTrait(*NewObject<UMassLODCollectorTrait>(VisualAsset));
	auto* Visual = NewObject<UCastleMassHumanoidVisualizationTrait>(VisualAsset);
	Visual->AnimationProvider = LoadObject<UAnimSequenceTransformProviderData>(nullptr,TEXT("/Game/MovingCastle/Mass/ATP_MassHumanoid"));
	Visual->AttackSequence = 999;
	VisualAsset->GetMutableConfig().AddTrait(*Visual);
	TestFalse(TEXT("越界攻击序列明确拒绝"), VisualAsset->GetMutableConfig().ValidateEntityTemplate(*Scope.World));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleEnemyLaserTest, "MovingCastle.Mass.EnemyLaserShieldAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleEnemyLaserTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassConcreteTests;
	FWorld Scope;
	auto* Asset = Config<UCastleMassEnemyTrait>();
	auto* Laser = NewObject<UCastleMassEnemyLaserAttackTrait>(Asset);
	Laser->BeamMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Laser->BeamMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MovingCastle/Mass/M_MassEnemyTint"));
	if (!TestNotNull(TEXT("测试光束网格可用"), Laser->BeamMesh.Get()) || !TestNotNull(TEXT("测试光束材质可用"), Laser->BeamMaterial.Get())) { return false; }
	Asset->GetMutableConfig().AddTrait(*Laser);
	TArray<FCastleMassUnitHandle> Enemies;
	FText Reason;
	if (!Scope.Units().SpawnUnits(Asset, {FTransform(FVector(600.0f, 0.0f, 0.0f))}, Enemies, Reason)) { AddError(Reason.ToString()); return false; }
	const uint64 ShieldId = Scope.Shields().UpsertActiveShield(Scope.Tower, FVector::ZeroVector, 1500.0f, 3, 7.0f);
	Step<UCastleMassEnemyLaserAttackProcessor>(Scope, 0.0f);
	Step<UCastleMassEnemyLaserVisualizationProcessor>(Scope, 0.0f);
	auto* State = Scope.Manager().GetFragmentDataPtr<FCastleMassEnemyLaserAttackFragment>(Enemies[0].Entity);
	TestTrue(TEXT("进入射程后进入攻击态"), State && State->bInAttackRange);
	int64 Blocks = 0;
	TestTrue(TEXT("首次进入射程立即扣除护盾抵挡次数"), Scope.Shields().GetShieldRemainingBlocks(Scope.Tower, Blocks));
	TestEqual(TEXT("首次激光已立即结算"), Blocks, int64(2));
	TestEqual(TEXT("光束实例显示"), Scope.World->GetSubsystem<UCastleMassEnemyLaserBeamSubsystem>()->GetActiveBeamCount(), 1);
	Step<UCastleMassEnemyLaserAttackProcessor>(Scope, 0.5f);
	TestTrue(TEXT("间隔未到护盾仍激活"), Scope.Shields().GetShieldRemainingBlocks(Scope.Tower, Blocks));
	TestEqual(TEXT("半间隔后剩余次数不变"), Blocks, int64(2));
	Step<UCastleMassEnemyLaserAttackProcessor>(Scope, 0.5f);
	Scope.Shields().GetShieldRemainingBlocks(Scope.Tower, Blocks);
	TestEqual(TEXT("达到一秒间隔后第二次结算"), Blocks, int64(1));
	TestTrue(TEXT("护盾激活 ID 有效"), ShieldId != 0);
	TestTrue(TEXT("死亡伤害成功"), Scope.Units().ApplyImmediateDamageToEnemy(Enemies[0], 1000.0f));
	// 伤害发生在 Movement 之后时，显示收尾仍须在同帧清掉光束。
	Step<UCastleMassEnemyLaserVisualizationProcessor>(Scope, 0.0f);
	TestEqual(TEXT("幼骸死亡当帧移除光束实例"), Scope.World->GetSubsystem<UCastleMassEnemyLaserBeamSubsystem>()->GetActiveBeamCount(), 0);
	Scope.Shields().DeactivateShield(Scope.Tower);
	if (!Scope.Units().SpawnUnits(Asset, {FTransform(FVector(600, 0, 0))}, Enemies, Reason)) { AddError(Reason.ToString()); return false; }
	Step<UCastleMassEnemyLaserAttackProcessor>(Scope, 0.0f);
	Step<UCastleMassEnemyLaserVisualizationProcessor>(Scope, 0.0f);
	TestEqual(TEXT("无盾时首击主塔"), Scope.Tower->CurrentHealth, 995.0f);
	Scope.Manager().DestroyEntity(Enemies[0].Entity);
	TestEqual(TEXT("直接销毁实体 Observer 立即回收最后一束"), Scope.World->GetSubsystem<UCastleMassEnemyLaserBeamSubsystem>()->GetActiveBeamCount(), 0);
	if (!Scope.Units().SpawnUnits(Asset, {FTransform(FVector(600, 0, 0))}, Enemies, Reason)) { return false; }
	Scope.Shields().UpsertActiveShield(Scope.Tower, FVector::ZeroVector, 1500, 1, 200);
	Step<UCastleMassEnemyLaserAttackProcessor>(Scope, 0.0f);
	Step<UCastleMassEnemyLaserVisualizationProcessor>(Scope, 0.0f);
	TestEqual(TEXT("致死反伤当帧无光束"), Scope.World->GetSubsystem<UCastleMassEnemyLaserBeamSubsystem>()->GetActiveBeamCount(), 0);
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
	const float Scales[] = {1.35f, 0.65f, 1.1f, 0.6f};
	const TCHAR* MeshNames[] = {TEXT("BeholderSK"), TEXT("BeholderSK"), TEXT("FlyingDemonSK"), TEXT("SpecterSK")};
	const TCHAR* AttackNames[] = {TEXT("Beholder_Attack01Anim"), TEXT("Beholder_Attack02RPTAnim"), TEXT("FlyingDemon_Attack01Anim"), TEXT("Specter_Attack01Anim")};
	const float Damages[] = {40, 5, 15, 4};
	const float Intervals[] = {2, 1, 1.25f, 0.8f};
	const float Ranges[] = {180, 800, 2200, 130};
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
		TestEqual(TEXT("使用指定正式怪物网格"), Mesh->GetName(), FString(MeshNames[Index]));
		TestTrue(TEXT("网格朝向转换为实体 +X"), Visual->MeshOffset.Rotator().Equals(FRotator(0,-90,0),0.01f));
		if (!TestTrue(TEXT("攻击序列索引有效"), Visual->AnimationProvider->GetSequences().IsValidIndex(Visual->AttackSequence))) { return false; }
		TestEqual(TEXT("攻击动作使用指南指定序列"), Visual->AnimationProvider->GetSequences()[Visual->AttackSequence].Sequence->GetName(), FString(AttackNames[Index]));
		if (!TestEqual(TEXT("完整覆盖网格所有材质槽"), Visual->MaterialOverrides.Num(), Mesh->GetMaterials().Num()) || Visual->MaterialOverrides.IsEmpty()) { return false; }
		for (const auto& Material : Visual->MaterialOverrides) { TestNotNull(TEXT("材质引用有效"), Material.Get()); }
		Colors.Add(Visual->MaterialOverrides[0]);
		if (Index == 1)
		{
			const auto* Attack = CastChecked<UCastleMassEnemyLaserAttackTrait>(Asset->FindTrait(UCastleMassEnemyLaserAttackTrait::StaticClass()));
			TestEqual(TEXT("激光伤害"), Attack->AttackDamage, Damages[Index]);
			TestEqual(TEXT("激光间隔"), Attack->AttackInterval, Intervals[Index]);
			TestEqual(TEXT("激光射程"), Attack->AttackRange, Ranges[Index]);
			TestNotNull(TEXT("光束网格"), Attack->BeamMesh.Get());
			TestNotNull(TEXT("发光材质"), Attack->BeamMaterial.Get());
			TestNull(TEXT("激光兵不装配近战"), Asset->FindTrait(UCastleMassMeleeAttackTrait::StaticClass()));
		}
		else if (Index == 2)
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
		if (TestEqual(TEXT("Specter 包含主体与剑两份骨骼描述"), Visual->SkinnedMeshInstanceDesc.Meshes.Num(), Index == 3 ? 2 : 1))
		{
			TestTrue(TEXT("Trait 把覆盖传递到引擎批次描述"), Visual->SkinnedMeshInstanceDesc.Meshes[0].MaterialOverrides == Visual->MaterialOverrides);
		}
	}
	TestEqual(TEXT("巨骸幼骸异色，其余保留原包材质"), Colors.Num(), 3);
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
