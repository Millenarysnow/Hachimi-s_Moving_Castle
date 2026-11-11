// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleMainTowerComponent.h"
#include "CastleMassEnemySpawner.h"
#include "CastleMassFragments.h"
#include "CastleMassProcessors.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MassCommonFragments.h"
#include "MassActorSubsystem.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassMovementFragments.h"
#include "MassProcessingContext.h"
#include "Misc/AutomationTest.h"

namespace CastleMassEnemyTests
{
	/** 建立不依赖关卡资产的游戏 World，测试 Mass 敌人行为和主塔组件交互。 */
	struct FScopedWorld
	{
		UWorld* World = nullptr;
		AActor* TowerActor = nullptr;
		UCastleMainTowerComponent* Tower = nullptr;

		FScopedWorld()
		{
			const UWorld::InitializationValues Settings = UWorld::InitializationValues()
				.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
			if (World == nullptr) { return; }

			TowerActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
			Tower = NewObject<UCastleMainTowerComponent>(TowerActor, TEXT("TestMainTowerHealth"));
			TowerActor->AddInstanceComponent(Tower);
			Tower->MaxHealth = 1000.0f;
			Tower->TargetHeightOffset = 0.0f;
			Tower->InitializeHealth();
			Tower->RegisterComponent();
			World->GetSubsystem<UCastleMainTowerSubsystem>()->RegisterMainTower(Tower);
		}

		~FScopedWorld()
		{
			if (World != nullptr)
			{
				if (TowerActor != nullptr) { TowerActor->Destroy(); }
				World->DestroyWorld(false);
			}
		}

		FMassEntityManager& Manager() const
		{
			return World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
		}

		UCastleMassUnitSubsystem& Units() const
		{
			return *World->GetSubsystem<UCastleMassUnitSubsystem>();
		}
	};

	template<typename T>
	void Step(FScopedWorld& TestWorld, const float DeltaTime)
	{
		TestWorld.Manager().FlushCommands();
		T* Processor = NewObject<T>(TestWorld.World);
		Processor->CallInitialize(TestWorld.World, TestWorld.Manager().AsShared());
		UE::Mass::FProcessingContext Context(TestWorld.Manager(), DeltaTime);
		UE::Mass::Executor::Run(*Processor, Context);
	}

