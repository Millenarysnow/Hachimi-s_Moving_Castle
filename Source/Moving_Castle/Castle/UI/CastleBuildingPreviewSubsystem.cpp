// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingPreviewSubsystem.h"

#include "CastleBuildingDefinition.h"
#include "CastleBuildingModelActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"

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
	const FString Key = FString::Printf(TEXT("%s:%d:%d"), *Descriptor.Definition->GetPathName(), Descriptor.Level, static_cast<int32>(Descriptor.Color));
	if (const TObjectPtr<UTextureRenderTarget2D>* Found = Textures.Find(Key))
	{
		return Found->Get();
	}
	FActorSpawnParameters Parameters;
	Parameters.ObjectFlags = RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Position(0.0f, Textures.Num() * 6000.0f, -100000.0f);
	ACastleBuildingModelActor* Model = GetWorld()->SpawnActor<ACastleBuildingModelActor>(Position, FRotator::ZeroRotator, Parameters);
	if (Model == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("建筑卡片预览无法生成模型载体。"));
		return nullptr;
	}
	// 预览只需要标准占用形状的外包范围，合成规则本身仍由棋盘执行。
	FIntVector Dimensions(1, 1, 1);
	if (Descriptor.Level == 2) { Dimensions = FIntVector(2, 1, 1); }
	if (Descriptor.Level == 3) { Dimensions = FIntVector(2, 2, 1); }
	if (Descriptor.Level == 4) { Dimensions = FIntVector(2, 2, 2); }
	if (!Model->ConfigureModel(Descriptor.Definition, Descriptor.Level, Descriptor.Color, Dimensions, 500.0f, 500.0f, OutFailureReason))
	{
		Model->Destroy();
		return nullptr;
	}
	Model->PrepareForPreview();
	// UE 的按需着色器编译不会仅因 IsComplete 查询而提交所有首次绘制任务。
	// 使用引擎为首次绘制提供的接口明确完成材质准备；相同模型的重复卡片走纹理缓存。
	TArray<UStaticMeshComponent*> ModelMeshes;
	Model->GetComponents<UStaticMeshComponent>(ModelMeshes, true);
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
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->ShowOnlyActorComponents(Model, true);
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
	// FinalColorLDR 保留最终颜色处理通道，模型材质颜色不依赖关卡灯光。
	AddFlag(TEXT("PostProcessing"), true);
	Capture->SetShowFlagSettings(FlagSettings);
	Capture->FOVAngle = 32.0f;
	UTextureRenderTarget2D* Texture = NewObject<UTextureRenderTarget2D>(this);
	Texture->ClearColor = FLinearColor(0.015f, 0.025f, 0.045f, 1.0f);
	Texture->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	Texture->InitAutoFormat(256, 256);
	Texture->UpdateResourceImmediate(true);
	Capture->TextureTarget = Texture;
	const FVector Direction = FVector(1.0f, -1.4f, 1.0f).GetSafeNormal();
	const float Distance = Bounds.GetExtent().Size() / FMath::Sin(FMath::DegreesToRadians(Capture->FOVAngle * 0.5f)) * 1.15f;
	const FVector CameraPosition = Bounds.GetCenter() + Direction * Distance;
	CaptureActor->SetActorLocationAndRotation(CameraPosition, (Bounds.GetCenter() - CameraPosition).Rotation());
	Capture->RegisterComponent();
	// 首次加载的拆件模型可能仍在异步编译，必须等真实渲染资源就绪后再缓存。
	PendingPreviews.Add({ Model, Capture, FPlatformTime::Seconds() });
	PreviewActors.Add(Model);
	PreviewActors.Add(CaptureActor);
	Textures.Add(Key, Texture);
	return Texture;
}

bool UCastleBuildingPreviewSubsystem::IsTickable() const
{
	return !IsTemplate() && !PendingPreviews.IsEmpty();
}

TStatId UCastleBuildingPreviewSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCastleBuildingPreviewSubsystem, STATGROUP_Tickables);
}

void UCastleBuildingPreviewSubsystem::Tick(float DeltaTime)
{
	if (!GetWorld()->HasBegunPlay()) { return; }
	for (int32 Index = PendingPreviews.Num() - 1; Index >= 0; --Index)
	{
		const FPendingPreview& Pending = PendingPreviews[Index];
		if (!Pending.Model.IsValid() || !Pending.Capture.IsValid())
		{
			PendingPreviews.RemoveAtSwap(Index);
			continue;
		}
		TArray<UStaticMeshComponent*> Meshes;
		Pending.Model->GetComponents<UStaticMeshComponent>(Meshes, true);
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
		if (bReady)
		{
			Pending.Capture->ShowOnlyActorComponents(Pending.Model.Get(), true);
			Pending.Capture->CaptureSceneDeferred();
			PendingPreviews.RemoveAtSwap(Index);
		}
		else if (FPlatformTime::Seconds() - Pending.RequestedAt > 30.0)
		{
			// 无法完成初始化时报告错误并停止等待，不切换模型或写入另一种占位。
			UE_LOG(LogMoving_Castle, Error, TEXT("建筑预览捕获失败：模型 %s 的渲染资源在 30 秒内未就绪。"), *Pending.Model->GetName());
			PendingPreviews.RemoveAtSwap(Index);
		}
	}
}

void UCastleBuildingPreviewSubsystem::Deinitialize()
{
	PendingPreviews.Reset();
	for (AActor* Actor : PreviewActors)
	{
		if (IsValid(Actor)) { Actor->Destroy(); }
	}
	PreviewActors.Reset();
	Textures.Reset();
	Super::Deinitialize();
}
