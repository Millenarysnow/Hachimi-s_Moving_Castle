#if WITH_DEV_AUTOMATION_TESTS

#include "AssemblyFlightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace AssemblyFlightTests
{
	/** 每个用例使用独立游戏世界，不改变用户当前打开的关卡，也不依赖正式游戏 GameMode。 */
	struct FScopedWorld
	{
		UWorld* World = nullptr;

		FScopedWorld()
		{
			// CreateWorld 会自行调用 InitializeNewWorld；通过 InIVS 一次性传入配置，不能再次初始化。
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
				true, ERHIFeatureLevel::Num, &Values);
		}

		~FScopedWorld()
		{
			World->DestroyWorld(false);
		}
	};

	/** 在测试 Actor 上建立真实可注册的网格组件，可让根网格和子网格同时参与动画。 */
	UStaticMeshComponent* AddPart(AActor* Owner, UStaticMesh* Mesh, const FName Name,
		USceneComponent* Parent, const FVector& Location)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner, Name);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		if (Parent)
		{
			Part->SetupAttachment(Parent);
		}
		else
		{
			Owner->SetRootComponent(Part);
		}
		Part->SetRelativeLocation(Location);
		Owner->AddInstanceComponent(Part);
		Part->RegisterComponent();
		return Part;
	}

	/** 组件默认参数与用户看到的效果一致，测试只关闭屏幕提示并手动推进时间。 */
	UAssemblyFlightComponent* AddFlight(AActor* Owner)
	{
		UAssemblyFlightComponent* Flight = NewObject<UAssemblyFlightComponent>(Owner);
		Flight->bAutoPlay = false;
		Flight->bShowStatusOnScreen = false;
		Owner->AddInstanceComponent(Flight);
		Flight->RegisterComponent();
		return Flight;
	}

	/** 只统计临时显示件，不把原模型自身的可见网格计为在途零件。 */
	int32 CountFlightVisuals(AActor* Owner, const TSet<UStaticMeshComponent*>& Originals)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Owner->GetComponents<UStaticMeshComponent>(Meshes, false);
		int32 Count = 0;
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (!Originals.Contains(Mesh) && Mesh->IsRegistered() && Mesh->IsVisible())
			{
				++Count;
			}
		}
		return Count;
	}
}

