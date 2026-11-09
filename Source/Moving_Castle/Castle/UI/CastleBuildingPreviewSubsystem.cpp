// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingPreviewSubsystem.h"

#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleBoard.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 法线纹理与卡牌颜色无关，同一模型只保留一份动态捕获。 */
	FString GetPreviewKey(const FCastleBuildingDescriptor& Descriptor)
	{
		return FString::Printf(TEXT("%s:%d"), *Descriptor.Definition->GetPathName(), Descriptor.Level);
	}

	/** 与原卡片预览一致，每秒 14 度，约 26 秒旋转一圈。 */
	constexpr float PreviewRotationSpeed = 14.0f;
	constexpr float CaptureInterval = 1.0f / 20.0f;
	constexpr double VisibilityGraceSeconds = 0.25;

	/** 预览生成完整等级蓝图，并用与棋盘相同的默认占用形状初始化它。 */
	TArray<FIntVector> BuildPreviewFootprint(int32 Level)
	{
		TArray<FIntVector> Coordinates;
		const int32 Width = Level >= 2 && Level <= 4 ? 2 : 1;
		const int32 Depth = Level >= 3 && Level <= 4 ? 2 : 1;
		const int32 Height = Level == 4 ? 2 : 1;
		for (int32 X = 0; X < Width; ++X)
		{
			for (int32 Y = 0; Y < Depth; ++Y)
			{
				for (int32 Z = 0; Z < Height; ++Z)
				{
					Coordinates.Add(FIntVector(X, Y, Z));
				}
			}
		}
		return Coordinates;
	}
}

UCastleBuildingPreviewSubsystem::UCastleBuildingPreviewSubsystem()
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Game/MovingCastle/UI/Materials/M_BuildingPreviewOutline.M_BuildingPreviewOutline"));
	OutlineMaterial = Material.Object;
}

bool UCastleBuildingPreviewSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UTextureRenderTarget2D* UCastleBuildingPreviewSubsystem::GetPreview(const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason)
{
	if (!Descriptor.Validate(OutFailureReason))
	{
		return nullptr;
	}
	if (OutlineMaterial == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("建筑预览缺少 M_BuildingPreviewOutline 描边材质。"));
		return nullptr;
	}
	const FString Key = GetPreviewKey(Descriptor);
	if (const TObjectPtr<UTextureRenderTarget2D>* Found = Textures.Find(Key))
	{
		MarkPreviewUsed(Descriptor);
		return Found->Get();
	}
	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags = RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Parameters.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;
	const FVector Position(0.0f, Textures.Num() * 6000.0f, -100000.0f);
	const TSubclassOf<ACastleBuildingBase> BuildingClass = Descriptor.GetBuildingClass();
	if (BuildingClass == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("建筑卡片预览没有可生成的等级类。"));
		return nullptr;
	}
	ACastleBuildingBase* Model = GetWorld()->SpawnActor<ACastleBuildingBase>(BuildingClass, Position, FRotator::ZeroRotator, Parameters);
	if (Model == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("建筑卡片预览无法生成等级建筑 Actor。"));
		return nullptr;
	}
	// 与放置入口读取同一棋盘尺寸，增大单格间距后，占位模型的高宽比例也保持一致。
	const ACastleBoard* Board = Cast<ACastleBoard>(UGameplayStatics::GetActorOfClass(GetWorld(), ACastleBoard::StaticClass()));
	if (Board == nullptr)
	{
		Model->Destroy();
		OutFailureReason = FText::FromString(TEXT("建筑预览找不到当前棋盘，无法读取网格尺寸。"));
		return nullptr;
	}
	if (!Model->InitializeBuilding(Descriptor, BuildPreviewFootprint(Descriptor.Level)))
	{
		Model->Destroy();
		OutFailureReason = FText::FromString(TEXT("建筑卡片预览初始化等级 Actor 失败。"));
		return nullptr;
	}
	if (!Model->ConfigureBuildingVisual(Board->CellSize, Board->LayerHeight, OutFailureReason))
	{
		Model->Destroy();
		return nullptr;
	}
	Model->PrepareForPreview();
	// UE 的按需着色器编译不会仅因 IsComplete 查询而提交所有首次绘制任务。
	// 使用引擎为首次绘制提供的接口明确完成材质准备；相同模型的重复卡片走纹理缓存。
	TArray<UStaticMeshComponent*> ModelMeshes;
	Model->GetComponents<UStaticMeshComponent>(ModelMeshes);
	TSet<UMaterialInterface*> PreparedMaterials;
	for (UStaticMeshComponent* Component : ModelMeshes)
	{
		for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
		{
			UMaterialInterface* Material = Component->GetMaterial(Index);
			if (Material != nullptr && !PreparedMaterials.Contains(Material))
			{
				Material->EnsureIsComplete();
				PreparedMaterials.Add(Material);
			}
		}
	}
	const FBox Bounds = Model->GetVisualBounds();
	if (!Bounds.IsValid || Bounds.GetExtent().IsNearlyZero())
	{
		Model->Destroy();
		OutFailureReason = FText::FromString(TEXT("建筑卡片配置的模型没有可见几何体。"));
		return nullptr;
	}

	AActor* CaptureActor = GetWorld()->SpawnActor<AActor>(Position, FRotator::ZeroRotator, Parameters);
	if (CaptureActor == nullptr)
	{
		Model->Destroy();
		OutFailureReason = FText::FromString(TEXT("建筑卡片预览无法创建捕获 Actor。"));
		return nullptr;
	}
	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(CaptureActor);
	CaptureActor->SetRootComponent(Capture);
	CaptureActor->AddInstanceComponent(Capture);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_Normal;
	Capture->ShowOnlyActorComponents(Model);
	// OnRegister 会从类默认对象重建 ShowFlags，因此必须通过持久的设置数组配置。
	// 只直接修改 ShowFlags 会在注册时丢失，导致位于场景外的预览只剩天空和雾。
	TArray<FEngineShowFlagsSetting> FlagSettings;
	const auto AddFlag = [&FlagSettings](const TCHAR* Name, bool bEnabled)
	{
		FEngineShowFlagsSetting Setting;
		Setting.ShowFlagName = Name;
		Setting.Enabled = bEnabled;
		FlagSettings.Add(Setting);
	};
	AddFlag(TEXT("Atmosphere"), false);
	AddFlag(TEXT("Fog"), false);
	AddFlag(TEXT("Lighting"), false);
	AddFlag(TEXT("MotionBlur"), false);
	// 法线直接用于边缘提取，不应用色调映射、时间抗锯齿或运动模糊，避免留下实心面或拖影。
	AddFlag(TEXT("PostProcessing"), false);
	AddFlag(TEXT("TemporalAA"), false);
	AddFlag(TEXT("AntiAliasing"), false);
	Capture->SetShowFlagSettings(FlagSettings);
	Capture->FOVAngle = 32.0f;
	UTextureRenderTarget2D* Texture = NewObject<UTextureRenderTarget2D>(this);
	Texture->ClearColor = FLinearColor::Transparent;
	// 世界法线包含负数，浮点纹理必须保留这些分量，否则背向世界正轴的面会丢失描边。
	Texture->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	Texture->TargetGamma = 1.0f;
	Texture->InitAutoFormat(256, 256);
	Texture->UpdateResourceImmediate(true);
	Capture->TextureTarget = Texture;
	FPreviewEntry Entry;
	Entry.Model = Model;
	Entry.Capture = Capture;
	Entry.Center = Bounds.GetCenter();
	// 使用包围球保证整圈旋转均不裁切，不随视角改变缩放，保持原先平稳旋转的观感。
	Entry.Distance = Bounds.GetExtent().Size() / FMath::Sin(FMath::DegreesToRadians(Capture->FOVAngle * 0.5f)) * 1.15f;
	Entry.LastUsedAt = FPlatformTime::Seconds();
	const FRotator CameraRotation(-24.0f, Entry.RotationDegrees, 0.0f);
	CaptureActor->SetActorLocationAndRotation(Entry.Center - CameraRotation.Vector() * Entry.Distance, CameraRotation);
	Capture->RegisterComponent();
	Entries.Add(Key, Entry);
	PreviewActors.Add(Model);
	PreviewActors.Add(CaptureActor);
	Textures.Add(Key, Texture);
	return Texture;
}

