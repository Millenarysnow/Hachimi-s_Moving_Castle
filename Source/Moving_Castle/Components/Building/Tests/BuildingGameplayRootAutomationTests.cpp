#if WITH_DEV_AUTOMATION_TESTS

#include "Components/Building/BuildingGameplayRootComponent.h"

#include "AssemblyFlightComponent.h"
#include "BuildingNavigationActor.h"
#include "BuildingMergeComponent.h"
#include "CastleBuildingTestUtilities.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NavModifierComponent.h"

namespace BuildingGameplayTests
{
	/** 使用独立游戏世界运行真实组件，避免修改用户关卡或依赖编辑器当前选择。 */
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

	/** 在现有 Actor 上建立可附着、可注册的组件，不使用测试专用蓝图或修改正式资源。 */
	template<typename T>
	T* AddComponent(AActor* Owner, const FName Name, USceneComponent* Parent)
	{
		T* Component = NewObject<T>(Owner, Name);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCanEverAffectNavigation(false);
		if (Parent) { Component->SetupAttachment(Parent); }
		else { Owner->SetRootComponent(Component); }
		Owner->AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}

	/** 建立一个真实美术零件，普通 Actor 的两种动画也应遵循相同的功能分支边界。 */
	UStaticMeshComponent* AddVisual(AActor* Owner, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Visual = AddComponent<UStaticMeshComponent>(Owner, TEXT("VisualPart"), Owner->GetRootComponent());
		Visual->SetStaticMesh(Mesh);
		Visual->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Visual->ComponentTags.Add(TEXT("AssemblyPart"));
		return Visual;
	}

	/** 保存作者状态，分别在起播、推进、取消和完成后检查，覆盖动画生命周期中的误修改。 */
	struct FPrimitiveSnapshot
	{
		UPrimitiveComponent* Component;
		FTransform RelativeTransform;
		FCollisionResponseContainer Responses;
		ECollisionEnabled::Type Collision;
		ECollisionChannel ObjectType;
		bool bVisible;
		bool bHidden;
		bool bNavigation;
		bool bOverlap;

		explicit FPrimitiveSnapshot(UPrimitiveComponent* InComponent)
			: Component(InComponent), RelativeTransform(Component->GetRelativeTransform()),
			Responses(Component->GetCollisionResponseToChannels()), Collision(Component->GetCollisionEnabled()),
			ObjectType(Component->GetCollisionObjectType()), bVisible(Component->IsVisible()),
			bHidden(Component->bHiddenInGame), bNavigation(Component->CanEverAffectNavigation()),
			bOverlap(Component->GetGenerateOverlapEvents())
		{
		}

		void Verify(FAutomationTestBase& Test) const
		{
			const FString Name = Component->GetName();
			Test.TestTrue(Name + TEXT(" 保留局部变换"), Component->GetRelativeTransform().Equals(RelativeTransform));
			Test.TestEqual(Name + TEXT(" 保留可见性"), Component->IsVisible(), bVisible);
			Test.TestEqual(Name + TEXT(" 保留游戏隐藏设置"), Component->bHiddenInGame != 0, bHidden);
			Test.TestEqual(Name + TEXT(" 保留碰撞开关"), Component->GetCollisionEnabled(), Collision);
			Test.TestTrue(Name + TEXT(" 保留各通道响应"), Component->GetCollisionResponseToChannels() == Responses);
			Test.TestEqual(Name + TEXT(" 保留碰撞对象类型"), Component->GetCollisionObjectType(), ObjectType);
			Test.TestEqual(Name + TEXT(" 保留导航设置"), Component->CanEverAffectNavigation(), bNavigation);
			Test.TestEqual(Name + TEXT(" 保留重叠事件"), Component->GetGenerateOverlapEvents(), bOverlap);
		}
	};

