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
	TestEqual(TEXT("只为实际变化件创建副本"), CountVisuals(Result), Merge->MovedPartCount + Merge->IncomingPartCount + Merge->RemovedPartCount);
	TestTrue(TEXT("源隐藏且关闭碰撞"), First->IsHidden() && !First->GetActorEnableCollision());
	TestEqual(TEXT("结果碰撞在动画中仍然有效"), Originals[0]->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	Merge->TickComponent(0.2f, LEVELTICK_All, nullptr);
	TArray<UStaticMeshComponent*> AllMeshes;
	Result->GetComponents<UStaticMeshComponent>(AllMeshes);
	int32 VisibleCopies = 0;
	for (const UStaticMeshComponent* Mesh : AllMeshes)
	{
		VisibleCopies += Mesh->HasAnyFlags(RF_Transient) && Mesh->IsVisible() ? 1 : 0;
	}
	TestTrue(TEXT("拆件阶段同时呈现多个真实网格"), VisibleCopies >= 12);

	// 保持时间不变，只转动和移动所属 Actor，零件整条轨迹也应做同样的刚体变换。
	// 此断言覆盖俯仰与横滚，能发现错误地绕固定世界竖轴旋转的情况。
	TMap<UStaticMeshComponent*, FTransform> BeforeMove;
	for (UStaticMeshComponent* Mesh : AllMeshes)
	{
		if (Mesh->HasAnyFlags(RF_Transient) && Mesh->IsVisible())
		{
			BeforeMove.Add(Mesh, Mesh->GetComponentTransform().GetRelativeTransform(Result->GetActorTransform()));
		}
	}
	Result->SetActorTransform(FTransform(FRotator(17.0, 67.0, 9.0), FVector(1100.0, -300.0, 200.0)));
	Merge->TickComponent(0.0f, LEVELTICK_All, nullptr);
	for (const auto& Pair : BeforeMove)
	{
		TestTrue(TEXT("轨迹随 Actor 的俯仰横滚整体移动"), Pair.Key->GetComponentTransform()
			.Equals(Pair.Value * Result->GetActorTransform(), 0.001));
	}
	Merge->ResetMerge();
	TestFalse(TEXT("取消恢复旧建筑可见"), First->IsHidden());
	TestTrue(TEXT("取消恢复旧建筑碰撞"), First->GetActorEnableCollision());
	TestEqual(TEXT("取消清理全部副本"), CountVisuals(Result), 0);
	TestTrue(TEXT("取消后可以重播"), Merge->StartMerge({ First, Second }));

	// 棋盘会立即销毁源 Actor，后续动画必须完全依赖捕获的副本而不读取旧组件。
	First->Destroy();
	Second->Destroy();
	Result->SetActorTransform(FTransform(FRotator(0.0, 67.0, 0.0), FVector(1100.0, -300.0, 200.0)));
	Merge->TickComponent(0.45f, LEVELTICK_All, nullptr);
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

/** 同一波相邻变化中，不变的零件保持原位，远处变化等待传播，不各自打散建筑。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingMergeLocalWaveTest,
	"MovingCastle.Components.Animation.BuildingMerge.LocalWaveAndKeep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingMergeLocalWaveTest::RunTest(const FString& Parameters)
{
	using namespace BuildingMergeTests;
	FScopedWorld Scope;
	TArray<UBuildingMergeComponent*> Waves;
	TArray<UStaticMeshComponent*> MovingVisuals;
	for (int32 Row = 0; Row < 2; ++Row)
	{
		const double Y = Row * 700.0;
		AActor* Previous = MakeParts(Scope.World, FVector(0.0, Y, 0.0), 2);
		TArray<UStaticMeshComponent*> PreviousMeshes;
		Previous->GetComponents<UStaticMeshComponent>(PreviousMeshes);
		for (UStaticMeshComponent* Mesh : PreviousMeshes)
		{
			if (Mesh != Previous->GetRootComponent()) { Mesh->SetRelativeLocation(FVector(300.0, 0.0, 0.0)); }
		}
		AActor* Result = MakeParts(Scope.World, FVector(0.0, Y, 0.0), 2);
		TArray<UStaticMeshComponent*> OriginalMeshes;
		Result->GetComponents<UStaticMeshComponent>(OriginalMeshes);
		UStaticMeshComponent* Keep = CastChecked<UStaticMeshComponent>(Result->GetRootComponent());
		for (UStaticMeshComponent* Mesh : OriginalMeshes)
		{
			if (Mesh != Keep) { Mesh->SetRelativeLocation(FVector(380.0, 0.0, 0.0)); }
		}
		UBuildingMergeComponent* Merge = AddMerge(Result);
		Merge->bUseSharedChangeOrigin = true;
		Merge->ChangeOrigin = FVector::ZeroVector;
		Merge->PropagationSpeed = 1000.0;
		TestTrue(TEXT("单座邻接变化也可从共享原点启动"), Merge->StartChange({ Previous }));
		TestEqual(TEXT("一件始终原位保留"), Merge->UnchangedPartCount, 1);
		TestEqual(TEXT("只有一件发生短距离移动"), Merge->MovedPartCount, 1);
		TestEqual(TEXT("不为原位件创建飞行副本"), CountVisuals(Result), 1);
		TestTrue(TEXT("原位件从第一帧即可见"), Keep->IsVisible());
		TArray<UStaticMeshComponent*> Meshes;
		Result->GetComponents<UStaticMeshComponent>(Meshes);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (Mesh->HasAnyFlags(RF_Transient)) { MovingVisuals.Add(Mesh); }
		}
		Waves.Add(Merge);
	}
	if (!TestEqual(TEXT("两处都各有一个变化件"), MovingVisuals.Num(), 2)) { return false; }
	for (UBuildingMergeComponent* Wave : Waves) { Wave->TickComponent(0.5f, LEVELTICK_All, nullptr); }
	TestTrue(TEXT("近处零件已开始向新位置移动"), MovingVisuals[0]->Bounds.Origin.X > 300.1);
	TestTrue(TEXT("远处等待同一波传播，仍在原位置"), FMath::IsNearlyEqual(MovingVisuals[1]->Bounds.Origin.X, 300.0, 0.01));
	TestTrue(TEXT("移动没有绕到独立汇聚中心"), MovingVisuals[0]->Bounds.Origin.X < 380.0);
	for (UBuildingMergeComponent* Wave : Waves) { Wave->TickComponent(5.0f, LEVELTICK_All, nullptr); }
	TestFalse(TEXT("近处完成"), Waves[0]->bIsPlaying);
	TestFalse(TEXT("远处完成"), Waves[1]->bIsPlaying);
	return true;
}

/** 下一次操作发生在第一轮飞入前，仍须带走全部输入零件，不能只扫描当前可见网格。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingMergePendingHandoffTest,
	"MovingCastle.Components.Animation.BuildingMerge.PendingArrivalHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingMergePendingHandoffTest::RunTest(const FString& Parameters)
{
	using namespace BuildingMergeTests;
	FScopedWorld Scope;
	AActor* Incoming = MakeParts(Scope.World, FVector::ZeroVector, 2);
	AActor* Existing = MakeParts(Scope.World, FVector(400.0, 0.0, 0.0), 2);
	AActor* Intermediate = MakeParts(Scope.World, FVector::ZeroVector, 4);
	UBuildingMergeComponent* First = AddMerge(Intermediate);
	First->IncomingSourceActor = Incoming;
	First->bUseSharedChangeOrigin = true;
	First->ChangeOrigin = FVector(5000.0, 0.0, 0.0);
	First->PropagationSpeed = 1000.0;
	TestTrue(TEXT("第一轮已有四件对应"), First->StartMerge({Incoming, Existing}));
	TestEqual(TEXT("两件等待飞入"), First->IncomingPartCount, 2);
	First->TickComponent(0.1f, LEVELTICK_All, nullptr);
	AActor* Additional = MakeParts(Scope.World, FVector(1000.0, 0.0, 0.0), 1);
	AActor* Result = MakeParts(Scope.World, FVector::ZeroVector, 5);
	UBuildingMergeComponent* Second = AddMerge(Result);
	Second->bUseSharedChangeOrigin = true;
	Second->ChangeOrigin = First->ChangeOrigin;
	Second->PropagationSpeed = First->PropagationSpeed;
	TestTrue(TEXT("第一轮未飞入时也能接续变化"), Second->StartMerge({Intermediate, Additional}));
	TestEqual(TEXT("全部五件输入都被捕获，包含不可见等待件"), Second->SourcePartCount, 5);
	TestEqual(TEXT("五件均有对应而非凭空新增"), Second->ReusedPartCount, 5);
	TestEqual(TEXT("原有两件继续等待飞入"), Second->IncomingPartCount, 2);
	Intermediate->Destroy();
	Second->TickComponent(10.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("交接后五件全部归位"), Second->CompletedPartCount, 5);
	TestEqual(TEXT("交接完成清理副本"), CountVisuals(Result), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