	/** 构造仅含敌人 Trait 的逻辑配置，避免测试依赖骨骼动画资产。 */
	UMassEntityConfigAsset* MakeEnemyConfig()
	{
		UMassEntityConfigAsset* Config = NewObject<UMassEntityConfigAsset>();
		Config->GetMutableConfig().AddTrait(*NewObject<UCastleMassEnemyTrait>(Config));
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCastleMassEnemyAttackAutomationTest,
	"MovingCastle.Mass.EnemyAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassEnemyAttackAutomationTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassEnemyTests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("创建敌人测试 World"), Scope.World) || !TestNotNull(TEXT("创建测试主塔"), Scope.Tower))
	{
		return false;
	}

	UMassEntityConfigAsset* Config = MakeEnemyConfig();
	UCastleMassEnemyTrait* Trait = CastChecked<UCastleMassEnemyTrait>(
		Config->GetMutableConfig().FindMutableTrait(UCastleMassEnemyTrait::StaticClass()));
	Trait->MaxHealth = 10.0f;
	Trait->AttackDamage = 25.0f;
	Trait->AttackInterval = 1.0f;
	Trait->AttackRange = 20.0f;
	Trait->Movement.MaxSpeed = 100.0f;
	Trait->Movement.Acceleration = 0.0f;
	Trait->Movement.AcceptanceRadius = 0.0f;

	TArray<FCastleMassUnitHandle> Handles;
	FText FailureReason;
	if (!TestTrue(TEXT("生成一名敌人"), Scope.Units().SpawnUnits(Config, {FTransform(FVector(100.0f, 0.0f, 0.0f))}, Handles, FailureReason)))
	{
		AddError(FailureReason.ToString());
		return false;
	}

	Step<UCastleMassEnemyAttackProcessor>(Scope, 0.0f);
	Step<UCastleMassMovementProcessor>(Scope, 0.5f);
	Step<UCastleMassEnemyAttackProcessor>(Scope, 0.0f);
	Step<UCastleMassMovementProcessor>(Scope, 0.4f);
	Step<UCastleMassEnemyAttackProcessor>(Scope, 0.1f);

	FCastleMassUnitSnapshot Snapshot;
	TestTrue(TEXT("可以读取敌人攻击状态"), Scope.Units().GetUnitSnapshot(Handles[0], Snapshot));
	TestTrue(TEXT("敌人进入主塔攻击范围后停止追踪"), Snapshot.bInAttackRange);
	TestFalse(TEXT("进入攻击范围后清除移动目标"), Snapshot.bHasMoveTarget);
	TestEqual(TEXT("第一次攻击已经记录"), Snapshot.AttackCount, 1);
	TestEqual(TEXT("第一次攻击扣除主塔 25 点生命"), Scope.Tower->CurrentHealth, 975.0f);

	Step<UCastleMassEnemyAttackProcessor>(Scope, 0.5f);
	TestEqual(TEXT("攻击冷却期间不会重复攻击"), Snapshot.AttackCount, 1);
	TestEqual(TEXT("攻击冷却期间主塔生命不变"), Scope.Tower->CurrentHealth, 975.0f);
	Step<UCastleMassEnemyAttackProcessor>(Scope, 0.5f);
	Scope.Units().GetUnitSnapshot(Handles[0], Snapshot);
	TestEqual(TEXT("攻击间隔结束后再次攻击"), Snapshot.AttackCount, 2);
	TestEqual(TEXT("第二次攻击再次扣除主塔生命"), Scope.Tower->CurrentHealth, 950.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCastleMassEnemyRingSpawnerAutomationTest,
	"MovingCastle.Mass.EnemyRingSpawner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassEnemyRingSpawnerAutomationTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassEnemyTests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("创建圆环生成测试 World"), Scope.World) || !TestNotNull(TEXT("创建圆环测试主塔"), Scope.Tower))
	{
		return false;
	}

	ACastleMassEnemySpawner* Spawner = Scope.World->SpawnActor<ACastleMassEnemySpawner>(ACastleMassEnemySpawner::StaticClass());
	if (!TestNotNull(TEXT("创建 Mass 敌人圆环生成器"), Spawner)) { return false; }
	Spawner->Configuration = MakeEnemyConfig();
	Spawner->Count = 32;
	Spawner->InnerRadius = 100.0f;
	Spawner->OuterRadius = 200.0f;
	Spawner->HeightOffset = 50.0f;
	Spawner->RandomSeed = 20261112;

	TestTrue(TEXT("圆环生成器可以生成完整批次"), Spawner->SpawnUnits());
	TestEqual(TEXT("圆环生成器返回配置数量"), Spawner->SpawnedUnits.Num(), 32);
	for (const FCastleMassUnitHandle& Handle : Spawner->SpawnedUnits)
	{
		FCastleMassUnitSnapshot Snapshot;
		TestTrue(TEXT("圆环单位句柄有效"), Scope.Units().GetUnitSnapshot(Handle, Snapshot));
		const float Distance = FVector::Dist2D(Snapshot.Transform.GetLocation(), FVector::ZeroVector);
		TestTrue(TEXT("敌人距离位于圆环内外半径之间"), Distance >= 100.0f && Distance <= 200.0f);
		TestEqual(TEXT("敌人生成高度使用配置值"), static_cast<float>(Snapshot.Transform.GetLocation().Z), 50.0f);
		TestNull(TEXT("敌人仍然是 Mass Entity，没有逐实体 Actor"),
			Scope.Manager().GetFragmentDataChecked<FMassActorFragment>(Handle.Entity).Get());
	}

	Spawner->DespawnUnits();
	return true;
}

#endif
