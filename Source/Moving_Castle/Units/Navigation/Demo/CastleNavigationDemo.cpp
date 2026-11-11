#include "Demo/CastleNavigationDemo.h"

#include "AssemblyFlightComponent.h"
#include "BuildingMergeComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

ACastleNavigationDemo::ACastleNavigationDemo()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Cube = Mesh.Object;
	// 这些碰撞就是可走结构本身，不存在另一套专用导航模型或六面数据。
	const auto AddBox = [this](const FName Name, FVector Position, FVector Size)
	{
		UStaticMeshComponent* Box = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Box->SetupAttachment(GetRootComponent());
		Box->SetStaticMesh(Cube);
		Box->SetRelativeLocation(Position);
		Box->SetRelativeScale3D(Size / 100.0);
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetCanEverAffectNavigation(true);
	};
	AddBox(TEXT("LowerFloor"), FVector(-700.0, 0.0, -25.0), FVector(1400.0, 1000.0, 50.0));
	AddBox(TEXT("UpperFloor"), FVector(1700.0, 0.0, 575.0), FVector(1000.0, 1000.0, 50.0));
	for (int32 Step = 0; Step < 20; ++Step)
	{
		const double Top = (Step + 1) * 30.0;
		AddBox(*FString::Printf(TEXT("Stair_%02d"), Step), FVector((Step + 0.5) * 60.0, 0.0, (Top - 50.0) * 0.5), FVector(60.0, 360.0, Top + 50.0));
	}
	DemoCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	DemoCamera->SetupAttachment(GetRootComponent());
	DemoCamera->SetRelativeLocation(FVector(-1800.0, -3100.0, 2300.0));
	DemoCamera->SetRelativeRotation((FVector(350.0, 0.0, 200.0) - DemoCamera->GetRelativeLocation()).Rotation());
	DemoCamera->SetFieldOfView(58.0f);
	// 减少白模过曝，使楼梯、墙体与绿色路线在默认灯光下仍能区分。
	DemoCamera->PostProcessSettings.bOverride_AutoExposureBias = true;
	DemoCamera->PostProcessSettings.AutoExposureBias = -1.0f;
}

AActor* ACastleNavigationDemo::SpawnWall(const TCHAR* Label, int32 FirstPiece, int32 NumPieces, double YOffset)
{
	AActor* Wall = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), GetActorTransform());
	if (!Wall) { return nullptr; }
	USceneComponent* Root = NewObject<USceneComponent>(Wall, TEXT("Root"));
	Wall->SetRootComponent(Root);
	Wall->AddInstanceComponent(Root);
	Root->RegisterComponent();
	Wall->SetActorTransform(GetActorTransform());
	Wall->Tags.Add(FName(Label));
	for (int32 Index = FirstPiece; Index < FirstPiece + NumPieces; ++Index)
	{
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(Wall);
		Piece->SetupAttachment(Root);
		Piece->SetMobility(EComponentMobility::Movable);
		Piece->SetStaticMesh(Cube);
		Piece->SetRelativeLocation(FVector(-600.0, -140.0 + Index * 80.0 + YOffset, 120.0));
		Piece->SetRelativeScale3D(FVector(0.8, 0.8, 2.4));
		Piece->SetCollisionProfileName(TEXT("BlockAll"));
		Piece->SetCanEverAffectNavigation(true);
		Wall->AddInstanceComponent(Piece);
		Piece->RegisterComponent();
	}
	return Wall;
}

void ACastleNavigationDemo::BeginPlay()
{
	Super::BeginPlay();
	bAutomation = FParse::Param(FCommandLine::Get(), TEXT("NavigationDemoAutomation"));
	const FScopedCastleNavigationChange NavigationChange(GetWorld(), this);
	for (int32 Half = 0; Half < 2; ++Half)
	{
		AActor* Wall = SpawnWall(TEXT("InitialWall"), Half * 4, 4, 0.0);
		if (!Wall) { StopWithError(TEXT("无法生成演示墙体。")); return; }
		Walls.Add(Wall);
		UAssemblyFlightComponent* Flight = NewObject<UAssemblyFlightComponent>(Wall);
		Flight->bAutoPlay = false;
		Flight->bShowStatusOnScreen = false;
		Flight->StartDelay = 0.5;
		Flight->LaunchInterval = 0.35;
		Flight->FlightDuration = 1.0;
		Wall->AddInstanceComponent(Flight);
		Flight->RegisterComponent();
		if (!Flight->StartAssembly()) { StopWithError(Flight->LastError); return; }
	}
	FText Failure;
	if (bRunPathDemonstration && !GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->RequestSharedPath(
		GetActorTransform().TransformPosition(FVector(-1200.0, 0.0, 5.0)),
		GetActorTransform().TransformPosition(FVector(2000.0, 0.0, 605.0)), Failure))
	{
		StopWithError(Failure.ToString());
		return;
	}
	if (bUseFixedCamera)
	{
		if (APlayerController* Controller = GetWorld()->GetFirstPlayerController()) { Controller->SetViewTarget(this); }
	}
	else if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Pawn = Controller->GetPawn())
		{
			// 新巡逻演示保留默认 Pawn 的飞行输入，只添加同曝光/视野的相机，让 NPC 和台阶清晰可见。
			UCameraComponent* FlyingCamera = NewObject<UCameraComponent>(Pawn, TEXT("PatrolDemoFlyingCamera"));
			FlyingCamera->SetupAttachment(Pawn->GetRootComponent());
			FlyingCamera->bUsePawnControlRotation = true;
			FlyingCamera->SetFieldOfView(DemoCamera->FieldOfView);
			FlyingCamera->PostProcessSettings = DemoCamera->PostProcessSettings;
			Pawn->AddInstanceComponent(FlyingCamera);
			FlyingCamera->RegisterComponent();
			Controller->SetViewTarget(Pawn);
		}
	}
}