	/** 同时包含直接子组件、多级分组、隐藏网格、触发盒和未配置网格，过滤必须先于零件校验。 */
	struct FGameplayBranch
	{
		UBuildingGameplayRootComponent* Root;
		UStaticMeshComponent* Nested;
		TArray<FPrimitiveSnapshot> Snapshots;

		FGameplayBranch(AActor* Owner, UStaticMesh* Mesh, UBuildingGameplayRootComponent* ExistingRoot = nullptr)
		{
			Root = ExistingRoot ? ExistingRoot
				: AddComponent<UBuildingGameplayRootComponent>(Owner, TEXT("GameplayRoot"), Owner->GetRootComponent());
			USceneComponent* FloorGroup = AddComponent<USceneComponent>(Owner, TEXT("FloorGroup"), Root);
			USceneComponent* InnerGroup = AddComponent<USceneComponent>(Owner, TEXT("InnerGroup"), FloorGroup);
			UStaticMeshComponent* Direct = AddComponent<UStaticMeshComponent>(Owner, TEXT("DirectFloor"), Root);
			Nested = AddComponent<UStaticMeshComponent>(Owner, TEXT("NestedFloor"), InnerGroup);
			UStaticMeshComponent* Hidden = AddComponent<UStaticMeshComponent>(Owner, TEXT("HiddenFloor"), InnerGroup);
			for (UStaticMeshComponent* Part : { Direct, Nested, Hidden })
			{
				Part->SetStaticMesh(Mesh);
				Part->SetRelativeLocation(FVector(6000.0, 0.0, 3000.0));
				Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Part->SetCollisionObjectType(ECC_WorldStatic);
				Part->SetCollisionResponseToAllChannels(ECR_Ignore);
				Part->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
				Part->SetGenerateOverlapEvents(false);
				Part->SetCanEverAffectNavigation(true);
				// 参与标签不能越过功能分支；可见的功能网格也必须被排除。
				Part->ComponentTags.Add(TEXT("AssemblyPart"));
			}
			Hidden->SetHiddenInGame(true, false);
			Direct->SetVisibility(false, false);
			UBoxComponent* Trigger = AddComponent<UBoxComponent>(Owner, TEXT("GameplayTrigger"), InnerGroup);
			Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Trigger->SetCollisionResponseToAllChannels(ECR_Overlap);
			Trigger->SetGenerateOverlapEvents(true);
			UStaticMeshComponent* Unconfigured = AddComponent<UStaticMeshComponent>(Owner, TEXT("UnconfiguredHelper"), InnerGroup);
			Unconfigured->ComponentTags.Add(TEXT("AssemblyPart"));
			for (UPrimitiveComponent* Part : TArray<UPrimitiveComponent*>{ Direct, Nested, Hidden, Trigger, Unconfigured })
			{
				Snapshots.Emplace(Part);
			}
		}

		void Verify(FAutomationTestBase& Test) const
		{
			for (const FPrimitiveSnapshot& Snapshot : Snapshots) { Snapshot.Verify(Test); }
		}
	};

	/** 单件普通 Actor 用于隔离动画规则，不要求依赖 CastleBuildingBase 才能排除功能组件。 */
	AActor* MakeActor(UWorld* World, UStaticMesh* Mesh, const FVector& Location)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		AddComponent<USceneComponent>(Actor, TEXT("SceneRoot"), nullptr);
		Actor->SetActorLocation(Location);
		AddVisual(Actor, Mesh);
		return Actor;
	}
}

