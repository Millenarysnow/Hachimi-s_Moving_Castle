#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingMergeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace BuildingMergeTests
{
	/** 每个用例独占一个有 WorldContext 的游戏世界，真实验证销毁而不污染编辑器当前关卡。 */
	struct FScopedWorld
	{
		UWorld* World = nullptr;
		FScopedWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}
		~FScopedWorld()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};

	/** 根网格与嵌套网格同时存在，验证动画不依赖 Actor 必须使用空场景根节点。 */
	AActor* MakeParts(UWorld* World, const FVector& Location, const int32 Count)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
			Mesh->SetStaticMesh(Cube);
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			if (Index == 0)
			{
				Actor->SetRootComponent(Mesh);
				Mesh->SetRelativeLocation(Location);
			}
			else
			{
				Mesh->SetupAttachment(Actor->GetRootComponent());
				Mesh->SetRelativeLocation(FVector((Index % 3) * 120.0, 0.0, Index * 70.0));
			}
			Actor->AddInstanceComponent(Mesh);
			Mesh->RegisterComponent();
		}
		return Actor;
	}

	/** 使用真实原生组件，只手动推进时间，不在测试中复制运动算法。 */
	UBuildingMergeComponent* AddMerge(AActor* Actor)
	{
		UBuildingMergeComponent* Merge = NewObject<UBuildingMergeComponent>(Actor);
		Merge->bShowStatusOnScreen = false;
		Actor->AddInstanceComponent(Merge);
		Merge->RegisterComponent();
		return Merge;
	}

	/** 副本网格都带 RF_Transient；静态模型资产本身不带此标记。 */
	int32 CountVisuals(AActor* Actor)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Actor->GetComponents<UStaticMeshComponent>(Meshes);
		return Meshes.FilterByPredicate([](const UStaticMeshComponent* Mesh)
		{
			return Mesh->HasAnyFlags(RF_Transient) && Mesh->IsRegistered();
		}).Num();
	}
}

