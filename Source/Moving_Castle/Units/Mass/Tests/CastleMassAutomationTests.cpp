#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleMassFragments.h"
#include "CastleMassProcessors.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitConfig.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassMovementFragments.h"
#include "MassProcessingContext.h"
#include "MassRepresentationFragments.h"
#include "Misc/AutomationTest.h"

namespace CastleMassTests
{
	struct FScopedWorld
	{
		UWorld* World;
		FScopedWorld()
		{
			const UWorld::InitializationValues Settings = UWorld::InitializationValues()
				.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
		}
		~FScopedWorld() { World->DestroyWorld(false); }
		FMassEntityManager& Manager() const { return World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager(); }
		UCastleMassUnitSubsystem& Units() const { return *World->GetSubsystem<UCastleMassUnitSubsystem>(); }
	};

	template<typename T>
	void Step(FScopedWorld& TestWorld, float DeltaTime)
	{
		TestWorld.Manager().FlushCommands();
		T* Processor = NewObject<T>(TestWorld.World);
		Processor->CallInitialize(TestWorld.World, TestWorld.Manager().AsShared());
		UE::Mass::FProcessingContext Context(TestWorld.Manager(), DeltaTime);
		UE::Mass::Executor::Run(*Processor, Context);
	}

	/** 纯模拟配置让运动/生命周期测试与美术资源、渲染设备完全独立。 */
	template<typename T>
	UMassEntityConfigAsset* MakeSimulationConfig()
	{
		UMassEntityConfigAsset* Config = NewObject<UMassEntityConfigAsset>();
		Config->GetMutableConfig().AddTrait(*NewObject<T>(Config));
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassBatchMovementTest, "MovingCastle.Mass.BatchMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassBatchMovementTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassTests;
	FScopedWorld Scope;
	UMassEntityConfigAsset* Config = MakeSimulationConfig<UCastleMassEnemyTrait>();
	auto* Trait = CastChecked<UCastleMassEnemyTrait>(Config->GetMutableConfig().FindMutableTrait(UCastleMassEnemyTrait::StaticClass()));
	Trait->Movement.MaxSpeed = 100.0f;
	Trait->Movement.Acceleration = 0.0f;
	Trait->Movement.AcceptanceRadius = 0.0f;
	TArray<FTransform> Transforms;
	for (int32 Index = 0; Index < 1024; ++Index) { Transforms.Emplace(FVector(0.0, Index * 150.0, 50.0)); }
	TArray<FCastleMassUnitHandle> Handles;
	FText Failure;
	if (!TestTrue(TEXT("一次生成 1024 个实体"), Scope.Units().SpawnUnits(Config, Transforms, Handles, Failure))) { return false; }
	TestEqual(TEXT("返回完整批次"), Handles.Num(), 1024);
	Scope.Units().SetUnitsVelocity(Handles, FVector(100.0, 0.0, 0.0));
	Step<UCastleMassMovementProcessor>(Scope, 0.5f);
	for (int32 Index : {0, 511, 1023})
	{
		FCastleMassUnitSnapshot Snapshot;
		TestTrue(TEXT("跨 Chunk 实体仍可查询"), Scope.Units().GetUnitSnapshot(Handles[Index], Snapshot));
		TestTrue(TEXT("整批按相同速度移动，保持各自位置"), Snapshot.Transform.GetLocation().Equals(FVector(50.0, Index * 150.0, 50.0)));
		TestNull(TEXT("单位没有附带逐实体 Actor"), Scope.Manager().GetFragmentDataChecked<FMassActorFragment>(Handles[Index].Entity).Get());
	}
	Scope.Units().StopUnits(Handles);
	Step<UCastleMassMovementProcessor>(Scope, 1.0f);
	TestTrue(TEXT("停止命令清空速度"), Scope.Manager().GetFragmentDataChecked<FMassVelocityFragment>(Handles[0].Entity).Value.IsZero());
	Scope.Units().MoveUnitsTo({Handles[0]}, FVector(60.0, 0.0, 150.0));
	Step<UCastleMassMovementProcessor>(Scope, 20.0f);
	FCastleMassUnitSnapshot Snapshot;
	Scope.Units().GetUnitSnapshot(Handles[0], Snapshot);
	TestTrue(TEXT("支持三维目标且大时间步不穿过目标"), Snapshot.Transform.GetLocation().Equals(FVector(60.0, 0.0, 150.0)));
	TestFalse(TEXT("到达后清除目标"), Snapshot.bHasMoveTarget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassProjectileLifetimeTest, "MovingCastle.Mass.ProjectileAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassProjectileLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassTests;
	FScopedWorld Scope;
	UMassEntityConfigAsset* Config = MakeSimulationConfig<UCastleMassProjectileTrait>();
	auto* Trait = CastChecked<UCastleMassProjectileTrait>(Config->GetMutableConfig().FindMutableTrait(UCastleMassProjectileTrait::StaticClass()));
	Trait->Movement.MaxSpeed = 100.0f;
	Trait->Gravity = 10.0f;
	Trait->Lifetime = 2.0f;
	TArray<FCastleMassUnitHandle> Handles;
	FText Failure;
	if (!TestTrue(TEXT("生成弹道实体"), Scope.Units().SpawnUnits(Config, {FTransform::Identity}, Handles, Failure))) { return false; }
	Step<UCastleMassProjectileProcessor>(Scope, 1.0f);
	FCastleMassUnitSnapshot Snapshot;
	TestTrue(TEXT("可读取弹道状态"), Scope.Units().GetUnitSnapshot(Handles[0], Snapshot));
	TestTrue(FString::Printf(TEXT("弹道使用解析重力位移，实际 %s"), *Snapshot.Transform.GetLocation().ToString()), Snapshot.Transform.GetLocation().Equals(FVector(100.0, 0.0, -5.0)));
	TestTrue(FString::Printf(TEXT("重力更新速度，实际 %s"), *Snapshot.Velocity.ToString()), Snapshot.Velocity.Equals(FVector(100.0, 0.0, -10.0)));
	TestTrue(TEXT("保留前一位置供连续命中检测使用"), Scope.Manager().GetFragmentDataChecked<FCastleMassProjectileFragment>(Handles[0].Entity).PreviousLocation.IsZero());
	Step<UCastleMassLifetimeProcessor>(Scope, 1.9f);
	TestTrue(TEXT("寿命到达前实体有效"), Scope.Units().IsUnitValid(Handles[0]));
	Step<UCastleMassLifetimeProcessor>(Scope, 0.2f);
	TestFalse(TEXT("寿命结束自动回收实体"), Scope.Units().IsUnitValid(Handles[0]));
	const FCastleMassUnitHandle Stale = Handles[0];
	Scope.Units().SpawnUnits(Config, {FTransform::Identity}, Handles, Failure);
	Scope.Units().DamageUnits({Stale}, 999.0f);
	Scope.Manager().FlushCommands();
	Scope.Units().GetUnitSnapshot(Handles[0], Snapshot);
	TestEqual(TEXT("旧句柄不能伤害复用槽位的新实体"), Snapshot.Health, 1.0f);
	Scope.Units().DespawnUnits(Handles);
	Step<UCastleMassLifetimeProcessor>(Scope, 0.0f);
	TestFalse(TEXT("主动回收与寿命回收使用同一流程"), Scope.Units().IsUnitValid(Handles[0]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassWorldIsolationTest, "MovingCastle.Mass.WorldIsolationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassWorldIsolationTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassTests;
	FScopedWorld First;
	FScopedWorld Second;
	UMassEntityConfigAsset* Config = MakeSimulationConfig<UCastleMassNPCTrait>();
	TArray<FCastleMassUnitHandle> Handles;
	FText Failure;
	if (!TestTrue(TEXT("生成 NPC 实体"), First.Units().SpawnUnits(Config, {FTransform::Identity}, Handles, Failure))) { return false; }
	TestFalse(TEXT("其他世界不接受同号实体句柄"), Second.Units().DamageUnits(Handles, 100.0f));
	TestFalse(TEXT("其他世界无法查询该实体"), Second.Units().IsUnitValid(Handles[0]));
	TestFalse(TEXT("负伤害不会意外治疗单位"), First.Units().DamageUnits(Handles, -1.0f));
	TestFalse(TEXT("空批次不生成实体"), First.Units().SpawnUnits(Config, {}, Handles, Failure));
	TestTrue(TEXT("失败时清空返回句柄"), Handles.IsEmpty());
	TestFalse(TEXT("没有单位 Trait 的配置被拒绝"), First.Units().SpawnUnits(NewObject<UMassEntityConfigAsset>(), {FTransform::Identity}, Handles, Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassHumanoidAnimationTest, "MovingCastle.Mass.HumanoidAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassHumanoidAnimationTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassTests;
	FScopedWorld Scope;
	for (const TCHAR* Path : {TEXT("/Game/MovingCastle/Mass/DA_MassEnemyBase"), TEXT("/Game/MovingCastle/Mass/DA_MassNPCBase")})
	{
		UMassEntityConfigAsset* Config = LoadObject<UMassEntityConfigAsset>(nullptr, Path);
		if (!TestNotNull(TEXT("可加载已配置的基础人形资产"), Config)) { return false; }
		TArray<FCastleMassUnitHandle> Handles;
		FText Failure;
		if (!TestTrue(TEXT("真实人形配置通过模板验证并批量生成"), Scope.Units().SpawnUnits(Config,
			{FTransform::Identity, FTransform(FVector(0.0, 150.0, 0.0))}, Handles, Failure)))
		{
			AddError(Failure.ToString());
			return false;
		}
		const auto& Representation = Scope.Manager().GetFragmentDataChecked<FMassRepresentationFragment>(Handles[0].Entity);
		TestTrue(TEXT("骨骼实例描述已注册"), Representation.SkinnedMeshDescHandle.IsValid());
		TestEqual(TEXT("没有注册近处 Actor 模板"), Representation.HighResTemplateActorIndex, static_cast<int16>(INDEX_NONE));
		const auto* Visual = CastChecked<UCastleMassHumanoidVisualizationTrait>(Config->FindTrait(UCastleMassHumanoidVisualizationTrait::StaticClass()));
		for (int32 LOD = EMassLOD::High; LOD <= EMassLOD::Low; ++LOD)
		{
			TestTrue(TEXT("三个可见 LOD 均使用骨骼实例"), Visual->Params.LODRepresentation[LOD] == EMassRepresentationType::SkinnedMeshInstance);
		}
		Step<UCastleMassAnimationProcessor>(Scope, 0.0f);
		TestEqual(TEXT("静止时选择待机序列"), Scope.Manager().GetFragmentDataChecked<FMassRepresentationAnimationFragment>(Handles[0].Entity).AnimData.SequenceIndex, Visual->IdleSequence);
		const float FirstPhase = Scope.Manager().GetFragmentDataChecked<FCastleMassAnimationFragment>(Handles[0].Entity).Phase;
		const float SecondPhase = Scope.Manager().GetFragmentDataChecked<FCastleMassAnimationFragment>(Handles[1].Entity).Phase;
		TestNotEqual(TEXT("同批实体相位不同"), FirstPhase, SecondPhase);
		Scope.Units().SetUnitsVelocity({Handles[0]}, FVector(100.0, 0.0, 0.0));
		Step<UCastleMassMovementProcessor>(Scope, 0.1f);
		Step<UCastleMassAnimationProcessor>(Scope, 0.1f);
		TestEqual(TEXT("移动实体切换到移动序列"), Scope.Manager().GetFragmentDataChecked<FMassRepresentationAnimationFragment>(Handles[0].Entity).AnimData.SequenceIndex, Visual->MoveSequence);
		TestEqual(TEXT("邻近静止实体仍然待机"), Scope.Manager().GetFragmentDataChecked<FMassRepresentationAnimationFragment>(Handles[1].Entity).AnimData.SequenceIndex, Visual->IdleSequence);
		Scope.Units().StopUnits(Handles);
		Step<UCastleMassAnimationProcessor>(Scope, 0.1f);
		TestEqual(TEXT("停止后恢复待机"), Scope.Manager().GetFragmentDataChecked<FMassRepresentationAnimationFragment>(Handles[0].Entity).AnimData.SequenceIndex, Visual->IdleSequence);
	}
	return true;
}

#endif
