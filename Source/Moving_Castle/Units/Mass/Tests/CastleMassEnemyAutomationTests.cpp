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
#include "Enemies/Flight/CastleMassFlightSolver.h"
#include "Containers/StaticArray.h"
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

	/** 按生产调度次序推进一帧飞行与近战意图，保持群体测试经过真实 Mass 处理器。 */
	void StepFlightAndMelee(FScopedWorld& TestWorld, const float DeltaTime)
	{
		Step<UCastleMassMovementProcessor>(TestWorld, DeltaTime);
		Step<UCastleMassMeleeAttackProcessor>(TestWorld, DeltaTime);
	}

	/** 检测四帧来回横摆且原地停滞的极限环，忽略浮点误差量级的位移。 */
	bool HasLateralLimitCycle(const TStaticArray<FVector, 4>& Steps, const int32 LastStep, const FVector& TravelAxis)
	{
		FVector NetDisplacement = FVector::ZeroVector;
		FVector PreviousLateral = FVector::ZeroVector;
		for (int32 Offset = 1; Offset <= Steps.Num(); ++Offset)
		{
			const FVector& Step = Steps[(LastStep + Offset) % Steps.Num()];
			const FVector Lateral = FVector::VectorPlaneProject(Step, TravelAxis);
			if (Lateral.SizeSquared() < FMath::Square(0.05)
				|| (Offset > 1 && FVector::DotProduct(PreviousLateral, Lateral) >= 0.0))
			{
				return false;
			}
			PreviousLateral = Lateral;
			NetDisplacement += Step;
		}
		return NetDisplacement.SizeSquared() < FMath::Square(0.2);
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
	Trait->Radius = 5.0f;
	Trait->Movement.MaxSpeed = 100.0f;
	Trait->Movement.Acceleration = 0.0f;
	Trait->Movement.AcceptanceRadius = 0.0f;

	TArray<FCastleMassUnitHandle> Handles;
	FText FailureReason;
	if (!TestTrue(TEXT("在主塔表面外侧生成一名敌人"), Scope.Units().SpawnUnits(Config, {FTransform(FVector(340.0f, 0.0f, 0.0f))}, Handles, FailureReason)))
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
	TestTrue(TEXT("敌人进入真实塔面攻击范围"), Snapshot.bInAttackRange);
	TestTrue(TEXT("攻击态保留由移动处理器消费的塔面停靠意图"), Snapshot.bHasMoveTarget);
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
	Spawner->InnerRadius = 600.0f;
	Spawner->OuterRadius = 700.0f;
	Spawner->HeightOffset = 50.0f;
	Spawner->RandomSeed = 20261112;

	TestTrue(TEXT("圆环生成器可以生成完整批次"), Spawner->SpawnUnits());
	TestEqual(TEXT("圆环生成器返回配置数量"), Spawner->SpawnedUnits.Num(), 32);
	for (const FCastleMassUnitHandle& Handle : Spawner->SpawnedUnits)
	{
		FCastleMassUnitSnapshot Snapshot;
		TestTrue(TEXT("圆环单位句柄有效"), Scope.Units().GetUnitSnapshot(Handle, Snapshot));
		const float Distance = FVector::Dist2D(Snapshot.Transform.GetLocation(), FVector::ZeroVector);
		TestTrue(TEXT("敌人距离位于圆环内外半径之间"), Distance >= 600.0f && Distance <= 700.0f);
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
		{FTransform(FVector(350, 0, 500))}, BaseHandles, Reason))
		|| !TestTrue(TEXT("近战能力通过组合生成"), Scope.Units().SpawnUnits(MakeEnemyConfig(),
		{FTransform(FVector(350, 0, 500))}, MeleeHandles, Reason))) { AddError(Reason.ToString()); return false; }
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

