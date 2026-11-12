#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleBuildings.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingEffectEntryTest, "MovingCastle.BuildingEffects.EntryAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBuildingEffectEntryTest::RunTest(const FString& Parameters)
{
	// 无渲染世界验证真实建筑默认子对象和原生接口，避免只测一个脱离建筑的计数函数。
	const UWorld::InitializationValues Settings = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	ACastleArcherTowerLevel1* Tower = World->SpawnActor<ACastleArcherTowerLevel1>();
	ACastleRoadBuilding* Road = World->SpawnActor<ACastleRoadBuilding>();
	TestNotNull(TEXT("箭塔自带效果组件"), Tower->BuildingEffect.Get());
	TestNotNull(TEXT("道路同样自带效果组件"), Road->BuildingEffect.Get());
	ICastleBuildingTriggerInterface* Entry = Cast<ICastleBuildingTriggerInterface>(Tower);
	if (TestNotNull(TEXT("建筑实现共同原生接口"), Entry))
	{
		for (int32 Index = 0; Index < 1000; ++Index)
		{
			if (!Entry->TriggerBuildingEffect(FCastleBuildingTriggerContext())) { AddError(TEXT("同帧高频触发被拒绝。")); break; }
		}
		TestEqual(TEXT("同帧千次调用没有冷却或吞事件"), Tower->BuildingEffect->TriggerCount, int64(1000));
		TestEqual(TEXT("同类体系其他建筑的实例状态不受影响"), Road->BuildingEffect->TriggerCount, int64(0));
		Tower->SetPlacementComplete(false);
		// 两次拒绝复用同一条错误文本，统一声明次数，避免期望日志相互抢占匹配。
		AddExpectedMessagePlain(TEXT("建筑效果拒绝无效、预览或尚未完成建造的目标"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 2);
		TestFalse(TEXT("建造中拒绝效果"), Entry->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
		Tower->SetPlacementComplete(true);
		Tower->PrepareForPreview();
		TestFalse(TEXT("卡片预览不接收效果"), Entry->TriggerBuildingEffect(FCastleBuildingTriggerContext()));
		TestEqual(TEXT("被拒绝的调用不污染成功次数"), Tower->BuildingEffect->TriggerCount, int64(1000));
	}
	World->DestroyWorld(false);
	return true;
}

#endif