/** 覆盖独立挂载、多个零件同时运动、源销毁、结果移动、取消和无临时组件残留。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingMergeLifecycleTest,
	"MovingCastle.Components.Animation.BuildingMerge.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingMergeLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace BuildingMergeTests;
	FScopedWorld Scope;
	AActor* First = MakeParts(Scope.World, FVector(-400.0, 0.0, 0.0), 12);
	AActor* Second = MakeParts(Scope.World, FVector(400.0, 0.0, 0.0), 12);
	AActor* Result = MakeParts(Scope.World, FVector::ZeroVector, 18);
	TArray<UStaticMeshComponent*> Originals;
	Result->GetComponents<UStaticMeshComponent>(Originals);
	TArray<FTransform> RelativeTransforms;
	for (UStaticMeshComponent* Mesh : Originals)
	{
		RelativeTransforms.Add(Mesh->GetRelativeTransform());
	}
	UBuildingMergeComponent* Merge = AddMerge(Result);
	TestTrue(TEXT("普通 Actor 挂载即能合并"), Merge->StartMerge({ First, Second }));
	TestEqual(TEXT("源零件数准确"), Merge->SourcePartCount, 24);
	TestEqual(TEXT("目标不重复抓取刚创建的源副本"), Merge->TargetPartCount, 18);
	TestEqual(TEXT("每件都有独立副本"), CountVisuals(Result), 42);
	TestTrue(TEXT("源隐藏且关闭碰撞"), First->IsHidden() && !First->GetActorEnableCollision());
	TestEqual(TEXT("结果碰撞在动画中仍然有效"), Originals[0]->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	Merge->TickComponent(0.8f, LEVELTICK_All, nullptr);
	TArray<UStaticMeshComponent*> AllMeshes;
	Result->GetComponents<UStaticMeshComponent>(AllMeshes);
	int32 VisibleCopies = 0;
	for (const UStaticMeshComponent* Mesh : AllMeshes)
	{
		VisibleCopies += Mesh->HasAnyFlags(RF_Transient) && Mesh->IsVisible() ? 1 : 0;
	}
	TestTrue(TEXT("拆件阶段同时呈现多个真实网格"), VisibleCopies >= 12);
	Merge->ResetMerge();
	TestFalse(TEXT("取消恢复旧建筑可见"), First->IsHidden());
	TestTrue(TEXT("取消恢复旧建筑碰撞"), First->GetActorEnableCollision());
	TestEqual(TEXT("取消清理全部副本"), CountVisuals(Result), 0);
	TestTrue(TEXT("取消后可以重播"), Merge->StartMerge({ First, Second }));

	// 棋盘会立即销毁源 Actor，后续动画必须完全依赖捕获的副本而不读取旧组件。
	First->Destroy();
	Second->Destroy();
	Result->SetActorTransform(FTransform(FRotator(0.0, 67.0, 0.0), FVector(1100.0, -300.0, 200.0)));
	Merge->TickComponent(1.8f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("源已销毁仍能重组"), Merge->bIsPlaying && CountVisuals(Result) > 0);
	Merge->TickComponent(10.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("大时间步也能正常结束"), Merge->bIsPlaying);
	TestFalse(TEXT("结束关闭 Tick"), Merge->IsComponentTickEnabled());
	TestEqual(TEXT("结果全部归位"), Merge->CompletedPartCount, 18);
	TestEqual(TEXT("结束没有临时组件"), CountVisuals(Result), 0);
	for (int32 Index = 0; Index < Originals.Num(); ++Index)
	{
		TestTrue(TEXT("结束恢复可见与碰撞"), Originals[Index]->IsVisible()
			&& Originals[Index]->GetCollisionEnabled() == ECollisionEnabled::QueryOnly);
		// 根组件由 Actor 移动而改变世界位置；比较其他零件的作者相对变换即可排除动画污染。
		if (Originals[Index] != Result->GetRootComponent())
		{
			TestTrue(TEXT("原模型局部变换未被改写"), Originals[Index]->GetRelativeTransform().Equals(RelativeTransforms[Index]));
		}
	}
	return true;
}

/** 验证失败的整轮不会半隐藏建筑；不支持的输入必须有明确错误。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingMergeValidationTest,
	"MovingCastle.Components.Animation.BuildingMerge.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingMergeValidationTest::RunTest(const FString& Parameters)
{
	using namespace BuildingMergeTests;
	FScopedWorld Scope;
	AActor* First = MakeParts(Scope.World, FVector(-200.0, 0.0, 0.0), 2);
	AActor* Second = MakeParts(Scope.World, FVector(200.0, 0.0, 0.0), 2);
	AActor* Result = MakeParts(Scope.World, FVector::ZeroVector, 4);
	UBuildingMergeComponent* Merge = AddMerge(Result);
	AddExpectedError(TEXT("零件合并失败"), EAutomationExpectedErrorFlags::Contains, 3);
	TestFalse(TEXT("拒绝重复源"), Merge->StartMerge({ First, First }));
	Merge->Duration = 0.0;
	TestFalse(TEXT("拒绝非法时长"), Merge->StartMerge({ First, Second }));
	Merge->Duration = 3.2;
	Second->FindComponentByClass<UStaticMeshComponent>()->SetStaticMesh(nullptr);
	TestFalse(TEXT("拒绝不完整模型"), Merge->StartMerge({ First, Second }));
	TestFalse(TEXT("错误原因可读取"), Merge->LastError.IsEmpty());
	TestFalse(TEXT("失败没有隐藏源"), First->IsHidden());
	TestTrue(TEXT("失败没有隐藏结果"), Result->FindComponentByClass<UStaticMeshComponent>()->IsVisible());
	TestEqual(TEXT("失败清理已创建的副本"), CountVisuals(Result), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