/** 验证重叠飞行、网格根组件、嵌套层级、所属 Actor 移动和取消恢复，不用自造算法镜像断言。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyFlightComponentLifecycleTest,
	"MovingCastle.Tests.AssemblyFlight.ComponentLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssemblyFlightComponentLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace AssemblyFlightTests;
	FScopedWorld Scope;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("引擎方块应能加载"), Cube))
	{
		return false;
	}
	AActor* Owner = Scope.World->SpawnActor<AActor>();
	TArray<UStaticMeshComponent*> Parts;
	Parts.Add(AddPart(Owner, Cube, TEXT("BottomRoot"), nullptr, FVector::ZeroVector));
	for (int32 Index = 1; Index < 24; ++Index)
	{
		Parts.Add(AddPart(Owner, Cube, *FString::Printf(TEXT("Part_%02d"), Index), Parts[0],
			FVector((Index % 3) * 110.0, 0.0, Index * 70.0)));
	}
	TArray<FTransform> OriginalRelative;
	TSet<UStaticMeshComponent*> OriginalSet;
	for (UStaticMeshComponent* Part : Parts)
	{
		// 根网格的 RelativeTransform 本身就是 Actor 世界变换，因此统一比较相对 Actor 的变换。
		OriginalRelative.Add(Part->GetComponentTransform().GetRelativeTransform(Owner->GetActorTransform()));
		OriginalSet.Add(Part);
	}

	UAssemblyFlightComponent* Flight = AddFlight(Owner);
	TestTrue(TEXT("挂在普通 Actor 上即能启动"), Flight->StartAssembly());
	TestEqual(TEXT("应收集全部 24 个网格"), Flight->TotalPartCount, 24);
	TestTrue(TEXT("所属 Actor 的根仍是原底部网格"), Owner->GetRootComponent() == Parts[0]);
	Flight->TickComponent(1.8f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("同一帧必须有多件在途"), Flight->FlyingPartCount >= 9);
	TestEqual(TEXT("临时显示件数量应与并发计数一致"), CountFlightVisuals(Owner, OriginalSet), Flight->FlyingPartCount);
	TestTrue(TEXT("最底部尚在飞时原件仍隐藏"), !Parts[0]->IsVisible());

	// 飞行期间整体移动和旋转，原件相对坐标和父子关系必须不变。
	Owner->SetActorTransform(FTransform(FRotator(0.0, 40.0, 0.0), FVector(800.0, -300.0, 100.0)));
	Flight->TickComponent(0.3f, LEVELTICK_All, nullptr);
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		TestTrue(TEXT("飞行不能移动原件的相对变换"), Parts[Index]->GetComponentTransform()
			.GetRelativeTransform(Owner->GetActorTransform()).Equals(OriginalRelative[Index], 0.0001));
	}
	Flight->ResetAssembly();
	TestEqual(TEXT("取消后必须清理全部临时件"), CountFlightVisuals(Owner, OriginalSet), 0);
	for (UStaticMeshComponent* Part : Parts)
	{
		TestTrue(TEXT("取消应恢复原件可见"), Part->IsVisible());
		TestEqual(TEXT("取消应恢复每件碰撞"), Part->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	}

	TestTrue(TEXT("取消后可以重播"), Flight->StartAssembly());
	Flight->TickComponent(2.3f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("底部网格应比顶部先落定"), Parts[0]->IsVisible() && !Parts.Last()->IsVisible());
	// 一帧直接跨过剩余发射时间，仍应全部完成，不能因漏帧丢件。
	Flight->TickComponent(20.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("全部零件应完成"), Flight->CompletedPartCount, 24);
	TestFalse(TEXT("完成后不应继续播放"), Flight->bIsPlaying);
	TestFalse(TEXT("完成后应关闭组件 Tick"), Flight->IsComponentTickEnabled());
	TestEqual(TEXT("完成后临时显示组件应为零"), CountFlightVisuals(Owner, OriginalSet), 0);
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		TestTrue(TEXT("结束相对变换应精确保留"), Parts[Index]->GetComponentTransform()
			.GetRelativeTransform(Owner->GetActorTransform()).Equals(OriginalRelative[Index], 0.0001));
	}

	// 验证参数错误明确拒绝且不隐藏原模型。
	Flight->LaunchInterval = 0.0;
	AddExpectedError(TEXT("连续飞入失败"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("零发射间隔必须拒绝"), Flight->StartAssembly());
	TestTrue(TEXT("失败应保留原模型可见"), Parts[0]->IsVisible());
	TestFalse(TEXT("失败原因必须可读取"), Flight->LastError.IsEmpty());
	return true;
}

/** 用真实拆件房屋验证资产独立性，组件不得引用旧特效蓝图才能工作。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssemblyFlightRealHouseTest,
	"MovingCastle.Tests.AssemblyFlight.ImportedHouse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssemblyFlightRealHouseTest::RunTest(const FString& Parameters)
{
	using namespace AssemblyFlightTests;
	FScopedWorld Scope;
	UClass* HouseClass = LoadClass<AActor>(nullptr, TEXT("/Game/test/BP_SketchUpHouse_Assembly.BP_SketchUpHouse_Assembly_C"));
	if (!TestNotNull(TEXT("原始拆件房屋蓝图应存在"), HouseClass))
	{
		return false;
	}
	AActor* House = Scope.World->SpawnActor<AActor>(HouseClass);
	TArray<UStaticMeshComponent*> Meshes;
	House->GetComponents<UStaticMeshComponent>(Meshes, false);
	TArray<FTransform> OriginalTransforms;
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		OriginalTransforms.Add(Mesh->GetComponentTransform());
	}
	UAssemblyFlightComponent* Flight = AddFlight(House);
	TestTrue(TEXT("真实房屋可直接挂载组件播放"), Flight->StartAssembly());
	TestEqual(TEXT("应收集房屋全部 123 件"), Flight->TotalPartCount, 123);
	Flight->TickComponent(4.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("默认参数应有约 15 件同时飞行"), Flight->FlyingPartCount >= 14 && Flight->FlyingPartCount <= 16);
	TestTrue(TEXT("第四秒应已有部分零件落定"), Flight->CompletedPartCount > 0 && Flight->CompletedPartCount < 123);
	Flight->TickComponent(8.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("约 12 秒应全部完成"), Flight->CompletedPartCount, 123);
	for (int32 Index = 0; Index < Meshes.Num(); ++Index)
	{
		TestTrue(TEXT("房屋每件的世界变换应与原位一致"), Meshes[Index]->GetComponentTransform().Equals(OriginalTransforms[Index], 0.0001));
		TestTrue(TEXT("完成后原件应可见"), Meshes[Index]->IsVisible());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
