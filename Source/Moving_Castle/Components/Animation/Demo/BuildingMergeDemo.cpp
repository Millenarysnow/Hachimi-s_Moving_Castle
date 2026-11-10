#include "BuildingMergeDemo.h"

#include "BuildingMergeComponent.h"
#include "Camera/CameraComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "Components/StaticMeshComponent.h"
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
	DemoBoard->GridSize = FIntVector(2, bShowAdjacentChanges ? 2 : 1, 1);
	DemoBoard->BuildingDefinitions = { BuildingDefinition };
	DemoBoard->FinishSpawning(GetActorTransform());
	FCastleGroundGenerationResult Ground;
	for (int32 X = 0; X < 2; ++X)
	{
		for (int32 Y = 0; Y < DemoBoard->GridSize.Y; ++Y)
		{
			FCastleGroundCell Cell;
			Cell.Coordinate = FIntVector(X, Y, 0);
			Cell.Color = ECastleTileColor::Red;
			Cell.Number = 3 + Y;
			Ground.Cells.Add(Cell);
		}
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

	// 放置动画已隐藏原零件；取各网格仍保留的真实 Bounds，不能从“当前可见”几何计算空包围盒。
	FBox Bounds(ForceInit);
	const auto AddBuildingBounds = [&Bounds](const ACastleBuildingBase* Building)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Building->GetComponents<UStaticMeshComponent>(Meshes, false);
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			if (Mesh->GetStaticMesh() && !Mesh->ComponentHasTag(TEXT("MergeExclude")))
			{
				Bounds += Mesh->Bounds.GetBox();
			}
		}
	};
	AddBuildingBounds(CurrentBuilding);
	if (AdjacentBuilding) { AddBuildingBounds(AdjacentBuilding); }
	if (!Bounds.IsValid)
	{
		StopWithError(TEXT("无法从放置建筑的静态网格计算演示相机范围。"));
		return;
	}
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
	UE_LOG(LogBuildingMergeDemo, Display, TEXT("局部变化演示已开始：已有件短移，新放置件飞入；相邻区域共用传播原点。"));
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
	if (bShowAdjacentChanges)
	{
		Card.Number = 4;
		AdjacentBuilding = DemoBoard->PlaceBuilding(FIntVector(0, 1, 0), Card, false, Reason);
		if (!AdjacentBuilding)
		{
			StopWithError(Reason.ToString());
			return false;
		}
	}
	PhaseTime = 0.0;
	Phase = EDemoPhase::WaitingForInitialFlights;
	return true;
}

bool ABuildingMergeDemo::AreInitialFlightsComplete() const
{
	return IsValid(CurrentBuilding) && CurrentBuilding->bHasCompletedPlacement
		&& (!bShowAdjacentChanges || (IsValid(AdjacentBuilding) && AdjacentBuilding->bHasCompletedPlacement));
}

bool ABuildingMergeDemo::PlaceMergeCards()
{
	FCastleBuildingDescriptor Card;
	FText Reason;
	if (!BuildingDefinition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Reason))
	{
		StopWithError(Reason.ToString());
		return false;
	}
	DemoBoard->bUseSharedVisualChangeOrigin = true;
	DemoBoard->VisualChangeOrigin = DemoBoard->GetGridWorldLocation(FIntVector(1, 0, 0));
	if (!DemoBoard->PlaceBuilding(FIntVector(1, 0, 0), Card, true, Reason))
	{
		StopWithError(Reason.ToString());
		return false;
	}
	if (bShowAdjacentChanges)
	{
		Card.Number = 4;
		if (!DemoBoard->PlaceBuilding(FIntVector(1, 1, 0), Card, true, Reason))
		{
			StopWithError(Reason.ToString());
			return false;
		}
	}
	return true;
}

bool ABuildingMergeDemo::AreMergedBuildingsReady(double& OutAnimationDuration) const
{
	OutAnimationDuration = 0.0;
	const TArray<ACastleBuildingBase*> Buildings = DemoBoard->GetPlacedBuildings();
	const int32 ExpectedCount = bShowAdjacentChanges ? 2 : 1;
	if (Buildings.Num() != ExpectedCount)
	{
		return false;
	}
	for (const ACastleBuildingBase* Building : Buildings)
	{
		if (Building->GetBuildingLevel() != 2 || !Building->bHasCompletedPlacement
			|| Building->BuildingMerge->TotalDuration <= 0.0)
		{
			return false;
		}
		OutAnimationDuration = FMath::Max(OutAnimationDuration, Building->BuildingMerge->TotalDuration);
	}
	return true;
}

void ABuildingMergeDemo::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PhaseTime += DeltaSeconds;
	if (Phase == EDemoPhase::WaitingForInitialFlights)
	{
		if (AreInitialFlightsComplete())
		{
			// 开场停留从初始房屋真正落位后开始计时，确保能看见原始形态。
			Phase = EDemoPhase::ShowingInitialBuildings;
			PhaseTime = 0.0;
		}
		return;
	}
	if (Phase == EDemoPhase::ShowingInitialBuildings)
	{
		if (PhaseTime >= StartDelay && PlaceMergeCards())
		{
			Phase = EDemoPhase::WaitingForMergeFlights;
			PhaseTime = 0.0;
		}
		return;
	}
	if (Phase == EDemoPhase::WaitingForMergeFlights)
	{
		double ReadyDuration = 0.0;
		if (AreMergedBuildingsReady(ReadyDuration))
		{
			// 在局部合并动画刚启动时就开始取帧，而不是等到二级建筑已经全部归位。
			Phase = EDemoPhase::ShowingMergedBuildings;
			PhaseTime = 0.0;
			CaptureFrameIndex = 0;
		}
		else if (PhaseTime > 60.0)
		{
			StopWithError(TEXT("放置飞入后未能完成预期合并，请检查放置完成回调。"));
		}
		return;
	}

	double ShowcaseDuration = 0.0;
	if (!AreMergedBuildingsReady(ShowcaseDuration))
	{
		StopWithError(TEXT("合并演示期间结果建筑被意外替换或回收。"));
		return;
	}
	if (bCaptureFrames)
	{
		// 固定 25 个观察时刻，保留原建筑、拆散、汇聚、展开和最终模型，供视觉复核与 GIF 使用。
		if (CaptureFrameIndex <= 24 && PhaseTime >= CaptureFrameIndex * ShowcaseDuration / 24.0)
		{
			const FString Filename = FPaths::ProjectSavedDir() / TEXT("Tests/BuildingMergeVisual")
				/ FString::Printf(TEXT("Frame_%02d.png"), CaptureFrameIndex++);
			FScreenshotRequest::RequestScreenshot(Filename, false, false);
		}
		if (PhaseTime >= ShowcaseDuration + 1.2 && CaptureFrameIndex > 24)
		{
			UE_LOG(LogBuildingMergeDemo, Display, TEXT("放置飞入及局部合并视觉验收已完成：25 张截图已请求，结束验收进程。"));
			FPlatformMisc::RequestExit(false);
		}
	}
	else if (PhaseTime >= ShowcaseDuration + 2.0)
	{
		if (!bLoop)
		{
			SetActorTickEnabled(false);
			return;
		}
		FText Reason;
		for (ACastleBuildingBase* Building : DemoBoard->GetPlacedBuildings())
		{
			if (!DemoBoard->RemoveBuilding(Building, Reason))
			{
				StopWithError(Reason.ToString());
				return;
			}
		}
		CurrentBuilding = nullptr;
		AdjacentBuilding = nullptr;
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
