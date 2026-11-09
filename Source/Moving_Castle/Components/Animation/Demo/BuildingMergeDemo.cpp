#include "BuildingMergeDemo.h"

#include "BuildingMergeComponent.h"
#include "Camera/CameraComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogBuildingMergeDemo, Log, All);

ABuildingMergeDemo::ABuildingMergeDemo()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot")));
	DemoCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("DemoCamera"));
	DemoCamera->SetupAttachment(GetRootComponent());
	DemoCamera->SetRelativeLocation(FVector(2100.0, -2800.0, 1800.0));
	DemoCamera->SetRelativeRotation((FVector(350.0, 0.0, 500.0) - DemoCamera->GetRelativeLocation()).Rotation());
	DemoCamera->SetFieldOfView(45.0f);
}

void ABuildingMergeDemo::BeginPlay()
{
	Super::BeginPlay();
	FText Reason;
	if (!IsValid(BuildingDefinition) || !BuildingDefinition->ValidateConfiguration(Reason)
		|| !BuildingDefinition->FindLevel(2) || !FMath::IsFinite(StartDelay) || StartDelay < 0.1)
	{
		StopWithError(FString::Printf(TEXT("请指定含一级、二级的有效建筑定义，并设置有效的开始等待时间。%s"), *Reason.ToString()));
		return;
	}
	// 延迟完成生成，避免正式棋盘在 BeginPlay 中创建随机地面或样例卡组。
	DemoBoard = GetWorld()->SpawnActorDeferred<ACastleBoard>(ACastleBoard::StaticClass(), GetActorTransform(),
		this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!DemoBoard)
	{
		StopWithError(TEXT("无法生成演示棋盘。"));
		return;
	}
	DemoBoard->bGenerateGroundOnBeginPlay = false;
	DemoBoard->GridSize = FIntVector(2, 1, 1);
	DemoBoard->BuildingDefinitions = { BuildingDefinition };
	DemoBoard->FinishSpawning(GetActorTransform());
	FCastleGroundGenerationResult Ground;
	for (int32 X = 0; X < 2; ++X)
	{
		FCastleGroundCell Cell;
		Cell.Coordinate = FIntVector(X, 0, 0);
		Cell.Color = ECastleTileColor::Red;
		Cell.Number = 3;
		Ground.Cells.Add(Cell);
	}
	if (!DemoBoard->ApplyGroundGenerationResult(Ground, Reason))
	{
		StopWithError(Reason.ToString());
		return;
	}
	if (!PlaceFirstBuilding())
	{
		return;
	}

	// 根据实际模型而非写死建筑尺寸取景；相邻两座一级建筑及松散零件都留有画面空间。
	FBox Bounds = CurrentBuilding->GetVisualBounds();
	Bounds += Bounds.ShiftBy(DemoBoard->GetGridWorldLocation(FIntVector(1, 0, 0))
		- DemoBoard->GetGridWorldLocation(FIntVector(0, 0, 0)));
	const FVector Center = Bounds.GetCenter();
	const FVector ViewDirection = GetActorQuat().RotateVector(FVector(1.1, -1.8, 1.0).GetSafeNormal());
	DemoCamera->SetWorldLocation(Center + ViewDirection * Bounds.GetExtent().Size() * 4.25);
	DemoCamera->SetWorldRotation((Center - DemoCamera->GetComponentLocation()).Rotation());
	DemoCamera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	DemoCamera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	DemoCamera->PostProcessSettings.bOverride_AutoExposureBias = true;
	DemoCamera->PostProcessSettings.AutoExposureBias = 0.0f;
	// 展示关卡使用低强度摄影棚灯光，固定曝光不叠加真实相机的光圈/快门衰减。
	DemoCamera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	DemoCamera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		Controller->SetViewTarget(this);
	}

	// 仅验收进程传入此参数时自动逐帧截图并退出，不影响编辑器里的普通 Play 演示。
	bCaptureFrames = FParse::Param(FCommandLine::Get(), TEXT("MergeDemoCapture"));
	if (bCaptureFrames)
	{
		bLoop = false;
		IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("Tests/BuildingMergeVisual")), true);
	}
	UE_LOG(LogBuildingMergeDemo, Display, TEXT("合并演示已开始：稍后自动将两座真实一级建筑合成为二级，普通运行会循环播放。"));
}

