// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Actors/CastleBuildings.h"
#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Components/Building/Effects/Attack/CastleTowerAttackEffectComponent.h"
#include "Components/Building/Effects/CastleBuildingTriggerInterface.h"
#include "Units/Mass/CastleMassTraits.h"
#include "Units/Mass/CastleMassUnitSubsystem.h"
#include "Units/Mass/Projectiles/CastleMassHomingProjectileProcessor.h"
#include "Units/Mass/Projectiles/CastleMassHomingProjectileTrait.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace CastleTowerAttackTests
{
	/** 显式运行一次原生 Mass 处理器，验证追踪子弹实际命中和结算生命。 */
	template<typename TProcessor>
	void StepProcessor(UWorld* World, const float DeltaTime)
	{
		FMassEntityManager& Manager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
		Manager.FlushCommands();
		TProcessor* Processor = NewObject<TProcessor>(World);
		Processor->CallInitialize(World, Manager.AsShared());
		UE::Mass::FProcessingContext Context(Manager, DeltaTime);
		UE::Mass::Executor::Run(*Processor, Context);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleTowerAttackQueueTest,
	"MovingCastle.BuildingEffects.TowerAttackQueueAndRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleTowerAttackQueueTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Settings = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建塔攻击测试 World"), World)) { return false; }
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

	ACastleArcherTowerLevel1* Tower = World->SpawnActor<ACastleArcherTowerLevel1>();
	if (!TestNotNull(TEXT("生成一级箭塔"), Tower)) { return false; }
	UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	Definition->AttackData->Levels[0].AttacksPerSecond = 2.0f;
	FCastleBuildingDescriptor Descriptor;
	FText FailureReason;
	if (!TestTrue(TEXT("创建塔建筑描述"), Definition->CreateDescriptor(1, ECastleTileColor::Red, 1, Descriptor, FailureReason))
		|| !TestTrue(TEXT("初始化箭塔的配置身份"), Tower->InitializeBuilding(Descriptor, { FIntVector::ZeroValue })))
	{
		AddError(FailureReason.ToString());
		return false;
	}
	UCastleTowerAttackEffectComponent* Attack = Cast<UCastleTowerAttackEffectComponent>(Tower->BuildingEffect);
	if (!TestNotNull(TEXT("箭塔现有 BuildingEffect 是塔攻击组件"), Attack)) { return false; }

	const FCastleBuildingTriggerContext Context;
	TestTrue(TEXT("第一次触发进入待射队列"), Tower->TriggerBuildingEffect(Context));
	TestTrue(TEXT("间隔未到时第二次触发仍进入待射队列"), Tower->TriggerBuildingEffect(Context));
	TestEqual(TEXT("两次触发都各自累计一发"), Attack->PendingShotCount, int64(2));
	TestTrue(TEXT("待射队列打开组件 Tick"), Attack->IsComponentTickEnabled());

	AddExpectedMessagePlain(TEXT("本次待射作废：攻击半径内没有存活敌人"),
		ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	Attack->TickComponent(0.49f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("少于半秒不消耗第一发"), Attack->PendingShotCount, int64(2));
	Attack->TickComponent(0.02f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("达到每秒两发的首个半秒后只消耗一发"), Attack->PendingShotCount, int64(1));
	Attack->TickComponent(0.48f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("第二发仍等待自己的射速间隔"), Attack->PendingShotCount, int64(1));
	Attack->TickComponent(0.02f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("第二个半秒到期后消耗第二发"), Attack->PendingShotCount, int64(0));
	TestFalse(TEXT("队列耗尽后关闭组件 Tick"), Attack->IsComponentTickEnabled());
	TestEqual(TEXT("所有触发仍保留在建筑触发计数中"), Tower->BuildingEffect->TriggerCount, int64(2));

	// 同一份攻击数据显式引用的追踪配置用于真实 Mass 生成，不替换生产参数或默认对象。
	UMassEntityConfigAsset* EnemyConfiguration = NewObject<UMassEntityConfigAsset>(World);
	UCastleMassEnemyTrait* EnemyTrait = NewObject<UCastleMassEnemyTrait>(EnemyConfiguration);
	EnemyTrait->MaxHealth = 10.0f;
	EnemyConfiguration->GetMutableConfig().AddTrait(*EnemyTrait);
	TArray<FCastleMassUnitHandle> Enemies;
	if (!TestTrue(TEXT("生成一名十点生命的 Mass 敌人"),
		World->GetSubsystem<UCastleMassUnitSubsystem>()->SpawnUnits(EnemyConfiguration,
			{ FTransform(FVector::ZeroVector) }, Enemies, FailureReason)))
	{
		AddError(FailureReason.ToString());
		return false;
	}
	TestTrue(TEXT("敌人确实位于箭塔发射点"), World->GetSubsystem<UCastleMassUnitSubsystem>()->IsUnitValid(Enemies[0]));
	TestTrue(TEXT("有敌人时触发仍增加一发"), Tower->TriggerBuildingEffect(Context));
	Attack->TickComponent(0.5f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("成功生成一发后只消耗对应待射项"), Attack->PendingShotCount, int64(0));
	CastleTowerAttackTests::StepProcessor<UCastleMassHomingProjectileProcessor>(World, 0.0f);
	FCastleMassUnitSnapshot EnemySnapshot;
	TestTrue(TEXT("读取被追踪命中的敌人状态"),
		World->GetSubsystem<UCastleMassUnitSubsystem>()->GetUnitSnapshot(Enemies[0], EnemySnapshot));
	TestEqual(TEXT("追踪子弹根据 Data Asset 伤害使敌人生命归零"), EnemySnapshot.Health, 0.0f);

	// 轻量测试 World 不自动派发关卡 teardown；显式启动组件后直接驱动其 Destroy EndPlay 回调。
	Tower->DispatchBeginPlay();
	TestTrue(TEXT("销毁前 Actor 与组件已进入 BeginPlay"), Attack->HasBegunPlay());
	TestTrue(TEXT("销毁前再次触发以验证实例清理"), Tower->TriggerBuildingEffect(Context));
	Attack->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("销毁前待射状态存在"), Attack->PendingShotCount, int64(1));
	Tower->Destroy();
	Attack->EndPlay(EEndPlayReason::Destroyed);
	TestEqual(TEXT("建筑销毁时清空待射状态"), Attack->PendingShotCount, int64(0));
	return true;
}

#endif