void UCastleBuildingPreviewSubsystem::MarkPreviewUsed(const FCastleBuildingDescriptor& Descriptor)
{
	if (!IsValid(Descriptor.Definition)) { return; }
	if (FPreviewEntry* Entry = Entries.Find(GetPreviewKey(Descriptor)))
	{
		Entry->LastUsedAt = FPlatformTime::Seconds();
	}
}

bool UCastleBuildingPreviewSubsystem::IsTickable() const
{
	if (IsTemplate()) { return false; }
	const double Now = FPlatformTime::Seconds();
	for (const auto& Pair : Entries)
	{
		if (!Pair.Value.bFailed && Now - Pair.Value.LastUsedAt <= VisibilityGraceSeconds) { return true; }
	}
	return false;
}

TStatId UCastleBuildingPreviewSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCastleBuildingPreviewSubsystem, STATGROUP_Tickables);
}

void UCastleBuildingPreviewSubsystem::Tick(float DeltaTime)
{
	if (!GetWorld()->HasBegunPlay()) { return; }
	CaptureAccumulator += DeltaTime;
	if (CaptureAccumulator < CaptureInterval) { return; }
	const float Elapsed = CaptureAccumulator;
	CaptureAccumulator = 0.0f;
	const double Now = FPlatformTime::Seconds();
	for (auto& Pair : Entries)
	{
		FPreviewEntry& Entry = Pair.Value;
		if (Entry.bFailed || Now - Entry.LastUsedAt > VisibilityGraceSeconds) { continue; }
		if (!Entry.Model.IsValid() || !Entry.Capture.IsValid())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("动态建筑预览失败：模型或捕获对象已失效。"));
			Entry.bFailed = true;
			continue;
		}
		if (!Entry.bReady)
		{
			TArray<UStaticMeshComponent*> Meshes;
			Entry.Model->GetComponents<UStaticMeshComponent>(Meshes);
			bool bReady = true;
			for (const UStaticMeshComponent* Component : Meshes)
			{
				const UStaticMesh* Mesh = Component->GetStaticMesh();
				if (Mesh != nullptr && (Mesh->IsCompiling() || Mesh->HasPendingRenderResourceInitialization() || !Component->IsRenderStateCreated()))
				{
					bReady = false;
					break;
				}
			}
			if (!bReady)
			{
				Entry.ResourceWaitSeconds += Elapsed;
				if (Entry.ResourceWaitSeconds > 30.0f)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("建筑预览捕获失败：%s 的渲染资源在 30 秒内未就绪。"), *Entry.Model->GetName());
					Entry.bFailed = true;
				}
				continue;
			}
			Entry.bReady = true;
			Entry.Capture->ShowOnlyActorComponents(Entry.Model.Get());
		}
		// 相机绕实际几何中心旋转，整座拆件建筑一起转动，组件局部位置和模型资产均不改变。
		Entry.RotationDegrees = FMath::Fmod(Entry.RotationDegrees + PreviewRotationSpeed * Elapsed, 360.0f);
		const FRotator Rotation(-24.0f, Entry.RotationDegrees, 0.0f);
		Entry.Capture->GetOwner()->SetActorLocationAndRotation(Entry.Center - Rotation.Vector() * Entry.Distance, Rotation);
		Entry.Capture->CaptureSceneDeferred();
	}
}

void UCastleBuildingPreviewSubsystem::Deinitialize()
{
	Entries.Reset();
	for (AActor* Actor : PreviewActors)
	{
		if (IsValid(Actor)) { Actor->Destroy(); }
	}
	PreviewActors.Reset();
	Textures.Reset();
	Super::Deinitialize();
}