bool ABuildingMergeDemo::PlaceFirstBuilding()
{
	FCastleBuildingDescriptor Card;
	FText Reason;
	if (!BuildingDefinition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason))
	{
		StopWithError(Reason.ToString());
		return false;
	}
	CurrentBuilding = DemoBoard->PlaceBuilding(FIntVector::ZeroValue, Card, false, Reason);
	if (!CurrentBuilding)
	{
		StopWithError(Reason.ToString());
		return false;
	}
	PhaseTime = 0.0;
	bMerging = false;
	return true;
}

void ABuildingMergeDemo::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PhaseTime += DeltaSeconds;
	if (!bMerging && PhaseTime >= StartDelay)
	{
		FCastleBuildingDescriptor Card;
		FText Reason;
		if (!BuildingDefinition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason))
		{
			StopWithError(Reason.ToString());
			return;
		}
		CurrentBuilding = DemoBoard->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason);
		if (!CurrentBuilding || !Reason.IsEmpty() || !CurrentBuilding->BuildingMerge->bIsPlaying)
		{
			StopWithError(FString::Printf(TEXT("真实合并流程未启动动画。%s"), *Reason.ToString()));
			return;
		}
		bMerging = true;
		PhaseTime = 0.0;
	}
	if (!bMerging)
	{
		return;
	}

	if (bCaptureFrames)
	{
		// 固定 25 个观察时刻，保留原建筑、拆散、汇聚、展开和最终模型，供视觉复核与 GIF 使用。
		if (CaptureFrameIndex <= 24 && PhaseTime >= CaptureFrameIndex * CurrentBuilding->BuildingMerge->Duration / 24.0)
		{
			const FString Filename = FPaths::ProjectSavedDir() / TEXT("Tests/BuildingMergeVisual")
				/ FString::Printf(TEXT("Frame_%02d.png"), CaptureFrameIndex++);
			FScreenshotRequest::RequestScreenshot(Filename, false, false);
		}
		if (PhaseTime >= CurrentBuilding->BuildingMerge->Duration + 1.2 && CaptureFrameIndex > 24)
		{
			UE_LOG(LogBuildingMergeDemo, Display, TEXT("合并视觉验收已完成：25 张截图已请求，结束验收进程。"));
			FPlatformMisc::RequestExit(false);
		}
	}
	else if (PhaseTime >= CurrentBuilding->BuildingMerge->Duration + 2.0)
	{
		if (!bLoop)
		{
			SetActorTickEnabled(false);
			return;
		}
		FText Reason;
		if (!DemoBoard->RemoveBuilding(CurrentBuilding, Reason))
		{
			StopWithError(Reason.ToString());
			return;
		}
		PlaceFirstBuilding();
	}
}

void ABuildingMergeDemo::StopWithError(const FString& Message)
{
	SetActorTickEnabled(false);
	UE_LOG(LogBuildingMergeDemo, Error, TEXT("合并演示停止：%s"), *Message);
	if (FParse::Param(FCommandLine::Get(), TEXT("MergeDemoCapture")))
	{
		// 自动验收失败也应退出并提供非零状态，避免留下阻挡下一次编译的游戏/编辑器进程。
		FPlatformMisc::RequestExitWithStatus(false, 1);
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this), 12.0f, FColor::Red,
			FString::Printf(TEXT("合并演示停止：%s"), *Message));
	}
}

void ABuildingMergeDemo::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 演示 Actor 被单独删除时也清理其棋盘和建筑，不在当前世界留下占用或临时副本。
	if (IsValid(DemoBoard))
	{
		for (ACastleBuildingBase* Building : DemoBoard->GetPlacedBuildings())
		{
			Building->Destroy();
		}
		DemoBoard->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}