bool ACastleNavigationDemo::StartMerge()
{
	if (Phase != 1 || Walls.Num() != 2) { return false; }
	const FScopedCastleNavigationChange NavigationChange(GetWorld(), this);
	AActor* Result = SpawnWall(TEXT("MergedWall"), 0, 8, -280.0);
	if (!Result) { StopWithError(TEXT("无法生成合并后的演示墙体。")); return false; }
	Merge = NewObject<UBuildingMergeComponent>(Result);
	Merge->bShowStatusOnScreen = false;
	Merge->Duration = 2.0;
	Result->AddInstanceComponent(Merge);
	Merge->RegisterComponent();
	TArray<AActor*> Sources;
	for (AActor* Wall : Walls) { Sources.Add(Wall); }
	if (!Merge->StartMerge(Sources)) { StopWithError(Merge->LastError); return false; }
	for (AActor* Wall : Walls) { Wall->Destroy(); }
	Walls = {Result};
	Phase = 2;
	return true;
}

void ACastleNavigationDemo::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunPathDemonstration) { return; }
	UCastleNavigationSubsystem* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	const auto Path = Navigation->AcquireSharedPath();
	if (Phase >= 0 && !Navigation->LastFailure.IsEmpty() && Navigation->ActiveChangeCount == 0
		&& !Navigation->bBuildingNavigation && (!Path || !Path->bValid))
	{
		StopWithError(Navigation->LastFailure.ToString());
	}
	if (Path && Path->bValid)
	{
		// 转折点之间是引擎的计划路线。作为前景叠加显示，避免楼梯遮住调试折线；
		// 后续实际单位的贴地跟随不由这里的绘制逻辑承担。
		for (int32 Index = 1; Index < Path->Points.Num(); ++Index)
		{
			DrawDebugLine(GetWorld(), Path->Points[Index - 1] + FVector(0.0, 0.0, 15.0), Path->Points[Index] + FVector(0.0, 0.0, 15.0), FColor::Green, false, -1.0f, 1, 8.0f);
		}
		for (const FVector& Point : Path->Points) { DrawDebugSphere(GetWorld(), Point + FVector(0.0, 0.0, 15.0), 12.0f, 8, FColor::Yellow, false, -1.0f, 1); }
		if (Phase == 0)
		{
			InitialPath = *Path;
			Phase = 1;
			ReadyTime = GetWorld()->GetTimeSeconds();
		}
		else if (Phase == 2)
		{
			MergedPath = *Path;
			Phase = 3;
			UE_LOG(LogMoving_Castle, Display, TEXT("导航演示完成：飞入和合并各刷新一次，最终共享路径已绕过新墙并沿楼梯抵达上层。"));
		}
	}
	if (Phase == 1 && !bAutomation && GetWorld()->GetTimeSeconds() - ReadyTime >= 4.0) { StartMerge(); }
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this), 0.1f, Phase < 0 ? FColor::Red : FColor::Cyan,
			FString::Printf(TEXT("Shared NavMesh | Phase %d | Animations %d | Refresh %d | Path queries %d | Version %d\nGreen: ONE shared path, yellow: path corners. Assembly -> route -> merge -> new route."), Phase,
				Navigation->ActiveChangeCount, Navigation->RefreshCount, Navigation->PathQueryCount, Navigation->NavigationRevision));
	}
}

void ACastleNavigationDemo::StopWithError(const FString& Error)
{
	Phase = -1;
	UE_LOG(LogMoving_Castle, Error, TEXT("导航演示失败：%s"), *Error);
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 30.0f, FColor::Red, Error); }
}
