// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleCardPortraitSubsystem.h"

#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Moving_Castle.h"

namespace
{
	// 商店卡窗约 140 像素宽；较高分辨率的线性纹理可让缩小后的建筑边缘保有覆盖率。
	constexpr int32 PortraitResolution = 512;
	constexpr float PortraitFov = 31.0f;
	constexpr double CaptureInterval = 1.0 / 30.0;
	constexpr double GeometryWaitSeconds = 30.0;

	/** Alpha 的 1 是「全透明」。不能用 Transparent 的 A=0 清屏，初始背景会变成实心。 */
	UTextureRenderTarget2D* NewPortraitTarget(UObject* Owner)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Owner, NAME_None, RF_Transient);
		Target->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
		Target->RenderTargetFormat = RTF_RGBA16f;
		Target->TargetGamma = 1.0f;
		Target->AddressX = TA_Clamp;
		Target->AddressY = TA_Clamp;
		Target->InitAutoFormat(PortraitResolution, PortraitResolution);
		return Target;
	}

	/** 固定摄影棚的主光、补光、轮廓光，不读取关卡的昼夜或曝光。 */
	void AddStudioLight(FPreviewScene& Studio, const FRotator& Direction, const FLinearColor& Color, float Brightness)
	{
		UDirectionalLightComponent* Light = NewObject<UDirectionalLightComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensity(Brightness);
		Light->SetLightColor(Color);
		Light->SetCastShadows(false);
		Studio.AddComponent(Light, FTransform(Direction));
	}
}

bool UCastleCardPortraitSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UCastleCardPortrait* UCastleCardPortraitSubsystem::RequestPortrait(const FCastleBuildingDescriptor& Descriptor,
	const ACastleBoard* Board, FText& OutFailureReason)
{
	if (!Descriptor.Validate(OutFailureReason))
	{
		return nullptr;
	}
	if (!IsValid(Board) || Board->GetWorld() != GetWorld() || !FMath::IsFinite(Board->CellSize)
		|| !FMath::IsFinite(Board->LayerHeight) || Board->CellSize <= 0.0f || Board->LayerHeight <= 0.0f)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("棋盘 %s 无法提供有效的建筑取景尺寸。"), *GetNameSafe(Board)));
		return nullptr;
	}
	// 正式材质不读取玩法色号，箔光也只属于卡面；不为这些值重复建立摄影缓存。
	const FString Key = FString::Printf(TEXT("%s:%d:%.9g:%.9g"), *Descriptor.Definition->GetPathName(),
		Descriptor.Level, Board->CellSize, Board->LayerHeight);
	if (const TObjectPtr<UCastleCardPortrait>* Found = Portraits.Find(Key))
	{
		return Found->Get();
	}

	UCastleCardPortrait* Portrait = NewObject<UCastleCardPortrait>(this, NAME_None, RF_Transient);
	Portrait->Descriptor = Descriptor;
	Portrait->CellSize = Board->CellSize;
	Portrait->LayerHeight = Board->LayerHeight;
	Portraits.Add(Key, Portrait);
	PendingPortraits.Add(Portrait);
	return Portrait;
}

void UCastleCardPortraitSubsystem::SetShopVisible(UObject* Shop, bool bVisible)
{
	if (bVisible)
	{
		VisibleShop = Shop;
	}
	else if (VisibleShop.Get() == Shop)
	{
		VisibleShop.Reset();
		ResetHover();
	}
}

void UCastleCardPortraitSubsystem::SetHover(UObject* Consumer, UCastleCardPortrait* Portrait, const FVector2D& Tilt)
{
	if (HoverConsumer.Get() != Consumer || HoverPortrait != Portrait)
	{
		// 上一张卡的动态纹理内容不是当前卡的画面，只有完成新的捕获才公开共享纹理。
		ResetHover();
		HoverConsumer = Consumer;
		HoverPortrait = Portrait;
		bHoverNeedsCapture = true;
	}
	if (!HoverTilt.Equals(Tilt, 0.005))
	{
		HoverTilt = Tilt;
		bHoverNeedsCapture = true;
	}
}

