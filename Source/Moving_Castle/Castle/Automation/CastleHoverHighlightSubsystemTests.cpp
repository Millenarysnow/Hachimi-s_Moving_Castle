#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

#include "Castle/Rendering/CastleHoverHighlightSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

/** 覆盖按位写入、精确恢复、目标切换及弱引用销毁后的状态清理。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCastleHoverHighlightStateTest,
	"MovingCastle.Rendering.HoverHighlightState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleHoverHighlightStateTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Settings = UWorld::InitializationValues()
		.AllowAudioPlayback(false)
		.CreatePhysicsScene(false)
		.CreateNavigation(false)
		.CreateAISystem(false)
		.ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(
		EWorldType::Game, false, TEXT("CastleHoverHighlightStateTest"), nullptr, true,
		ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建悬停测试世界"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	UCastleHoverHighlightSubsystem* Highlight = World->GetSubsystem<UCastleHoverHighlightSubsystem>();
	if (!TestNotNull(TEXT("游戏世界创建悬停高亮子系统"), Highlight))
	{
		return false;
	}

	AActor* Actor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("创建组件宿主 Actor"), Actor))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("HoverTestRoot"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();

	UStaticMeshComponent* First = NewObject<UStaticMeshComponent>(Actor, TEXT("HoverTestFirst"));
	Actor->AddInstanceComponent(First);
	First->SetupAttachment(Root);
	First->SetCustomDepthStencilValue(1);
	First->SetRenderCustomDepth(false);
	First->RegisterComponent();

	UStaticMeshComponent* Second = NewObject<UStaticMeshComponent>(Actor, TEXT("HoverTestSecond"));
	Actor->AddInstanceComponent(Second);
	Second->SetupAttachment(Root);
	Second->SetCustomDepthStencilValue(5);
	Second->SetRenderCustomDepth(true);
	Second->RegisterComponent();

	Highlight->SetHighlightedPrimitives({First, Second});
	TestTrue(TEXT("悬停开启第一组件 CustomDepth"), First->bRenderCustomDepth);
	TestEqual(TEXT("悬停仅增加 bit 1 并保留 bit 0"), First->CustomDepthStencilValue, 3);
	TestTrue(TEXT("原本开启的第二组件继续写 CustomDepth"), Second->bRenderCustomDepth);
	TestEqual(TEXT("第二组件保留其它 Stencil 位"), Second->CustomDepthStencilValue, 7);
	TestEqual(TEXT("查询返回两个高亮组件"), Highlight->GetHighlightedPrimitives().Num(), 2);

	Highlight->SetHighlightedPrimitives({Second, First});
	TestEqual(TEXT("同一集合改变顺序不重复写入状态"), First->CustomDepthStencilValue, 3);
	TestEqual(TEXT("同一集合仍保留完整成员"), Highlight->GetHighlightedPrimitives().Num(), 2);

	Highlight->SetHighlightedPrimitives({Second});
	TestFalse(TEXT("切换目标时立即恢复旧组件 CustomDepth"), First->bRenderCustomDepth);
	TestEqual(TEXT("切换目标时恢复旧组件 Stencil 原值"), First->CustomDepthStencilValue, 1);
	TestEqual(TEXT("新目标仍带悬停位"), Second->CustomDepthStencilValue, 7);

	Highlight->ClearHighlight();
	TestFalse(TEXT("清空时恢复关闭的 CustomDepth"), First->bRenderCustomDepth);
	TestEqual(TEXT("清空时恢复第一组件原始 Stencil"), First->CustomDepthStencilValue, 1);
	TestTrue(TEXT("清空时保留原本开启的 CustomDepth"), Second->bRenderCustomDepth);
	TestEqual(TEXT("清空时恢复第二组件原始 Stencil"), Second->CustomDepthStencilValue, 5);

	Highlight->SetHighlightedPrimitives({First});
	First->DestroyComponent();
	Highlight->ClearHighlight();
	TestTrue(TEXT("销毁组件后查询不返回已注销组件"), Highlight->GetHighlightedPrimitives().IsEmpty());
	return true;
}
#endif
