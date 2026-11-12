#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Async/Async.h"
#include "CastleMassFragments.h"
#include "CastleMassProcessors.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityQuery.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "MassExecutor.h"
#include "MassLODTrait.h"
#include "MassMovementFragments.h"
#include "MassProcessingContext.h"
#include "MassRepresentationFragments.h"
#include "Misc/AutomationTest.h"
#include "Projectiles/CastleMassHomingProjectileFragments.h"
#include "Projectiles/CastleMassHomingProjectileProcessor.h"
#include "Projectiles/CastleMassHomingProjectileTrait.h"
#include <limits>

namespace CastleMassHomingTests
{
	/** 独立游戏世界仅运行需要的处理器，运动结算不依赖场景、美术或塔层实现。 */
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
		~FScopedWorld() { if (World) { World->DestroyWorld(false); } }
		FMassEntityManager& Manager() const { return World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager(); }
		UCastleMassUnitSubsystem& Units() const { return *World->GetSubsystem<UCastleMassUnitSubsystem>(); }
	};

	/** 使用引擎执行器建立真实查询上下文，结束后提交生命周期的延迟销毁。 */
	template<typename T>
	void Step(FScopedWorld& Scope, float DeltaTime)
	{
		Scope.Manager().FlushCommands();
		T* Processor = NewObject<T>(Scope.World);
		Processor->CallInitialize(Scope.World, Scope.Manager().AsShared());
		UE::Mass::FProcessingContext Context(Scope.Manager(), DeltaTime);
		UE::Mass::Executor::Run(*Processor, Context);
	}

	/** 纯模拟配置显式给出测试速度和半径；高模板伤害用于证明命中不读取 Trait 的伤害。 */
	template<typename T>
	UMassEntityConfigAsset* MakeConfig()
	{
		UMassEntityConfigAsset* Config = NewObject<UMassEntityConfigAsset>();
		T* Trait = NewObject<T>(Config);
		Trait->MaxHealth = 100.0f;
		Trait->Radius = 3.0f;
		Trait->Movement.MaxSpeed = 100.0f;
		Trait->Movement.Acceleration = 0.0f;
		Config->GetMutableConfig().AddTrait(*Trait);
		return Config;
	}

	UMassEntityConfigAsset* MakeProjectileConfig()
	{
		UMassEntityConfigAsset* Config = MakeConfig<UCastleMassHomingProjectileTrait>();
		auto* Trait = CastChecked<UCastleMassHomingProjectileTrait>(Config->GetMutableConfig().FindMutableTrait(UCastleMassHomingProjectileTrait::StaticClass()));
		Trait->MaxHealth = 1.0f;
		Trait->Radius = 2.0f;
		Trait->Lifetime = 5.0f;
		Trait->Damage = 999.0f;
		return Config;
	}

	/** 统计实际实体，防止接口返回失败却已经偷偷生成前半批。 */
	int32 CountProjectiles(FScopedWorld& Scope)
	{
		FMassEntityQuery Query(Scope.Manager().AsShared());
		Query.AddRequirement<FCastleMassHomingProjectileFragment>(EMassFragmentAccess::ReadOnly);
		FMassExecutionContext Context(Scope.Manager(), 0.0f, false);
		int32 Count = 0;
		Query.ForEachEntityChunk(Context, [&Count](FMassExecutionContext& Chunk) { Count += Chunk.GetNumEntities(); });
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassNearestEnemyTest, "MovingCastle.Mass.Homing.NearestEnemy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassNearestEnemyTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassHomingTests;
	FScopedWorld Scope;
	TArray<FCastleMassUnitHandle> Enemies, NPCs;
	FText Reason;
	if (!TestTrue(TEXT("生成不同三维位置的敌人"), Scope.Units().SpawnUnits(MakeConfig<UCastleMassEnemyTrait>(),
		{FTransform(FVector(100, 0, 0)), FTransform(FVector(0, 0, 150)), FTransform(FVector(2, 0, 0))}, Enemies, Reason))
		|| !TestTrue(TEXT("生成比敌人更近的 NPC"), Scope.Units().SpawnUnits(MakeConfig<UCastleMassNPCTrait>(),
			{FTransform(FVector(1, 0, 0))}, NPCs, Reason))) { AddError(Reason.ToString()); return false; }
	Scope.Units().DamageUnits({Enemies[2]}, 100.0f);
	Scope.Manager().FlushCommands();
	FCastleMassUnitHandle Found = Enemies[0];
	TestFalse(TEXT("范围内仅有 NPC 和死亡敌人时没有目标"), Scope.Units().FindNearestEnemy(FVector::ZeroVector, 99.0f, Found));
	TestFalse(TEXT("没有目标时清空旧输出句柄"), Found.Entity.IsSet());
	TestTrue(TEXT("边界上的存活敌人仍在范围内"), Scope.Units().FindNearestEnemy(FVector::ZeroVector, 100.0f, Found));
	TestTrue(TEXT("NPC 与近处死亡敌人均未被选择"), Found.Entity == Enemies[0].Entity && Found.World == Scope.World);
	TestTrue(TEXT("高度参与最近距离和范围计算"), Scope.Units().FindNearestEnemy(FVector(0, 0, 140), 10.0f, Found));
	TestTrue(TEXT("三维查询选择高处敌人"), Found.Entity == Enemies[1].Entity);
	TestFalse(TEXT("零半径不扩大成默认搜索范围"), Scope.Units().FindNearestEnemy(FVector::ZeroVector, 0.0f, Found));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassHomingHitTest, "MovingCastle.Mass.Homing.SteeringAndContinuousHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassHomingHitTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassHomingTests;
	FScopedWorld Scope;
	TArray<FCastleMassUnitHandle> Enemies, Projectiles;
	FText Reason;
	UMassEntityConfigAsset* ProjectileConfig = MakeProjectileConfig();
	if (!TestTrue(TEXT("生成锁定敌人"), Scope.Units().SpawnUnits(MakeConfig<UCastleMassEnemyTrait>(),
		{FTransform(FVector(100, 0, 0))}, Enemies, Reason))) { AddError(Reason.ToString()); return false; }
	FCastleMassHomingProjectileSpawn Shot;
	Shot.Target = Enemies[0];
	Shot.Damage = 7.0f;
	if (!TestTrue(TEXT("生成逐发伤害为 7 的追踪弹"), Scope.Units().SpawnHomingProjectiles(ProjectileConfig, {Shot}, Projectiles, Reason)))
	{
		AddError(Reason.ToString()); return false;
	}
	const FCastleMassUnitHandle First = Projectiles[0];
	TestTrue(TEXT("创建 Observer 后仍保留精确发射速度"), Scope.Manager().GetFragmentDataChecked<FMassVelocityFragment>(First.Entity).Value.Equals(FVector(100, 0, 0)));
	Step<UCastleMassProjectileProcessor>(Scope, 0.25f);
	TestTrue(TEXT("基类弹道不重复推进追踪子类"), Scope.Manager().GetFragmentDataChecked<FTransformFragment>(First.Entity).GetTransform().GetLocation().IsZero());
	Step<UCastleMassHomingProjectileProcessor>(Scope, 0.25f);
	TestTrue(TEXT("按配置速度飞行"), Scope.Manager().GetFragmentDataChecked<FTransformFragment>(First.Entity).GetTransform().GetLocation().Equals(FVector(25, 0, 0)));
	Scope.Units().SetUnitsVelocity(Enemies, FVector(0, 100, 0));
	Step<UCastleMassMovementProcessor>(Scope, 0.5f);
	Step<UCastleMassHomingProjectileProcessor>(Scope, 0.25f);
	const FVector Velocity = Scope.Manager().GetFragmentDataChecked<FMassVelocityFragment>(First.Entity).Value;
	TestTrue(TEXT("目标移动后追踪弹改变飞行方向"), Velocity.Y > 0.0 && Velocity.X > 0.0);
	TestTrue(TEXT("转向后速度大小仍等于配置 MaxSpeed"), FMath::IsNearlyEqual(Velocity.Size(), 100.0));
	TestTrue(TEXT("保留当前飞行线段起点"), Scope.Manager().GetFragmentDataChecked<FCastleMassProjectileFragment>(First.Entity).PreviousLocation.Equals(FVector(25, 0, 0)));
	Step<UCastleMassHomingProjectileProcessor>(Scope, 2.0f);
	TestEqual(TEXT("大步长跨过目标时仍命中，伤害读取逐发参数"), Scope.Manager().GetFragmentDataChecked<FCastleMassUnitFragment>(Enemies[0].Entity).Health, 93.0f);
	Step<UCastleMassLifetimeProcessor>(Scope, 0.0f);
	TestFalse(TEXT("命中后子弹由生命周期回收"), Scope.Units().IsUnitValid(First));

	// 零伤害是攻击方显式选择，不能把 0 解释为缺省并使用模板上的 999。
	Shot.Origin = Scope.Manager().GetFragmentDataChecked<FTransformFragment>(Enemies[0].Entity).GetTransform().GetLocation();
	Shot.Damage = 0.0f;
	if (!TestTrue(TEXT("允许逐发零伤害"), Scope.Units().SpawnHomingProjectiles(ProjectileConfig, {Shot}, Projectiles, Reason))) { AddError(Reason.ToString()); return false; }
	Step<UCastleMassHomingProjectileProcessor>(Scope, 0.0f);
	Step<UCastleMassLifetimeProcessor>(Scope, 0.0f);
	TestEqual(TEXT("零伤害不会使用 Trait 伤害"), Scope.Manager().GetFragmentDataChecked<FCastleMassUnitFragment>(Enemies[0].Entity).Health, 93.0f);
	TestFalse(TEXT("重合且零伤害的子弹仍完成命中回收"), Scope.Units().IsUnitValid(Projectiles[0]));
	Shot.Damage = 100.0f;
	if (!TestTrue(TEXT("生成致命伤害子弹"), Scope.Units().SpawnHomingProjectiles(ProjectileConfig, {Shot}, Projectiles, Reason))) { AddError(Reason.ToString()); return false; }
	Step<UCastleMassHomingProjectileProcessor>(Scope, 0.0f);
	Step<UCastleMassLifetimeProcessor>(Scope, 0.0f);
	TestFalse(TEXT("目标死亡复用同一生命回收流程"), Scope.Units().IsUnitValid(Enemies[0]));
	TestFalse(TEXT("致命命中的子弹同时回收"), Scope.Units().IsUnitValid(Projectiles[0]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassHomingTargetLostTest, "MovingCastle.Mass.Homing.TargetLost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassHomingTargetLostTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassHomingTests;
	FScopedWorld Scope;
	TArray<FCastleMassUnitHandle> Enemies, Projectiles, Replacements;
	FText Reason;
	UMassEntityConfigAsset* EnemyConfig = MakeConfig<UCastleMassEnemyTrait>();
	if (!TestTrue(TEXT("生成即将死亡和消失的目标"), Scope.Units().SpawnUnits(EnemyConfig,
		{FTransform(FVector(100, 0, 0)), FTransform(FVector(200, 0, 0)), FTransform(FVector(10, 0, 0))}, Enemies, Reason))) { AddError(Reason.ToString()); return false; }
	FCastleMassHomingProjectileSpawn FirstShot, SecondShot;
	FirstShot.Target = Enemies[0]; FirstShot.Damage = 17.0f;
	SecondShot.Target = Enemies[1]; SecondShot.Damage = 23.0f;
	if (!TestTrue(TEXT("整批生成两个独立锁定弹"), Scope.Units().SpawnHomingProjectiles(MakeProjectileConfig(),
		{FirstShot, SecondShot}, Projectiles, Reason))) { AddError(Reason.ToString()); return false; }
	Scope.Units().DamageUnits({Enemies[0]}, 100.0f);
	Scope.Manager().FlushCommands();
	Scope.Manager().DestroyEntity(Enemies[1].Entity);
	if (!TestTrue(TEXT("消失后生成其他敌人，允许引擎复用索引"), Scope.Units().SpawnUnits(EnemyConfig,
		{FTransform(FVector(200, 0, 0))}, Replacements, Reason))) { AddError(Reason.ToString()); return false; }
	AddExpectedMessage(TEXT("已死亡或消失，本发回收且不造成伤害"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2, false);
	Step<UCastleMassHomingProjectileProcessor>(Scope, 2.0f);
	TestEqual(TEXT("已经死亡的目标未被追加伤害"), Scope.Manager().GetFragmentDataChecked<FCastleMassUnitFragment>(Enemies[0].Entity).Health, 0.0f);
	TestEqual(TEXT("不会重新锁定更近的存活敌人"), Scope.Manager().GetFragmentDataChecked<FCastleMassUnitFragment>(Enemies[2].Entity).Health, 100.0f);
	TestEqual(TEXT("实体序列号阻止伤害复用槽位的新敌人"), Scope.Manager().GetFragmentDataChecked<FCastleMassUnitFragment>(Replacements[0].Entity).Health, 100.0f);
	Step<UCastleMassLifetimeProcessor>(Scope, 0.0f);
	TestFalse(TEXT("死亡目标对应的子弹同帧回收"), Scope.Units().IsUnitValid(Projectiles[0]));
	TestFalse(TEXT("消失目标对应的子弹同帧回收"), Scope.Units().IsUnitValid(Projectiles[1]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassHomingBatchValidationTest, "MovingCastle.Mass.Homing.BatchValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassHomingBatchValidationTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassHomingTests;
	FScopedWorld Scope, Other;
	TArray<FCastleMassUnitHandle> Enemies, NPCs, Foreign, Projectiles;
	FText Reason;
	UMassEntityConfigAsset* EnemyConfig = MakeConfig<UCastleMassEnemyTrait>();
	if (!TestTrue(TEXT("生成校验目标"), Scope.Units().SpawnUnits(EnemyConfig,
		{FTransform(FVector(100, 0, 0)), FTransform(FVector(200, 0, 0))}, Enemies, Reason))
		|| !TestTrue(TEXT("生成 NPC 校验目标"), Scope.Units().SpawnUnits(MakeConfig<UCastleMassNPCTrait>(), {FTransform::Identity}, NPCs, Reason))
		|| !TestTrue(TEXT("生成异世界目标"), Other.Units().SpawnUnits(EnemyConfig, {FTransform::Identity}, Foreign, Reason))) { AddError(Reason.ToString()); return false; }
	Scope.Units().DamageUnits({Enemies[1]}, 100.0f);
	Scope.Manager().FlushCommands();
	UMassEntityConfigAsset* Config = MakeProjectileConfig();
	FCastleMassHomingProjectileSpawn ValidShot;
	ValidShot.Target = Enemies[0]; ValidShot.Damage = 5.0f;
	if (!TestTrue(TEXT("建立非空实体基线"), Scope.Units().SpawnHomingProjectiles(Config, {ValidShot}, Projectiles, Reason))) { AddError(Reason.ToString()); return false; }
	const int32 Before = CountProjectiles(Scope);
	AddExpectedMessage(TEXT("SpawnHomingProjectiles："), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 11, false);
	const auto RejectBatch = [this, &Scope, &Projectiles, &Reason, Before](const TCHAR* Label, UMassEntityConfigAsset* InputConfig,
		const TArray<FCastleMassHomingProjectileSpawn>& Shots)
	{
		TestFalse(Label, Scope.Units().SpawnHomingProjectiles(InputConfig, Shots, Projectiles, Reason));
		TestTrue(TEXT("拒绝时输出句柄清空"), Projectiles.IsEmpty());
		TestFalse(TEXT("拒绝时提供明确原因"), Reason.IsEmpty());
		TestEqual(TEXT("拒绝时没有部分生成"), CountProjectiles(Scope), Before);
	};
	RejectBatch(TEXT("普通子弹配置不能发射追踪弹"), MakeConfig<UCastleMassProjectileTrait>(), {ValidShot});
	RejectBatch(TEXT("缺失配置整批拒绝"), nullptr, {ValidShot});
	RejectBatch(TEXT("空发射批次拒绝"), Config, {});
	FCastleMassHomingProjectileSpawn Invalid = ValidShot;
	Invalid.Damage = -1.0f;
	RejectBatch(TEXT("后续发射项负伤害导致整批拒绝"), Config, {ValidShot, Invalid});
	Invalid.Damage = std::numeric_limits<float>::infinity();
	RejectBatch(TEXT("非有限伤害整批拒绝"), Config, {ValidShot, Invalid});
	Invalid = ValidShot; Invalid.Origin.X = std::numeric_limits<double>::infinity();
	RejectBatch(TEXT("无效发射位置整批拒绝"), Config, {ValidShot, Invalid});
	Invalid = ValidShot; Invalid.Target = NPCs[0];
	RejectBatch(TEXT("NPC 目标导致整批拒绝"), Config, {ValidShot, Invalid});
	Invalid.Target = Enemies[1];
	RejectBatch(TEXT("死亡目标导致整批拒绝"), Config, {ValidShot, Invalid});
	Invalid.Target = Foreign[0];
	RejectBatch(TEXT("异世界目标导致整批拒绝"), Config, {ValidShot, Invalid});
	Invalid.Target = FCastleMassUnitHandle();
	RejectBatch(TEXT("未设置的目标导致整批拒绝"), Config, {ValidShot, Invalid});
	UMassEntityConfigAsset* ProjectileParent = MakeConfig<UCastleMassProjectileTrait>();
	UMassEntityConfigAsset* Conflicting = MakeProjectileConfig();
	Conflicting->GetMutableConfig().SetParentAsset(*ProjectileParent);
	RejectBatch(TEXT("含普通子弹 Trait 的父链与追踪 Trait 组合会被整批拒绝"), Conflicting, {ValidShot});
	AddExpectedMessage(TEXT("SpawnUnits：配置"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1, false);
	TestFalse(TEXT("通用入口不能隐式发射无目标追踪弹"), Scope.Units().SpawnUnits(Config, {FTransform::Identity}, Projectiles, Reason));
	TestEqual(TEXT("通用入口同样没有部分生成"), CountProjectiles(Scope), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassHomingCallBoundaryTest, "MovingCastle.Mass.Homing.CallBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassHomingCallBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassHomingTests;
	FScopedWorld Scope;
	FCastleMassUnitHandle Found;
	TArray<FCastleMassUnitHandle> Projectiles;
	FText Reason;
	AddExpectedMessage(TEXT("FindNearestEnemy："), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 3, false);
	AddExpectedMessage(TEXT("SpawnHomingProjectiles："), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 2, false);
	TestFalse(TEXT("负查询半径明确拒绝"), Scope.Units().FindNearestEnemy(FVector::ZeroVector, -1.0f, Found));
	{
		auto Processing = Scope.Manager().NewProcessingScope();
		TestFalse(TEXT("Mass 正在处理时拒绝查询"), Scope.Units().FindNearestEnemy(FVector::ZeroVector, 100.0f, Found));
		TestFalse(TEXT("Mass 正在处理时拒绝生成"), Scope.Units().SpawnHomingProjectiles(nullptr, {}, Projectiles, Reason));
	}
	// 强制独立线程调用边界，不从该线程触碰实体存储；接口应在 GetManager 前提检查处失败。
	TestFalse(TEXT("工作线程拒绝查询"), Async(EAsyncExecution::Thread, [&Scope]
	{
		FCastleMassUnitHandle ThreadFound;
		return Scope.Units().FindNearestEnemy(FVector::ZeroVector, 100.0f, ThreadFound);
	}).Get());
	TestFalse(TEXT("工作线程拒绝生成"), Async(EAsyncExecution::Thread, [&Scope]
	{
		TArray<FCastleMassUnitHandle> ThreadProjectiles;
		FText ThreadReason;
		return Scope.Units().SpawnHomingProjectiles(nullptr, {}, ThreadProjectiles, ThreadReason);
	}).Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMassArrowAssetTest, "MovingCastle.Mass.Homing.ArrowAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMassArrowAssetTest::RunTest(const FString& Parameters)
{
	using namespace CastleMassHomingTests;
	UMassEntityConfigAsset* Arrow = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassArrowProjectile"));
	if (!TestNotNull(TEXT("独立箭矢配置存在"), Arrow)) { return false; }
	TestTrue(TEXT("箭矢使用无构造器默认 Trait 的普通 Mass 配置"), Arrow->GetClass() == UMassEntityConfigAsset::StaticClass());
	TestNull(TEXT("箭矢不继承含基类子弹 Trait 的配置"), Arrow->GetConfig().GetParent());
	TestNull(TEXT("箭矢没有装配精确类普通子弹 Trait"), Arrow->FindTrait(UCastleMassProjectileTrait::StaticClass(), true));
	const auto* Homing = Cast<UCastleMassHomingProjectileTrait>(Arrow->FindTrait(UCastleMassHomingProjectileTrait::StaticClass()));
	const auto* Visual = Cast<UMassMovableVisualizationTrait>(Arrow->FindTrait(UMassMovableVisualizationTrait::StaticClass()));
	if (!TestNotNull(TEXT("箭矢装配真正的子弹 Trait 子类"), Homing)
		|| !TestNotNull(TEXT("本地可见的实例化网格表示"), Visual)) { return false; }
	int32 UnitTraitCount = 0;
	for (const UMassEntityTraitBase* ConfiguredTrait : Arrow->GetConfig().GetTraits())
	{
		if (ConfiguredTrait && ConfiguredTrait->IsA<UCastleMassUnitTrait>())
		{
			++UnitTraitCount;
			TestTrue(TEXT("唯一 Unit Trait 是追踪子类"), ConfiguredTrait->GetClass() == UCastleMassHomingProjectileTrait::StaticClass());
		}
	}
	TestEqual(TEXT("资产本地只装配一个 Unit Trait"), UnitTraitCount, 1);
	TestNotNull(TEXT("静态网格显示所需的 LOD Collector 存在"), Arrow->FindTrait(UMassLODCollectorTrait::StaticClass()));
	TestTrue(TEXT("箭矢配置有正飞行速度、半径和有限寿命"), Homing->Movement.MaxSpeed > 0.0f && Homing->Radius > 0.0f && Homing->Lifetime > 0.0f);
	if (!TestTrue(TEXT("表示包含可见网格"), !Visual->StaticMeshInstanceDesc.Meshes.IsEmpty())) { return false; }
	TestNotNull(TEXT("配置的可见网格有效"), Visual->StaticMeshInstanceDesc.Meshes[0].Mesh.Get());
	FScopedWorld Scope;
	TArray<FCastleMassUnitHandle> Enemies, Projectiles;
	FText Reason;
	if (!TestTrue(TEXT("为真实资产创建命中目标"), Scope.Units().SpawnUnits(MakeConfig<UCastleMassEnemyTrait>(),
		{FTransform(FVector(1000, 0, 0))}, Enemies, Reason))) { AddError(Reason.ToString()); return false; }
	FCastleMassHomingProjectileSpawn Shot;
	Shot.Target = Enemies[0]; Shot.Damage = 4.0f;
	if (!TestTrue(TEXT("无父配置的真实资产成功生成"), Scope.Units().SpawnHomingProjectiles(Arrow, {Shot}, Projectiles, Reason))) { AddError(Reason.ToString()); return false; }
	TestTrue(TEXT("静态实例显示描述已注册"), Scope.Manager().GetFragmentDataChecked<FMassRepresentationFragment>(Projectiles[0].Entity).StaticMeshDescHandle.IsValid());
	TestTrue(TEXT("生成使用子类资产配置的速度"), FMath::IsNearlyEqual(Scope.Manager().GetFragmentDataChecked<FMassVelocityFragment>(Projectiles[0].Entity).Value.Size(), static_cast<double>(Homing->Movement.MaxSpeed)));
	Step<UCastleMassHomingProjectileProcessor>(Scope, 1.0f);
	TestEqual(TEXT("真实箭矢命中并使用逐发伤害"), Scope.Manager().GetFragmentDataChecked<FCastleMassUnitFragment>(Enemies[0].Entity).Health, 96.0f);
	Step<UCastleMassLifetimeProcessor>(Scope, 0.0f);
	TestFalse(TEXT("真实箭矢命中后进入死亡回收"), Scope.Units().IsUnitValid(Projectiles[0]));
	return true;
}

#endif