void UCastleCardPortraitSubsystem::ClearHover(const UObject* Consumer)
{
	// 刷新列表时旧 Widget 的析构可能晚于新 Widget 的鼠标进入，不允许旧卡清除新卡的请求。
	if (HoverConsumer.Get() == Consumer)
	{
		ResetHover();
	}
}

void UCastleCardPortraitSubsystem::ResetHover()
{
	HoverConsumer.Reset();
	HoverPortrait = nullptr;
	HoverTilt = FVector2D::ZeroVector;
	HoverFailure = FText::GetEmpty();
	bHoverNeedsCapture = false;
	bHoverReady = false;
}

UTextureRenderTarget2D* UCastleCardPortraitSubsystem::GetHoverTexture(const UObject* Consumer) const
{
	return bHoverReady && HoverConsumer.Get() == Consumer ? HoverTexture.Get() : nullptr;
}

const FText* UCastleCardPortraitSubsystem::GetHoverFailure(const UObject* Consumer) const
{
	return HoverConsumer.Get() == Consumer && !HoverFailure.IsEmpty() ? &HoverFailure : nullptr;
}

bool UCastleCardPortraitSubsystem::CreateStudio(FText& OutFailureReason)
{
	if (Studio)
	{
		return true;
	}
	if (!FApp::CanEverRender())
	{
		OutFailureReason = FText::FromString(TEXT("当前进程禁止渲染，无法进行建筑卡牌摄影。"));
		return false;
	}
	// 不绑定真实 GameInstance 或 GameMode，也不启动摄影世界的 BeginPlay，预览不参与玩法。
	Studio = MakeUnique<FPreviewScene>(FPreviewScene::ConstructionValues().SetEditor(false)
		.SetCreateDefaultLighting(false).SetCreatePhysicsScene(false).SetForceMipsResident(false).SetTransactional(false));
	AddStudioLight(*Studio, FRotator(-43.0, 15.0, 0.0), FLinearColor(1.0f, 0.88f, 0.69f), 5.0f);
	AddStudioLight(*Studio, FRotator(-24.0, -109.0, 0.0), FLinearColor(0.57f, 0.78f, 1.0f), 2.6f);
	AddStudioLight(*Studio, FRotator(-53.0, 162.0, 0.0), FLinearColor(0.91f, 0.80f, 0.62f), 2.9f);

	Camera = NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Camera->bCaptureEveryFrame = false;
	Camera->bCaptureOnMovement = false;
	Camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Camera->CaptureSource = SCS_SceneColorHDR;
	Camera->FOVAngle = PortraitFov;
	TArray<FEngineShowFlagsSetting> FlagSettings;
	// 持久的 ShowFlagSettings 不会在注册组件时被 CDO 重置。后期与主视图曝光不是摄影输入。
	for (const TCHAR* Name : {TEXT("Atmosphere"), TEXT("Fog"), TEXT("VolumetricFog"), TEXT("SkyLighting"),
		TEXT("PostProcessing"), TEXT("EyeAdaptation"), TEXT("MotionBlur"), TEXT("TemporalAA"),
		TEXT("AntiAliasing"), TEXT("GlobalIllumination"), TEXT("LumenReflections")})
	{
		FEngineShowFlagsSetting Setting;
		Setting.ShowFlagName = Name;
		Setting.Enabled = false;
		FlagSettings.Add(Setting);
	}
	Camera->SetShowFlagSettings(FlagSettings);
	Studio->AddComponent(Camera, FTransform::Identity);
	return true;
}

