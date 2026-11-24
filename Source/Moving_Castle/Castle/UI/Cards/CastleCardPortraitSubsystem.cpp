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
	// 卡面摄影独立限频为 30 Hz，使用专用名称避免在 Unity Build 中与建造预览冲突。
	constexpr double CardPortraitCaptureInterval = 1.0 / 30.0;
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
		if (!VisibleStandalonePortraits.Contains(DynamicConsumer)) { ResetDynamicView(); }
	}
}

void UCastleCardPortraitSubsystem::SetStandalonePortraitVisible(UObject* Consumer, UCastleCardPortrait* Portrait, bool bVisible)
{
	if (!bVisible)
	{
		VisibleStandalonePortraits.Remove(Consumer);
		ClearDynamicView(Consumer);
		return;
	}
	if (!IsValid(Consumer) || !IsValid(Portrait))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("无法登记独立卡牌摄影：卡牌 %s 或摄影请求 %s 已失效。"),
			*GetPathNameSafe(Consumer), *GetPathNameSafe(Portrait));
		return;
	}
	VisibleStandalonePortraits.Add(Consumer, Portrait);
}

void UCastleCardPortraitSubsystem::SetDynamicView(UObject* Consumer, UCastleCardPortrait* Portrait, const FVector2D& Tilt)
{
	if (DynamicConsumer.Get() != Consumer || DynamicPortrait != Portrait)
	{
		// 上一张卡的动态纹理内容不是当前卡的画面，只有完成新的捕获才公开共享纹理。
		ResetDynamicView();
		DynamicConsumer = Consumer;
		DynamicPortrait = Portrait;
		bDynamicNeedsCapture = true;
	}
	if (!DynamicTilt.Equals(Tilt, 0.005))
	{
		DynamicTilt = Tilt;
		bDynamicNeedsCapture = true;
	}
}

void UCastleCardPortraitSubsystem::ClearDynamicView(const UObject* Consumer)
{
	// 刷新列表时旧 Widget 的析构可能晚于新卡被抓住，不允许旧卡清除新卡的请求。
	if (DynamicConsumer.Get() == Consumer)
	{
		ResetDynamicView();
	}
}

void UCastleCardPortraitSubsystem::ResetDynamicView()
{
	DynamicConsumer.Reset();
	DynamicPortrait = nullptr;
	DynamicTilt = FVector2D::ZeroVector;
	DynamicFailure = FText::GetEmpty();
	bDynamicNeedsCapture = false;
	bDynamicReady = false;
}

UTextureRenderTarget2D* UCastleCardPortraitSubsystem::GetDynamicTexture(const UObject* Consumer, uint64* OutCaptureNumber) const
{
	const bool bReadyForConsumer = bDynamicReady && DynamicConsumer.Get() == Consumer;
	if (OutCaptureNumber != nullptr)
	{
		*OutCaptureNumber = bReadyForConsumer ? DynamicCaptureNumber : 0;
	}
	return bReadyForConsumer ? DynamicTexture.Get() : nullptr;
}

const FText* UCastleCardPortraitSubsystem::GetDynamicFailure(const UObject* Consumer) const
{
	return DynamicConsumer.Get() == Consumer && !DynamicFailure.IsEmpty() ? &DynamicFailure : nullptr;
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
	// 右边或下边向玩家靠近时，从建筑右侧或较低处看进去；相机位置与 View 的前向反向。
	const FRotator View(-20.0 + Tilt.Y * 5.5, -34.0 - Tilt.X * 10.0, 0.0);
	Camera->SetWorldLocationAndRotation(StageCenter - View.Vector() * StageDistance, View);
	Camera->TextureTarget = Target;
	Camera->CaptureScene();
	return true;
}

void UCastleCardPortraitSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (Now < NextCaptureAt)
	{
		return;
	}
	const int32 PendingIndex = FindPendingCaptureIndex();
	const bool bIdleCapture = PendingIndex != INDEX_NONE;
	UCastleCardPortrait* Portrait = bIdleCapture ? PendingPortraits[PendingIndex].Get()
		: (HasDynamicCaptureRequest() ? DynamicPortrait.Get() : nullptr);
	if (Portrait == nullptr)
	{
		return;
	}
	NextCaptureAt = Now + CardPortraitCaptureInterval;
	FText Reason;
	bool bCanCapture = StagePortrait(Portrait, Reason);
	if (bCanCapture)
	{
		// 轮询网格与材质的异步编译。超时前保留明确的排队状态，终止后由卡牌呈现失败。
		bCanCapture = StagedModel->ArePreviewRenderResourcesReady(Reason);
		if (!bCanCapture && Reason.IsEmpty())
		{
			if (Now - GeometryWaitStartedAt < GeometryWaitSeconds) { return; }
			Reason = INVTEXT("预览网格或材质在 30 秒内未就绪。");
		}
	}

	if (bIdleCapture)
	{
		PendingPortraits.RemoveAt(PendingIndex, EAllowShrinking::No);
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

	bDynamicNeedsCapture = false;
	if (bCanCapture)
	{
		if (DynamicTexture == nullptr)
		{
			DynamicTexture = NewPortraitTarget(this);
		}
		if (CaptureInto(DynamicTexture, DynamicTilt, Reason))
		{
			++DynamicCaptureNumber;
			bDynamicReady = true;
			return;
		}
	}
	// 动态成像失败时不拿另一张卡的图或旧动态帧继续呈现，向当前卡公开失败原因。
	bDynamicReady = false;
	DynamicFailure = Reason;
	UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡牌 %s Lv%d 互动视角捕获失败：%s"),
		*GetPathNameSafe(Portrait->Descriptor.Definition.Get()), Portrait->Descriptor.Level, *Reason.ToString());
	DropStage();
}

bool UCastleCardPortraitSubsystem::IsTickable() const
{
	return Super::IsTickable() && (FindPendingCaptureIndex() != INDEX_NONE || HasDynamicCaptureRequest());
}

int32 UCastleCardPortraitSubsystem::FindPendingCaptureIndex() const
{
	// 拆除菜单优先用自己的待拍牌；只有商店可见时才继续拍摄其货架批次。
	return PendingPortraits.IndexOfByPredicate([this](const TObjectPtr<UCastleCardPortrait>& Pending)
	{
		if (VisibleShop.IsValid()) { return true; }
		for (const auto& Pair : VisibleStandalonePortraits)
		{
			if (Pair.Key.IsValid() && Pair.Value.Get() == Pending.Get()) { return true; }
		}
		return false;
	});
}

bool UCastleCardPortraitSubsystem::HasDynamicCaptureRequest() const
{
	return bDynamicNeedsCapture && DynamicFailure.IsEmpty() && DynamicConsumer.IsValid()
		&& (VisibleShop.IsValid() || VisibleStandalonePortraits.Contains(DynamicConsumer))
		&& IsValid(DynamicPortrait) && DynamicPortrait->State == ECastleCardPortraitState::Ready;
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
	VisibleStandalonePortraits.Reset();
	ResetDynamicView();
	DropStage();
	Camera = nullptr;
	Studio.Reset();
	PendingPortraits.Reset();
	Portraits.Reset();
	DynamicTexture = nullptr;
	Super::Deinitialize();
}