/** 几何回归覆盖缩放旋转、逻辑表面净距、低帧率球体投影与切向速度守恒。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleTowerPrismGeometryTest,
	"MovingCastle.Mass.Flight.TowerPrismGeometryAndSphereProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleTowerPrismGeometryTest::RunTest(const FString& Parameters)
{
	const FTransform ActorTransform(FRotator(0.0f, 31.0f, 0.0f), FVector(130.0f, -80.0f, 24.0f),
		FVector(1.5f, 0.75f, 2.0f));
	FCastleTowerPrism Tower;
	FString FailureReason;
	if (!TestTrue(TEXT("旋转并缩放的主塔可生成真实塔身快照"),
		FCastleTowerPrism::Build(ActorTransform, 515.0f, 1680.0f, Tower, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestTrue(TEXT("塔身 X 半边长吸收 Actor 缩放"), FMath::IsNearlyEqual(Tower.HalfSize.X, 386.25f));
	TestTrue(TEXT("塔身 Y 半边长吸收 Actor 缩放"), FMath::IsNearlyEqual(Tower.HalfSize.Y, 193.125f));
	TestTrue(TEXT("塔身高吸收 Actor Z 缩放"), FMath::IsNearlyEqual(Tower.HalfSize.Z, 3360.0f));

	const FVector LocalOutside(Tower.HalfSize.X + 10.0f, 0.0f, 800.0f);
	const FVector WorldOutside = Tower.BottomCenter + Tower.Rotation.RotateVector(LocalOutside);
	FVector SurfacePoint;
	FVector SurfaceNormal;
	float SurfaceDistance = 0.0f;
	TestTrue(TEXT("查询点命中塔身真实正侧面"),
		Tower.QuerySurface(WorldOutside, SurfacePoint, SurfaceNormal, SurfaceDistance));
	TestTrue(TEXT("有向净距保留 10 cm"), FMath::IsNearlyEqual(SurfaceDistance, 10.0f, 0.01f));
	TestTrue(TEXT("侧面法线跟随主塔旋转"), SurfaceNormal.Equals(Tower.Rotation.GetAxisX(), 0.01f));

	const FVector Tangent = Tower.Rotation.GetAxisY() * 50.0f;
	FVector SphereCenter = SurfacePoint + SurfaceNormal * 15.0f;
	FVector Velocity = -SurfaceNormal * 100.0f + Tangent;
	TestTrue(TEXT("相交的球体可投影到塔面外侧"), Tower.ProjectSphereOutside(SphereCenter, 22.0f, Velocity));
	TestTrue(TEXT("球心净距达到逻辑半径"), FVector::Dist(SphereCenter, SurfacePoint) >= 21.99f);
	TestTrue(TEXT("投影移除指向塔内的法向速度"), FMath::Abs(FVector::DotProduct(Velocity, SurfaceNormal)) <= 0.01f);
	TestTrue(TEXT("投影完整保留球体切向速度"), FMath::IsNearlyEqual(FVector::DotProduct(Velocity, Tower.Rotation.GetAxisY()), 50.0f, 0.01f));

	for (const int32 Layers : {5, 7, 9})
	{
		const FString Scenario = FString::Printf(TEXT("扩至 %d 层的旋转主塔"), Layers);
		FCastleTowerPrism ExpandedTower;
		if (!TestTrue(Scenario + TEXT("具有有效的实时几何"), FCastleTowerPrism::Build(
			ActorTransform, 515.0f, Layers * 560.0f, ExpandedTower, FailureReason))) { AddError(FailureReason); return false; }
		FCastleMassFlightBody FastBody;
		FastBody.EntityIndex = 91;
		FastBody.Radius = 22.0f;
		FastBody.MaxSpeed = 2000.0f;
		// 从新增楼层撞向塔身，复用旧高度的快照会在该高度放行这一步。
		const double AddedLayerHeight = ExpandedTower.HalfSize.Z - 560.0;
		FastBody.Location = ExpandedTower.BottomCenter + ExpandedTower.Rotation.RotateVector(
			FVector(ExpandedTower.HalfSize.X + 180.0f, 0.0f, AddedLayerHeight)) - ExpandedTower.GetUpVector() * FastBody.Radius;
		FastBody.DesiredVelocity = -ExpandedTower.Rotation.GetAxisX() * 1800.0f
			+ ExpandedTower.Rotation.GetAxisY() * 100.0f;
		TArray<FCastleMassFlightResult> FastResults;
		if (!TestTrue(Scenario + TEXT("可积分 0.2 秒低帧率步进"),
			FCastleMassFlightSolver::Solve({FastBody}, 0.2f, &ExpandedTower, FastResults, FailureReason)))
		{
			AddError(FailureReason);
			return false;
		}
		const FVector FastCenter = FastResults[0].Location + ExpandedTower.GetUpVector() * FastBody.Radius;
		if (!TestTrue(Scenario + TEXT("在高速步进后仍可查询塔面"),
			ExpandedTower.QuerySurface(FastCenter, SurfacePoint, SurfaceNormal, SurfaceDistance))) { return false; }
		TestTrue(Scenario + TEXT("的新增塔身阻止高速单位穿隧"),
			FMath::IsNearlyEqual(SurfaceDistance, FastBody.Radius, 0.5f)
			&& ExpandedTower.ToLocalPoint(FastCenter).X >= ExpandedTower.HalfSize.X + FastBody.Radius - 0.5f);
		TestTrue(Scenario + TEXT("保留高速球体的切向速度"),
			FMath::IsNearlyEqual(FVector::DotProduct(FastResults[0].Velocity, ExpandedTower.Rotation.GetAxisY()), 100.0f, 0.1f));
	}
	return true;
}

/** 解算器回归覆盖拥挤分离、200 ms 低帧率防穿隧和攻击态切向让位。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassFlightSolverAutomationTest,
	"MovingCastle.Mass.Flight.ContinuousSeparationCrossingAndTowerSlide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassFlightSolverAutomationTest::RunTest(const FString& Parameters)
{
	TArray<FCastleMassFlightBody> Bodies;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FCastleMassFlightBody& Body = Bodies.AddDefaulted_GetRef();
		Body.EntityIndex = Index + 1;
		Body.Location = FVector(Index == 0 ? 0.0f : 30.0f, 0.0f, 0.0f);
		Body.Radius = 22.0f;
		Body.MaxSpeed = 100.0f;
		Body.Acceleration = 0.0f;
	}
	TArray<FCastleMassFlightResult> Results;
	FString FailureReason;
	// 簇出生允许轻微初始重合，给连续力三个子步自然撑开个人空间。
	if (!TestTrue(TEXT("拥挤双单位 Boids 积分成功"),
		FCastleMassFlightSolver::Solve(Bodies, 0.1f, nullptr, Results, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	const FVector FirstAway = Results[0].Velocity;
	const FVector SecondAway = Results[1].Velocity;
	TestTrue(TEXT("连续软分离将重叠单位推向相反方向"), FirstAway.X < 0.0f && SecondAway.X > 0.0f);
	const double SeparatedDistance = FVector::Dist(Results[0].Location, Results[1].Location);
	TestTrue(FString::Printf(TEXT("连续分离后两球保持有效间距，实测 %.2f cm"), SeparatedDistance), SeparatedDistance >= 37.4f);

	FCastleTowerPrism Tower;
	TestTrue(TEXT("可构造围塔解算几何"), FCastleTowerPrism::Build(FTransform::Identity,
		515.0f, 1680.0f, Tower, FailureReason));
	Bodies.Reset();
	const float Radius = 22.0f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FCastleMassFlightBody& Body = Bodies.AddDefaulted_GetRef();
		Body.EntityIndex = Index + 11;
		Body.Location = FVector(257.5f + 40.0f + Index * 52.5f, 0.0f, 800.0f);
		Body.Radius = Radius;
		Body.MaxSpeed = 420.0f;
		Body.Acceleration = 1200.0f;
		Body.bTrackingMainTower = true;
		Body.bInAttackRange = true;
		Body.AttackRange = 130.0f;
		Body.IdealStandOff = 40.0f;
	}
	const FVector InitialFront = Bodies[0].Location;
	if (!TestTrue(TEXT("攻击态拥挤围塔解算成功"),
		FCastleMassFlightSolver::Solve(Bodies, 1.0f, &Tower, Results, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	const FVector SlideNormal = FVector::ForwardVector;
	const FVector Delta = Results[0].Location - InitialFront;
	const FVector TangentialDelta = Delta - SlideNormal * FVector::DotProduct(Delta, SlideNormal);
	TestTrue(TEXT("后排压墙时前排产生稳定切向让位"), TangentialDelta.Size() > 15.0f);
	FVector ClosestPoint;
	FVector OutwardNormal;
	float FrontSurfaceDistance = 0.0f;
	TestTrue(TEXT("让位后仍可查询到前排塔面"), Tower.QuerySurface(
		Results[0].Location + FVector::UpVector * Radius, ClosestPoint, OutwardNormal, FrontSurfaceDistance));
	TestTrue(TEXT("让位不改变近战贴靠净距"), FMath::IsNearlyEqual(FrontSurfaceDistance, 40.0f, 0.5f));

	// 两球同处于理想贴靠净距，将球心间距从壳内逐渐移到壳外，只剩可度量的切向排斥。
	Bodies[1].Location = Bodies[0].Location;
	const float AttackShell = 2.0f * Radius * 1.55f;
	const auto MeasureSlideSpeed = [&](const float Gap, float& OutSpeed)
	{
		Bodies[1].Location.Y = Bodies[0].Location.Y + Gap;
		if (!TestTrue(TEXT("贴墙个人空间单子步可积分"),
			FCastleMassFlightSolver::Solve(Bodies, 1.0f / 30.0f, &Tower, Results, FailureReason)))
		{
			AddError(FailureReason);
			return false;
		}
		OutSpeed = FMath::Abs(Results[0].Velocity.Y);
		return true;
	};
	float InnerSpeed = 0.0f, EdgeSpeed = 0.0f, OutsideSpeed = 0.0f;
	if (!MeasureSlideSpeed(AttackShell - 2.0f, InnerSpeed) || !MeasureSlideSpeed(AttackShell - 0.25f, EdgeSpeed)
		|| !MeasureSlideSpeed(AttackShell + 0.25f, OutsideSpeed)) { return false; }
	TestTrue(TEXT("个人空间边缘的微弱切向推力随间距平滑衰减到零"),
		InnerSpeed > EdgeSpeed && EdgeSpeed > 0.0f && EdgeSpeed < 2.0f && FMath::IsNearlyZero(OutsideSpeed));
	return true;
}

/** 两股三维错列群虫相向穿过，检验转向连续和相交后仍向各自目标前进。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassFlightCrossingAutomationTest,
	"MovingCastle.Mass.Flight.CrossingFlocksStayInMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassFlightCrossingAutomationTest::RunTest(const FString& Parameters)
{
	constexpr int32 MembersPerFlock = 4;
	constexpr float Speed = 420.0f;
	constexpr float Acceleration = 1200.0f;
	constexpr float StepSeconds = 1.0f / 60.0f;
	TArray<FCastleMassFlightBody> Bodies;
	TStaticArray<TStaticArray<FVector, 4>, 2 * MembersPerFlock> Displacements;
	for (int32 Index = 0; Index < 2 * MembersPerFlock; ++Index)
	{
		const int32 Lane = Index % MembersPerFlock;
		const float Direction = Index < MembersPerFlock ? 1.0f : -1.0f;
		auto& Body = Bodies.AddDefaulted_GetRef();
		Body.EntityIndex = Index + 1;
		Body.Location = FVector(-360.0f * Direction, (Lane % 2 * 2 - 1) * 32.0f + 11.0f * Direction,
			600.0f + (Lane / 2 * 2 - 1) * 24.0f + 14.0f * Direction);
		Body.Radius = 22.0f;
		Body.MaxSpeed = Speed;
		Body.Acceleration = Acceleration;
		Body.Velocity = FVector::ForwardVector * Direction * Speed;
		Body.Target = FVector(1800.0f * Direction, Body.Location.Y, Body.Location.Z);
		Body.bHasTarget = true;
	}
	TArray<FCastleMassFlightResult> Results;
	FString FailureReason;
	double LargestVelocityDelta = 0.0;
	bool bSawLimitCycle = false;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		if (!FCastleMassFlightSolver::Solve(Bodies, StepSeconds, nullptr, Results, FailureReason))
		{
			AddError(FailureReason);
			return false;
		}
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			auto& Body = Bodies[Index];
			const auto& Result = Results[Index];
			LargestVelocityDelta = FMath::Max(LargestVelocityDelta, FVector::Dist(Result.Velocity, Body.Velocity));
			Displacements[Index][Frame % 4] = Result.Location - Body.Location;
			bSawLimitCycle |= Frame >= 3 && CastleMassEnemyTests::HasLateralLimitCycle(Displacements[Index], Frame, FVector::ForwardVector);
			Body.Location = Result.Location;
			Body.Velocity = Result.Velocity;
		}
	}
	TestTrue(TEXT("交汇避让期间速度变化不超过子步加速度界"), LargestVelocityDelta <= Acceleration * StepSeconds + 0.01);
	TestFalse(TEXT("交汇期间无四帧原地来回横摆"), bSawLimitCycle);
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		const float Direction = Index < MembersPerFlock ? 1.0f : -1.0f;
		TestTrue(TEXT("所有群虫均穿过对向群并继续向目标前进"), Bodies[Index].Location.X * Direction > 300.0f);
	}
	return true;
}

/** 用真实 Mass Processor 推进两种来袭角度，验证 18 只同侧群虫的攻击、贴墙与无死锁。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassCrowdedTowerFlowAutomationTest,
	"MovingCastle.Mass.Flight.MultiWaveTowerFlow180And210",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassCrowdedTowerFlowAutomationTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassEnemyTests;
	// 分开创建游戏 World，避免上一种来袭角度遗留主塔生命、冷却或实体。
	const auto RunApproach = [this](const float AngleDegrees)
	{
		FScopedWorld Scope;
		if (!TestNotNull(TEXT("创建围塔测试 World"), Scope.World)
			|| !TestNotNull(TEXT("创建围塔测试主塔"), Scope.Tower)) { return false; }
		UMassEntityConfigAsset* Config = MakeEnemyConfig();
		auto* Enemy = CastChecked<UCastleMassEnemyTrait>(
			Config->GetMutableConfig().FindMutableTrait(UCastleMassEnemyTrait::StaticClass()));
		auto* Melee = CastChecked<UCastleMassMeleeAttackTrait>(
			Config->GetMutableConfig().FindMutableTrait(UCastleMassMeleeAttackTrait::StaticClass()));
		Enemy->Radius = 22.0f;
		Enemy->Movement.MaxSpeed = 420.0f;
		Enemy->Movement.Acceleration = 1200.0f;
		Enemy->Movement.AcceptanceRadius = 0.0f;
		Melee->AttackDamage = 0.1f;
		Melee->AttackInterval = 0.8f;
		Melee->AttackRange = 130.0f;

		TArray<FTransform> SpawnTransforms;
		const FVector Approach(FMath::Cos(FMath::DegreesToRadians(AngleDegrees)),
			FMath::Sin(FMath::DegreesToRadians(AngleDegrees)), 0.0f);
		const FVector Lateral = FVector::CrossProduct(FVector::UpVector, Approach).GetSafeNormal();
		for (int32 Index = 0; Index < 18; ++Index)
		{
			const int32 LateralLane = Index % 3;
			const int32 HeightLane = Index / 3;
			// 初始队形采用紧凑的错列三维簇，保留 200 cm 以上高度跨度，避免测试装置人为制造远距孤点。
			const float LateralOffset = static_cast<float>(LateralLane - 1) * 48.0f
				+ (HeightLane % 2 == 0 ? 0.0f : 24.0f);
			const float HeightBand = 680.0f + static_cast<float>(HeightLane) * 42.0f
				+ static_cast<float>(LateralLane - 1) * 7.0f;
			const FVector Location = Approach * 750.0f + Lateral * LateralOffset + FVector::UpVector * HeightBand;
			SpawnTransforms.Emplace((FVector::ZeroVector - Location).Rotation(), Location);
		}
		TArray<FCastleMassUnitHandle> Handles;
		FText SpawnFailure;
		if (!TestTrue(TEXT("从单侧同时生成 18 只群虫"),
			Scope.Units().SpawnUnits(Config, SpawnTransforms, Handles, SpawnFailure)))
		{
			AddError(SpawnFailure.ToString());
			return false;
		}

		FCastleTowerPrism Tower;
		FString GeometryFailure;
		if (!Scope.Tower->GetTowerShaftPrism(Tower, GeometryFailure))
		{
			AddError(GeometryFailure);
			return false;
		}
		const FString Scenario = FString::Printf(TEXT("%g° 同侧来袭"), AngleDegrees);
		float LowestFlightClearance = TNumericLimits<float>::Max();
		double LowestFlightRootHeight = TNumericLimits<double>::Max();
		TArray<FVector> LastLocations;
		LastLocations.Reserve(SpawnTransforms.Num());
		for (const FTransform& Transform : SpawnTransforms) { LastLocations.Add(Transform.GetLocation()); }
		TArray<TStaticArray<FVector, 4>> MovementHistory;
		MovementHistory.SetNum(Handles.Num());
		bool bSawLimitCycle = false;
		for (int32 Frame = 0; Frame < 150; ++Frame)
		{
			StepFlightAndMelee(Scope, 0.1f);
			for (int32 Index = 0; Index < Handles.Num(); ++Index)
			{
				FCastleMassUnitSnapshot Snapshot;
				FVector SurfacePoint, SurfaceNormal;
				float SurfaceDistance = 0.0f;
				if (!Scope.Units().GetUnitSnapshot(Handles[Index], Snapshot)
					|| !Tower.QuerySurface(Snapshot.Transform.GetLocation() + Tower.GetUpVector() * 22.0f,
						SurfacePoint, SurfaceNormal, SurfaceDistance))
				{
					AddError(Scenario + TEXT("的轨迹快照或塔面距离查询失败。"));
					return false;
				}
				const FVector Location = Snapshot.Transform.GetLocation();
				LowestFlightClearance = FMath::Min(LowestFlightClearance, SurfaceDistance);
				LowestFlightRootHeight = FMath::Min(LowestFlightRootHeight, Tower.ToLocalPoint(Location).Z);
				auto& Steps = MovementHistory[Index];
				Steps[Frame % 4] = Location - LastLocations[Index];
				bSawLimitCycle |= Frame >= 3 && CastleMassEnemyTests::HasLateralLimitCycle(Steps, Frame, Approach);
				LastLocations[Index] = Location;
			}
		}
		TestTrue(Scenario + TEXT("全过程球心未进入塔身且实体根节点未钻入塔底"),
			LowestFlightClearance >= 21.5f && LowestFlightRootHeight >= -0.5);
		TestFalse(Scenario + TEXT("全过程无四帧原地来回横摆"), bSawLimitCycle);

		TArray<FVector> Centers;
		Centers.Reserve(Handles.Num());
		float LowestAttackingHeight = TNumericLimits<float>::Max();
		float HighestAttackingHeight = -TNumericLimits<float>::Max();
		float MinimumWallDistance = TNumericLimits<float>::Max();
		float MaximumWallDistance = 0.0f;
		double MaximumHeightChange = 0.0;
		bool bSawXFace = false;
		bool bSawYFace = false;
		for (const FCastleMassUnitHandle& Handle : Handles)
		{
			FCastleMassUnitSnapshot Snapshot;
			if (!Scope.Units().GetUnitSnapshot(Handle, Snapshot))
			{
				AddError(TEXT("围塔实体快照读取失败。"));
				return false;
			}
			TestTrue(TEXT("每只来袭群虫都进入射程并完成至少两次攻击"), Snapshot.AttackCount >= 2 && Snapshot.bInAttackRange);
			const FVector Center = Snapshot.Transform.GetLocation() + Tower.GetUpVector() * 22.0f;
			FVector SurfacePoint;
			FVector SurfaceNormal;
			float SurfaceDistance = 0.0f;
			if (!Tower.QuerySurface(Center, SurfacePoint, SurfaceNormal, SurfaceDistance))
			{
				AddError(TEXT("围塔球心主塔表面距离查询失败。"));
				return false;
			}
			MinimumWallDistance = FMath::Min(MinimumWallDistance, SurfaceDistance);
			MaximumWallDistance = FMath::Max(MaximumWallDistance, SurfaceDistance);
			if (Snapshot.AttackCount > 0)
			{
				const FVector InitialRoot = SpawnTransforms[Centers.Num()].GetLocation();
				MaximumHeightChange = FMath::Max(MaximumHeightChange, FMath::Abs(Snapshot.Transform.GetLocation().Z - InitialRoot.Z));
				TestTrue(TEXT("攻击态群虫贴墙落入指定近战净距"), SurfaceDistance >= 22.0f && SurfaceDistance <= 75.0f);
				LowestAttackingHeight = FMath::Min(LowestAttackingHeight, Center.Z);
				HighestAttackingHeight = FMath::Max(HighestAttackingHeight, Center.Z);
				bSawXFace |= FMath::Abs(SurfaceNormal.X) > 0.75f;
				bSawYFace |= FMath::Abs(SurfaceNormal.Y) > 0.75f;
			}
			Centers.Add(Center);
		}

		float MinimumPairDistance = TNumericLimits<float>::Max();
		float SumNearestNeighborDistances = 0.0f;
		float MaximumNearestNeighborDistance = 0.0f;
		for (int32 First = 0; First < Centers.Num(); ++First)
		{
			float NearestNeighborDistance = TNumericLimits<float>::Max();
			for (int32 Second = 0; Second < Centers.Num(); ++Second)
			{
				if (First == Second) { continue; }
				const float Distance = FVector::Distance(Centers[First], Centers[Second]);
				MinimumPairDistance = FMath::Min(MinimumPairDistance, Distance);
				NearestNeighborDistance = FMath::Min(NearestNeighborDistance, Distance);
			}
			SumNearestNeighborDistances += NearestNeighborDistance;
			MaximumNearestNeighborDistance = FMath::Max(MaximumNearestNeighborDistance, NearestNeighborDistance);
		}
		const float MeanNearestNeighborDistance = SumNearestNeighborDistances / Centers.Num();
		AddInfo(FString::Printf(TEXT("%s，稳定态塔面净距 %.2f～%.2f cm，最近邻球心距 %.2f～%.2f cm，平均 %.2f cm。"),
			*Scenario, MinimumWallDistance, MaximumWallDistance, MinimumPairDistance,
			MaximumNearestNeighborDistance, MeanNearestNeighborDistance));
		TestTrue(Scenario + TEXT("的任意两只群虫球心相距至少 37.4 cm"), MinimumPairDistance >= 37.4f);
		TestTrue(Scenario + TEXT("的平均最近邻球心距位于 44～95 cm"),
			MeanNearestNeighborDistance >= 44.0f && MeanNearestNeighborDistance <= 95.0f);
		TestTrue(Scenario + TEXT("的单只最大最近邻球心距不超过 160 cm"), MaximumNearestNeighborDistance <= 160.0f);
		TestTrue(Scenario + TEXT("的群虫绕行至相邻塔面或在高度上主动让位"),
			(bSawXFace && bSawYFace) || (HighestAttackingHeight - LowestAttackingHeight >= 200.0f && MaximumHeightChange > 15.0));
		return true;
	};

	const bool bHeadOnCompleted = RunApproach(180.0f);
	const bool bDiagonalCompleted = RunApproach(210.0f);
	return bHeadOnCompleted && bDiagonalCompleted;
}

/** 先建立稳定攻击前排，再从正后方生成拥挤单位，验证让位期间仍连续造成伤害。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassMeleeTowerSlideAutomationTest,
	"MovingCastle.Mass.Flight.AttackingFrontSlidesAndKeepsDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassMeleeTowerSlideAutomationTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassEnemyTests;
	FScopedWorld Scope;
	if (!TestNotNull(TEXT("创建前排滑移测试 World"), Scope.World)
		|| !TestNotNull(TEXT("创建前排滑移测试主塔"), Scope.Tower)) { return false; }
	UMassEntityConfigAsset* Config = MakeEnemyConfig();
	auto* Enemy = CastChecked<UCastleMassEnemyTrait>(Config->GetMutableConfig().FindMutableTrait(UCastleMassEnemyTrait::StaticClass()));
	auto* Melee = CastChecked<UCastleMassMeleeAttackTrait>(Config->GetMutableConfig().FindMutableTrait(UCastleMassMeleeAttackTrait::StaticClass()));
	Enemy->Radius = 22.0f;
	Enemy->Movement.MaxSpeed = 420.0f;
	Enemy->Movement.Acceleration = 1200.0f;
	Melee->AttackDamage = 1.0f;
	Melee->AttackInterval = 0.2f;
	Melee->AttackRange = 130.0f;
	TArray<FCastleMassUnitHandle> Front;
	FText SpawnFailure;
	if (!TestTrue(TEXT("在真实塔面净距 40 cm 处生成前排近战兵"), Scope.Units().SpawnUnits(
		Config, {FTransform(FVector(297.5f, 0.0f, 800.0f))}, Front, SpawnFailure)))
	{
		AddError(SpawnFailure.ToString());
		return false;
	}
	StepFlightAndMelee(Scope, 0.0f);
	FCastleMassUnitSnapshot Before;
	if (!TestTrue(TEXT("前排已进入近战攻击态"), Scope.Units().GetUnitSnapshot(Front[0], Before) && Before.bInAttackRange)) { return false; }
	const FVector InitialLocation = Before.Transform.GetLocation();
	const int32 InitialAttackCount = Before.AttackCount;
	const float InitialTowerHealth = Scope.Tower->CurrentHealth;

	TArray<FTransform> RearTransforms;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const float SideOffset = (static_cast<float>(Index) - 2.5f) * 5.0f;
		RearTransforms.Emplace(FVector(350.0f, SideOffset, 800.0f));
	}
	TArray<FCastleMassUnitHandle> Rear;
	if (!TestTrue(TEXT("从前排正后方生成后续群虫"), Scope.Units().SpawnUnits(Config, RearTransforms, Rear, SpawnFailure)))
	{
		AddError(SpawnFailure.ToString());
		return false;
	}
	bool bFrontStayedInRange = true;
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		StepFlightAndMelee(Scope, 0.1f);
		FCastleMassUnitSnapshot During;
		bFrontStayedInRange &= Scope.Units().GetUnitSnapshot(Front[0], During) && During.bInAttackRange;
	}

	FCastleMassUnitSnapshot After;
	if (!TestTrue(TEXT("前排滑移后快照仍有效"), Scope.Units().GetUnitSnapshot(Front[0], After))) { return false; }
	FCastleTowerPrism Tower;
	FString GeometryFailure;
	if (!Scope.Tower->GetTowerShaftPrism(Tower, GeometryFailure)) { AddError(GeometryFailure); return false; }
	const FVector Center = After.Transform.GetLocation() + Tower.GetUpVector() * 22.0f;
	FVector SurfacePoint;
	FVector SurfaceNormal;
	float SurfaceDistance = 0.0f;
	if (!Tower.QuerySurface(Center, SurfacePoint, SurfaceNormal, SurfaceDistance))
	{
		AddError(TEXT("前排滑移后主塔表面距离查询失败。"));
		return false;
	}
	const FVector Displacement = After.Transform.GetLocation() - InitialLocation;
	const FVector TangentialDisplacement = Displacement - SurfaceNormal * FVector::DotProduct(Displacement, SurfaceNormal);
	TestTrue(TEXT("前排单位沿塔面切向滑移超过 15 cm"), TangentialDisplacement.Size() > 15.0f);
	TestTrue(TEXT("滑移期间前排每帧处于攻击态且有效攻击次数持续增长"),
		bFrontStayedInRange && After.AttackCount > InitialAttackCount);
	TestTrue(TEXT("滑移期间持续造成主塔伤害"), Scope.Tower->CurrentHealth < InitialTowerHealth);
	TestTrue(TEXT("滑移后的前排仍保持紧贴真实墙面"), SurfaceDistance >= 22.0f && SurfaceDistance <= 75.0f);
	return true;
}

#endif
