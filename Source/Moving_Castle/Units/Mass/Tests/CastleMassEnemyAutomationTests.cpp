// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleMainTowerComponent.h"
#include "CastleMassEnemySpawner.h"
#include "CastleMassFragments.h"
#include "CastleMassProcessors.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleCombatData.h"
#include "Enemies/CastleMassMeleeAttackFragments.h"
#include "Enemies/CastleMassMeleeAttackProcessor.h"
#include "Enemies/CastleMassMeleeAttackTrait.h"
#include "Engine/Engine.h"
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
		FWorldContext* WorldContext = nullptr;
		AActor* TowerActor = nullptr;
		UCastleMainTowerComponent* Tower = nullptr;

		FScopedWorld()
		{
			const UWorld::InitializationValues Settings = UWorld::InitializationValues()
				.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
			if (World == nullptr) { return; }
			WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext->SetCurrentWorld(World);

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
				GEngine->DestroyWorldContext(World);
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

	/** 显式选择是否装配近战能力；逻辑测试不依赖骨骼动画资产。 */
	UMassEntityConfigAsset* MakeEnemyConfig(const bool bWithMelee = true)
	{
		UMassEntityConfigAsset* Config = NewObject<UMassEntityConfigAsset>();
		Config->GetMutableConfig().AddTrait(*NewObject<UCastleMassEnemyTrait>(Config));
		if (bWithMelee) { Config->GetMutableConfig().AddTrait(*NewObject<UCastleMassMeleeAttackTrait>(Config)); }
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
	UCastleMassMeleeAttackTrait* Melee = CastChecked<UCastleMassMeleeAttackTrait>(
		Config->GetMutableConfig().FindMutableTrait(UCastleMassMeleeAttackTrait::StaticClass()));
	Trait->MaxHealth = 10.0f;
	Melee->AttackDamage = 25.0f;
	Melee->AttackInterval = 1.0f;
	Melee->AttackRange = 20.0f;
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

	Step<UCastleMassMeleeAttackProcessor>(Scope, 0.0f);
	Step<UCastleMassMovementProcessor>(Scope, 0.5f);
	Step<UCastleMassMeleeAttackProcessor>(Scope, 0.0f);
	Step<UCastleMassMovementProcessor>(Scope, 0.4f);
	Step<UCastleMassMeleeAttackProcessor>(Scope, 0.1f);

	FCastleMassUnitSnapshot Snapshot;
	TestTrue(TEXT("可以读取敌人攻击状态"), Scope.Units().GetUnitSnapshot(Handles[0], Snapshot));
	TestTrue(TEXT("敌人进入主塔攻击范围后停止追踪"), Snapshot.bInAttackRange);
	TestFalse(TEXT("进入攻击范围后清除移动目标"), Snapshot.bHasMoveTarget);
	TestEqual(TEXT("第一次攻击已经记录"), Snapshot.AttackCount, 1);
	TestEqual(TEXT("第一次攻击扣除主塔 25 点生命"), Scope.Tower->CurrentHealth, 975.0f);

	Step<UCastleMassMeleeAttackProcessor>(Scope, 0.5f);
	Scope.Units().GetUnitSnapshot(Handles[0], Snapshot);
	TestEqual(TEXT("攻击冷却期间不会重复攻击"), Snapshot.AttackCount, 1);
	TestEqual(TEXT("攻击冷却期间主塔生命不变"), Scope.Tower->CurrentHealth, 975.0f);
	Step<UCastleMassMeleeAttackProcessor>(Scope, 0.5f);
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

/** 基类与近战实体可以同时存在；只有显式装配近战 Trait 的一方追塔并攻击。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassMeleeCompositionTest, "MovingCastle.Mass.MeleeComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassMeleeCompositionTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassEnemyTests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("创建能力隔离测试世界"), Scope.World)) { return false; }
	TArray<FCastleMassUnitHandle> BaseHandles, MeleeHandles;
	FText Reason;
	if (!TestTrue(TEXT("仅敌人基类可以独立生成"), Scope.Units().SpawnUnits(MakeEnemyConfig(false),
		{FTransform(FVector(100, 0, 0))}, BaseHandles, Reason))
		|| !TestTrue(TEXT("近战能力通过组合生成"), Scope.Units().SpawnUnits(MakeEnemyConfig(),
		{FTransform(FVector(100, 0, 0))}, MeleeHandles, Reason))) { AddError(Reason.ToString()); return false; }
	TestNull(TEXT("基类实体不分配近战 Fragment"), Scope.Manager().GetFragmentDataPtr<FCastleMassMeleeAttackFragment>(BaseHandles[0].Entity));
	TestNotNull(TEXT("近战实体拥有独立战斗状态"), Scope.Manager().GetFragmentDataPtr<FCastleMassMeleeAttackFragment>(MeleeHandles[0].Entity));
	Step<UCastleMassMeleeAttackProcessor>(Scope, 1.0f);
	FCastleMassUnitSnapshot BaseSnapshot, MeleeSnapshot;
	Scope.Units().GetUnitSnapshot(BaseHandles[0], BaseSnapshot);
	Scope.Units().GetUnitSnapshot(MeleeHandles[0], MeleeSnapshot);
	TestFalse(TEXT("基类不会自动索敌"), BaseSnapshot.bHasMoveTarget);
	TestEqual(TEXT("基类不会自动攻击"), BaseSnapshot.AttackCount, 0);
	TestEqual(TEXT("近战兵在范围内执行一次攻击"), MeleeSnapshot.AttackCount, 1);
	TestEqual(TEXT("零伤害攻击不扣主塔血量"), Scope.Tower->CurrentHealth, 1000.0f);
	TestEqual(TEXT("零伤害仍记入主塔受击次数"), Scope.Tower->AttackCount, 1);
	TestNull(TEXT("近战兵仍没有逐实体 Actor"), Scope.Manager().GetFragmentDataChecked<FMassActorFragment>(MeleeHandles[0].Entity).Get());
	return true;
}

/** 使用项目真实资产验证继承、入口引用和模板组成，防止只拆了代码却仍从基类生成示例兵。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassMeleeAssetTest, "MovingCastle.Mass.MeleeTestAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassMeleeAssetTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassEnemyTests;
	UMassEntityConfigAsset* Base = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassEnemyBase"));
	UMassEntityConfigAsset* Melee = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassMeleeTest"));
	UCastleCombatDataAsset* Combat = LoadObject<UCastleCombatDataAsset>(nullptr, TEXT("/Game/MovingCastle/Data/DA_CastleCombat"));
	if (!TestNotNull(TEXT("敌人基类资产存在"), Base) || !TestNotNull(TEXT("近战测试资产存在"), Melee)
		|| !TestNotNull(TEXT("战斗入口配置存在"), Combat)) { return false; }
	TestTrue(TEXT("子兵种使用普通 Mass 配置类"), Melee->GetClass() == UMassEntityConfigAsset::StaticClass());
	TestTrue(TEXT("测试兵继承敌人基类"), Melee->GetConfig().GetParent() == Base);
	TestTrue(TEXT("开局使用测试兵而非基类"), Combat->EnemyConfiguration == Melee);
	TestTrue(TEXT("圆环生成器默认配置为测试兵"), GetDefault<ACastleMassEnemySpawner>()->Configuration == Melee);
	TestNull(TEXT("基类未装配近战能力"), Base->FindTrait(UCastleMassMeleeAttackTrait::StaticClass()));
	const UCastleMassMeleeAttackTrait* Attack = Cast<UCastleMassMeleeAttackTrait>(Melee->FindTrait(UCastleMassMeleeAttackTrait::StaticClass()));
	if (!TestNotNull(TEXT("测试兵显式装配近战能力"), Attack)) { return false; }
	TestEqual(TEXT("测试兵伤害为 0"), Attack->AttackDamage, 0.0f);
	const UCastleMassEnemyTrait* Unit = CastChecked<UCastleMassEnemyTrait>(Melee->FindTrait(UCastleMassEnemyTrait::StaticClass()));
	TestEqual(TEXT("测试兵生命为 10"), Unit->MaxHealth, 10.0f);

	FScopedWorld Scope;
	TArray<FCastleMassUnitHandle> Handles;
	FText Reason;
	if (!TestTrue(TEXT("真实近战资产通过模板校验"), Scope.Units().SpawnUnits(Melee,
		{FTransform(FVector(1000, 0, 0))}, Handles, Reason))) { AddError(Reason.ToString()); return false; }
	Step<UCastleMassMeleeAttackProcessor>(Scope, 0.0f);
	FCastleMassUnitSnapshot Snapshot;
	Scope.Units().GetUnitSnapshot(Handles[0], Snapshot);
	TestTrue(TEXT("真实资产生成的测试兵会追踪主塔"), Snapshot.bHasMoveTarget);
	TestEqual(TEXT("真实实体使用子兵种生命"), Snapshot.Health, 10.0f);
	return true;
}

#endif