/** 真实飞入只接管美术件；取消、完成与重新挂接均不依赖逐件标签或特定父级深度。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingGameplayAssemblyTest,
	"MovingCastle.Components.Building.GameplayRoot.Assembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingGameplayAssemblyTest::RunTest(const FString& Parameters)
{
	using namespace BuildingGameplayTests;
	FScopedWorld Scope;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("加载引擎方块"), Cube)) { return false; }
	AActor* Actor = MakeActor(Scope.World, Cube, FVector::ZeroVector);
	FGameplayBranch Branch(Actor, Cube);
	// 根节点可以重命名，识别必须由原生类型决定，不能依赖 GameplayRoot 字符串。
	Branch.Root->Rename(TEXT("RenamedGameplayGroup"));
	UAssemblyFlightComponent* Flight = NewObject<UAssemblyFlightComponent>(Actor);
	Flight->bAutoPlay = false;
	Flight->bShowStatusOnScreen = false;
	Flight->PartComponentTag = TEXT("AssemblyPart");
	Actor->AddInstanceComponent(Flight);
	Flight->RegisterComponent();
	if (!TestTrue(TEXT("功能分支含空网格仍能正常飞入"), Flight->StartAssembly())) { return false; }
	TestEqual(TEXT("只收集功能分支外的一件美术网格"), Flight->TotalPartCount, 1);
	Branch.Verify(*this);
	Flight->TickComponent(1.1f, LEVELTICK_All, nullptr);
	Branch.Verify(*this);
	Flight->ResetAssembly();
	Branch.Verify(*this);
	TestTrue(TEXT("取消后可重播"), Flight->StartAssembly());
	Flight->TickComponent(10.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("仅完成一件美术零件"), Flight->CompletedPartCount, 1);
	Branch.Verify(*this);

	// 从功能分支移出的网格恢复普通参与资格，不能缓存一次检测结果或给后代永久打排除标签。
	Branch.Nested->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	TestTrue(TEXT("重新挂接后可启动"), Flight->StartAssembly());
	TestEqual(TEXT("移出功能分支的可见网格重新参与"), Flight->TotalPartCount, 2);
	Flight->ResetAssembly();
	return true;
}

/** 同时验证合并来源和结果的过滤，以及结果功能组件不被批量显隐接管。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingGameplayMergeTest,
	"MovingCastle.Components.Building.GameplayRoot.Merge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingGameplayMergeTest::RunTest(const FString& Parameters)
{
	using namespace BuildingGameplayTests;
	FScopedWorld Scope;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("加载引擎方块"), Cube)) { return false; }
	AActor* First = MakeActor(Scope.World, Cube, FVector(-300.0, 0.0, 0.0));
	AActor* Second = MakeActor(Scope.World, Cube, FVector(300.0, 0.0, 0.0));
	AActor* Result = MakeActor(Scope.World, Cube, FVector::ZeroVector);
	FGameplayBranch FirstBranch(First, Cube);
	FGameplayBranch SecondBranch(Second, Cube);
	FGameplayBranch ResultBranch(Result, Cube);
	UBuildingMergeComponent* Merge = NewObject<UBuildingMergeComponent>(Result);
	Merge->bShowStatusOnScreen = false;
	Result->AddInstanceComponent(Merge);
	Merge->RegisterComponent();
	if (!TestTrue(TEXT("新旧建筑功能分支不阻止合并"), Merge->StartMerge({ First, Second }))) { return false; }
	TestEqual(TEXT("来源只收集两件美术网格"), Merge->SourcePartCount, 2);
	TestEqual(TEXT("结果只收集一件美术网格"), Merge->TargetPartCount, 1);
	TestTrue(TEXT("合并动画不隐藏来源 Actor 或关闭 Actor 总碰撞"), !First->IsHidden() && First->GetActorEnableCollision()
		&& !Second->IsHidden() && Second->GetActorEnableCollision());
	ResultBranch.Verify(*this);
	FirstBranch.Verify(*this);
	SecondBranch.Verify(*this);
	Merge->TickComponent(0.2f, LEVELTICK_All, nullptr);
	ResultBranch.Verify(*this);
	FirstBranch.Verify(*this);
	SecondBranch.Verify(*this);
	Merge->ResetMerge();
	FirstBranch.Verify(*this);
	SecondBranch.Verify(*this);
	ResultBranch.Verify(*this);
	TestTrue(TEXT("取消恢复旧 Actor 的碰撞总开关"), First->GetActorEnableCollision() && Second->GetActorEnableCollision());
	TestTrue(TEXT("取消后可重新合并"), Merge->StartMerge({ First, Second }));
	Merge->TickComponent(10.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("正常合并完成"), Merge->bIsPlaying);
	ResultBranch.Verify(*this);
	FirstBranch.Verify(*this);
	SecondBranch.Verify(*this);
	return true;
}

/** 功能几何不能被误认成模型、扩大展示范围，也不能被建筑初始化覆盖碰撞与导航。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingGameplayConfigurationTest,
	"MovingCastle.Components.Building.GameplayRoot.BuildingConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingGameplayConfigurationTest::RunTest(const FString& Parameters)
{
	using namespace BuildingGameplayTests;
	FScopedWorld Scope;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("加载引擎方块"), Cube)) { return false; }
	ACastleBuildingBase* Building = Scope.World->SpawnActor<ACastleArcherTowerLevel1>();
	if (!TestNotNull(TEXT("建筑原生自带功能根节点"), Building->GameplayRoot.Get())) { return false; }
	FGameplayBranch Branch(Building, Cube, Building->GameplayRoot);
	UCastleBuildingDefinition* Definition = CastleBuildingTests::MakeDefinition(ECastleBuildingType::ArcherTower);
	FCastleBuildingDescriptor Descriptor;
	FText Reason;
	if (!TestTrue(TEXT("创建一级建筑描述"), Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Descriptor, Reason))) { return false; }
	TestTrue(TEXT("初始化建筑占位"), Building->InitializeBuilding(Descriptor, { FIntVector::ZeroValue }));
	if (!TestTrue(TEXT("配置仅有功能几何的建筑"), Building->ConfigureBuildingVisual(700.0f, 500.0f, Reason))) { return false; }
	TestTrue(TEXT("功能网格不冒充正式美术模型"), Building->IsUsingPlaceholder());
	TestTrue(TEXT("远处功能网格不扩大展示包围盒"), Building->GetVisualBounds().Max.X < 1000.0);
	Branch.Verify(*this);

	// 再增加一个真正的模型，反复初始化仍应保留全部功能设置，并只按模型计算展示范围。
	UStaticMeshComponent* Visual = AddVisual(Building, Cube);
	if (!TestTrue(TEXT("可重新配置正式美术模型"), Building->ConfigureBuildingVisual(700.0f, 500.0f, Reason))) { return false; }
	TestFalse(TEXT("功能分支外的模型被正常识别"), Building->IsUsingPlaceholder());
	const FBox VisualBounds = Building->GetVisualBounds();
	const FBox MeshBounds = Visual->Bounds.GetBox();
	TestTrue(TEXT("展示范围只包含正式美术模型"), VisualBounds.Min.Equals(MeshBounds.Min) && VisualBounds.Max.Equals(MeshBounds.Max));
	Branch.Verify(*this);
	Building->PrepareForPreview();
	for (const FPrimitiveSnapshot& Snapshot : Branch.Snapshots)
	{
		TestFalse(TEXT("卡片预览不显示功能几何"), Snapshot.Component->IsVisible());
		TestEqual(TEXT("预览保留功能组件碰撞"), Snapshot.Component->GetCollisionEnabled(), Snapshot.Collision);
		TestEqual(TEXT("预览保留功能组件导航设置"), Snapshot.Component->CanEverAffectNavigation(), Snapshot.bNavigation);
	}
	TestTrue(TEXT("预览不关闭 Actor 总碰撞"), Building->GetActorEnableCollision());
	return true;
}

/** 验证所有建筑族共用引擎导航过滤，而非依赖棋盘初始化或美术网格的碰撞预设。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingGameplayNavigationBoundaryTest,
	"MovingCastle.Components.Building.GameplayRoot.NavigationBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingGameplayNavigationBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace BuildingGameplayTests;
	FScopedWorld Scope;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("加载引擎方块"), Cube)) { return false; }

	// 主塔使用无业务状态的建筑模型基类；道路和三个功能建筑族的所有等级均须继承同一边界。
	const TArray<UClass*> Classes = {
		ABuildingNavigationActor::StaticClass(), ACastleRoadBuilding::StaticClass(),
		ACastleArcherTowerLevel1::StaticClass(), ACastleArcherTowerLevel2::StaticClass(),
		ACastleArcherTowerLevel3::StaticClass(), ACastleArcherTowerLevel4::StaticClass(), ACastleArcherTowerLevel5::StaticClass(),
		ACastleCannonTowerLevel1::StaticClass(), ACastleCannonTowerLevel2::StaticClass(),
		ACastleCannonTowerLevel3::StaticClass(), ACastleCannonTowerLevel4::StaticClass(), ACastleCannonTowerLevel5::StaticClass(),
		ACastleFarmLevel1::StaticClass(), ACastleFarmLevel2::StaticClass(), ACastleFarmLevel3::StaticClass(),
		ACastleFarmLevel4::StaticClass(), ACastleFarmLevel5::StaticClass()
	};
	for (UClass* Class : Classes)
	{
		ABuildingNavigationActor* Building = Scope.World->SpawnActor<ABuildingNavigationActor>(Class);
		if (!TestNotNull(Class->GetName() + TEXT(" 可生成"), Building)) { return false; }
		if (!Building->GetRootComponent()) { AddComponent<USceneComponent>(Building, TEXT("ModelRoot"), nullptr); }
		ACastleBuildingBase* PlacedBuilding = Cast<ACastleBuildingBase>(Building);
		FGameplayBranch Branch(Building, Cube, PlacedBuilding ? PlacedBuilding->GameplayRoot.Get() : nullptr);
		UStaticMeshComponent* Visual = AddVisual(Building, Cube);
		Visual->SetCollisionProfileName(TEXT("BlockAll"));
		Visual->SetCanEverAffectNavigation(true);
		TestTrue(TEXT("分支外网格自身允许导航，确保过滤来自建筑而非碰撞开关"), Visual->IsNavigationRelevant());
		TestFalse(Class->GetName() + TEXT(" 直接生成且未初始化时拒绝美术组件"), Building->IsComponentRelevantForNavigation(Visual));

		// 非 Scene 的导航修改器无法挂在 GameplayRoot 下，也必须被这条统一规则排除。
		UNavModifierComponent* Modifier = NewObject<UNavModifierComponent>(Building);
		Building->AddInstanceComponent(Modifier);
		Modifier->RegisterComponent();
		TestFalse(TEXT("无功能父级链的导航修改器不参与"), Building->IsComponentRelevantForNavigation(Modifier));
		TestFalse(TEXT("空组件不参与"), Building->IsComponentRelevantForNavigation(nullptr));
		Branch.Root->Rename(TEXT("RenamedGameplayGroup"));
		for (const FPrimitiveSnapshot& Snapshot : Branch.Snapshots)
		{
			TestTrue(TEXT("功能分支的多级、隐藏和触发组件保留导航资格"), Building->IsComponentRelevantForNavigation(Snapshot.Component));
		}
		Branch.Verify(*this);

		// 运行时重新挂接必须按当前层级立即重新判断，不永久修改功能件的作者开关。
		Visual->AttachToComponent(Branch.Root, FAttachmentTransformRules::KeepRelativeTransform);
		TestTrue(TEXT("网格移入功能分支后获得资格"), Building->IsComponentRelevantForNavigation(Visual));
		Visual->AttachToComponent(Building->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		TestFalse(TEXT("网格移出功能分支后失去资格"), Building->IsComponentRelevantForNavigation(Visual));
		TestTrue(TEXT("导航边界不强制改写组件自身开关"), Visual->CanEverAffectNavigation());
		Building->Destroy();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