bool UCastleCardPortraitSubsystem::StagePortrait(UCastleCardPortrait* Portrait, FText& OutFailureReason)
{
	if (StagedPortrait == Portrait && IsValid(StagedModel))
	{
		return true;
	}
	if (!CreateStudio(OutFailureReason))
	{
		return false;
	}
	DropStage();
	const TSubclassOf<ACastleBuildingBase> ModelClass = Portrait->Descriptor.GetBuildingClass();
	if (ModelClass == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("定义没有有效的等级建筑类。"));
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags = RF_Transient;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	StagedModel = Studio->GetWorld()->SpawnActor<ACastleBuildingBase>(ModelClass, FTransform::Identity, Spawn);
	if (!IsValid(StagedModel))
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("摄影场景无法生成等级类 %s。"), *GetNameSafe(ModelClass.Get())));
		return false;
	}
	if (!StagedModel->InitializeBuilding(Portrait->Descriptor, ACastleBoard::BuildPlacementFootprint(FIntVector::ZeroValue,
		Portrait->Descriptor.Level)))
	{
		OutFailureReason = FText::FromString(TEXT("真实等级模型初始化失败。"));
		DropStage();
		return false;
	}
	if (!StagedModel->ConfigureBuildingVisual(Portrait->CellSize, Portrait->LayerHeight, OutFailureReason))
	{
		DropStage();
		return false;
	}
	StagedModel->PrepareForPreview();
	const FBox Bounds = StagedModel->GetVisualBounds();
	if (!Bounds.IsValid || Bounds.GetExtent().IsNearlyZero())
	{
		OutFailureReason = FText::FromString(TEXT("等级建筑没有可取景的美术几何。"));
		DropStage();
		return false;
	}
	TArray<UMeshComponent*> Meshes;
	StagedModel->GetComponents<UMeshComponent>(Meshes);
	TSet<UMaterialInterface*> PreparedMaterials;
	for (UMeshComponent* Mesh : Meshes)
	{
		// 按需着色器编译时，主动准备此模型会实际绘制的材质，不能把首次灰模渲进缓存。
		if (Mesh->IsVisible())
		{
			for (UMaterialInterface* Material : Mesh->GetMaterials())
			{
				if (!IsValid(Material))
				{
					OutFailureReason = FText::FromString(FString::Printf(TEXT("美术组件 %s 的材质槽未配置。"), *Mesh->GetPathName()));
					DropStage();
					return false;
				}
				if (!PreparedMaterials.Contains(Material))
				{
					Material->EnsureIsComplete();
					PreparedMaterials.Add(Material);
				}
			}
		}
	}

	StageCenter = Bounds.GetCenter();
	// 预留卡窗圆角和悬停余量；同一个模型切换视角时保持距离，避免呼吸式缩放。
	StageDistance = Bounds.GetExtent().Size() / FMath::Sin(FMath::DegreesToRadians(PortraitFov * 0.5f)) * 1.08f;
	Camera->ShowOnlyActorComponents(StagedModel);
	StagedPortrait = Portrait;
	GeometryWaitStartedAt = FPlatformTime::Seconds();
	return true;
}

bool UCastleCardPortraitSubsystem::CaptureInto(UTextureRenderTarget2D* Target, const FVector2D& Tilt, FText& OutFailureReason)
{
	if (!IsValid(Camera) || !IsValid(Target) || !IsValid(StagedModel) || !Camera->IsRegistered())
	{
		OutFailureReason = FText::FromString(TEXT("摄影场景、镜头或渲染目标已经失效。"));
		return false;
	}
	// 预览世界没有主视口，因此直接提交捕获，不能等待 CaptureSceneDeferred 所依赖的主视图。
	const FRotator View(-20.0 + Tilt.Y * 5.5, -34.0 + Tilt.X * 10.0, 0.0);
	Camera->SetWorldLocationAndRotation(StageCenter - View.Vector() * StageDistance, View);
	Camera->TextureTarget = Target;
	Camera->CaptureScene();
	return true;
}

void UCastleCardPortraitSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (Now < NextCaptureAt || !VisibleShop.IsValid())
	{
		return;
	}
	const bool bIdleCapture = !PendingPortraits.IsEmpty();
	UCastleCardPortrait* Portrait = bIdleCapture ? PendingPortraits[0].Get() : HoverPortrait.Get();
	if (Portrait == nullptr)
	{
		return;
	}
	NextCaptureAt = Now + CaptureInterval;
	FText Reason;
	bool bCanCapture = StagePortrait(Portrait, Reason);
	if (bCanCapture)
	{
		// 网格异步建好后才能拍一张可长期复用的图；等待有明示状态与超时，不提交空图。
		TArray<UStaticMeshComponent*> Meshes;
		StagedModel->GetComponents<UStaticMeshComponent>(Meshes);
		for (const UStaticMeshComponent* Component : Meshes)
		{
			const UStaticMesh* Mesh = Component->GetStaticMesh();
			if (Component->IsVisible() && (!IsValid(Mesh) || Mesh->IsCompiling()
				|| Mesh->HasPendingRenderResourceInitialization() || !Component->IsRenderStateCreated()))
			{
				if (IsValid(Mesh) && Now - GeometryWaitStartedAt < GeometryWaitSeconds)
				{
					return;
				}
				Reason = FText::FromString(FString::Printf(TEXT("网格 %s（组件 %s）缺失或渲染资源未在时限内就绪。"),
					*GetNameSafe(Mesh), *Component->GetPathName()));
				bCanCapture = false;
				break;
			}
		}
	}

	if (bIdleCapture)
	{
		PendingPortraits.RemoveAt(0, EAllowShrinking::No);
		if (bCanCapture)
		{
			Portrait->Texture = NewPortraitTarget(this);
			if (CaptureInto(Portrait->Texture, FVector2D::ZeroVector, Reason))
			{
				Portrait->State = ECastleCardPortraitState::Ready;
				return;
			}
			Portrait->Texture = nullptr;
		}
		FailPortrait(Portrait, Reason);
		DropStage();
		return;
	}

	bHoverNeedsCapture = false;
	if (bCanCapture)
	{
		if (HoverTexture == nullptr)
		{
			HoverTexture = NewPortraitTarget(this);
		}
		if (CaptureInto(HoverTexture, HoverTilt, Reason))
		{
			bHoverReady = true;
			return;
		}
	}
	// 动态成像失败时不拿另一张卡的图或旧动态帧继续呈现，向当前卡公开失败原因。
	bHoverReady = false;
	HoverFailure = Reason;
	UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡牌 %s Lv%d 悬停捕获失败：%s"),
		*GetPathNameSafe(Portrait->Descriptor.Definition.Get()), Portrait->Descriptor.Level, *Reason.ToString());
	DropStage();
}

bool UCastleCardPortraitSubsystem::IsTickable() const
{
	return Super::IsTickable() && VisibleShop.IsValid() && (!PendingPortraits.IsEmpty()
		|| (bHoverNeedsCapture && HoverFailure.IsEmpty() && HoverConsumer.IsValid() && IsValid(HoverPortrait)
			&& HoverPortrait->State == ECastleCardPortraitState::Ready));
}

TStatId UCastleCardPortraitSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCastleCardPortraitSubsystem, STATGROUP_Tickables);
}

void UCastleCardPortraitSubsystem::FailPortrait(UCastleCardPortrait* Portrait, const FText& Reason) const
{
	Portrait->State = ECastleCardPortraitState::Failed;
	Portrait->FailureReason = Reason;
	UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡牌 %s Lv%d 静止捕获失败：%s"),
		*GetPathNameSafe(Portrait->Descriptor.Definition.Get()), Portrait->Descriptor.Level, *Reason.ToString());
}

void UCastleCardPortraitSubsystem::DropStage()
{
	if (IsValid(Camera))
	{
		Camera->ClearShowOnlyComponents();
		Camera->TextureTarget = nullptr;
	}
	if (IsValid(StagedModel))
	{
		StagedModel->Destroy();
	}
	StagedModel = nullptr;
	StagedPortrait = nullptr;
}

void UCastleCardPortraitSubsystem::Deinitialize()
{
	// 镜头先断开纹理和入镜物体，再销毁摄影场景，防止结束 PIE 时残留世界上下文。
	VisibleShop.Reset();
	ResetHover();
	DropStage();
	Camera = nullptr;
	Studio.Reset();
	PendingPortraits.Reset();
	Portraits.Reset();
	HoverTexture = nullptr;
	Super::Deinitialize();
}
