#include "AerialTerrainBackdrop.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "RHIGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogAerialTerrain, Log, All);

namespace
{
    // 约 30 m/像素，细节分布交给独立的地表材质。半精度线性格式保留水线附近的高差。
    constexpr int32 FieldResolution = 2048;

    // 切到下一周期时仍取同一地貌，负向航行也保持在 [0,1) 内。
    FVector2D WrapMapUV(const FVector2D& UV)
    {
        return FVector2D(UV.X - FMath::FloorToDouble(UV.X), UV.Y - FMath::FloorToDouble(UV.Y));
    }

    const FName FieldTextureParameter(TEXT("TerrainField"));
    const FName SeedParameter(TEXT("TerrainSeed"));
    const FName TravelParameter(TEXT("SamplingOffset"));
    const FName GeometryParameter(TEXT("GeometrySettings"));
    const FName SunParameter(TEXT("SunAndHazeStart"));
    const FName HazeParameter(TEXT("HazeAndFadeEnd"));
}

AAerialTerrainBackdrop::AAerialTerrainBackdrop()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;

    // 固定网格专用于渲染。近处关卡的射线、导航、光照和阴影均独立于远景。
    BackdropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackdropMesh"));
    SetRootComponent(BackdropMesh);
    BackdropMesh->SetVisibility(false);
    BackdropMesh->SetMobility(EComponentMobility::Static);
    BackdropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BackdropMesh->SetGenerateOverlapEvents(false);
    BackdropMesh->SetCanEverAffectNavigation(false);
    BackdropMesh->SetCastShadow(false);
    BackdropMesh->SetAffectDistanceFieldLighting(false);
    BackdropMesh->bAffectDynamicIndirectLighting = false;
    BackdropMesh->bVisibleInRealTimeSkyCaptures = false;
    BackdropMesh->SetVisibleInRayTracing(false);
}

bool AAerialTerrainBackdrop::ValidateConfiguration() const
{
    const UStaticMesh* Mesh = BackdropMesh->GetStaticMesh();
    if (!FApp::CanEverRender() || !Mesh || !FieldMaterial || !SurfaceMaterial)
    {
        UE_LOG(LogAerialTerrain, Error,
            TEXT("%s: 高空地表初始化失败。需要可渲染的世界、静态网格 %s、生成材质 %s 和地表材质 %s。"),
            *GetPathName(), *GetPathNameSafe(Mesh), *GetPathNameSafe(FieldMaterial), *GetPathNameSafe(SurfaceMaterial));
        return false;
    }

    // 横向尺度直接来自静态网格 UV。限制变换能保证航向仍以世界东/北为基准。
    const FRotator Rotation = GetActorRotation();
    if (!Rotation.IsNearlyZero() || !GetActorScale3D().Equals(FVector::OneVector) ||
        !FMath::IsFinite(MapPeriodCm) || MapPeriodCm <= 0.0 ||
        !FMath::IsFinite(HeightScaleCm) || HeightScaleCm <= 0.0f ||
        TerrainSeed < 0 || TerrainSeed > 65535 ||
        !FMath::IsFinite(InitialMapUV.X) || !FMath::IsFinite(InitialMapUV.Y) ||
        !FMath::IsFinite(HeadingDegrees) || !FMath::IsFinite(TravelSpeedCmPerSecond) || TravelSpeedCmPerSecond <= 0.0 ||
        SunDirection.ContainsNaN() || SunDirection.IsNearlyZero() || SunDirection.Z <= 0.0 ||
        !FMath::IsFinite(HazeStartCm) || !FMath::IsFinite(FadeEndCm) || HazeStartCm <= 0.0f || FadeEndCm <= HazeStartCm)
    {
        UE_LOG(LogAerialTerrain, Error,
            TEXT("%s: 高空地表参数无效。网格 %s 需要零旋转和单位缩放，地貌尺度、航程、太阳方向及雾距离须在有效范围内。"),
            *GetPathName(), *GetPathNameSafe(Mesh));
        return false;
    }

    const FBox Bounds = Mesh->GetBoundingBox();
    // 已知静态面片位于 Z=0，地貌函数输出的可见高度幅度小于 HeightScaleCm。
    // 包围盒须覆盖位移，并给地平线淡出留下连续的圆形网格。
    if (Bounds.Max.Z < HeightScaleCm || Bounds.Min.Z > -HeightScaleCm ||
        Bounds.Max.X < FadeEndCm || -Bounds.Min.X < FadeEndCm ||
        Bounds.Max.Y < FadeEndCm || -Bounds.Min.Y < FadeEndCm)
    {
        UE_LOG(LogAerialTerrain, Error, TEXT("%s: 网格 %s 的包围盒不足以覆盖位移或远端淡出。"),
            *GetPathName(), *GetPathNameSafe(Mesh));
        return false;
    }

    // 在创建资源前检查材质接口，避免 MID 静默丢弃拼错或遗失的参数。
    float SeedValue = 0.0f;
    FLinearColor VectorValue;
    UTexture* TextureValue = nullptr;
    if (!FieldMaterial->GetScalarParameterValue(FMaterialParameterInfo(SeedParameter), SeedValue) ||
        !SurfaceMaterial->GetTextureParameterValue(FMaterialParameterInfo(FieldTextureParameter), TextureValue) ||
        !Cast<UTextureRenderTarget2D>(TextureValue) ||
        !SurfaceMaterial->GetVectorParameterValue(FMaterialParameterInfo(TravelParameter), VectorValue) ||
        !SurfaceMaterial->GetVectorParameterValue(FMaterialParameterInfo(GeometryParameter), VectorValue) ||
        !SurfaceMaterial->GetVectorParameterValue(FMaterialParameterInfo(SunParameter), VectorValue) ||
        !SurfaceMaterial->GetVectorParameterValue(FMaterialParameterInfo(HazeParameter), VectorValue))
    {
        UE_LOG(LogAerialTerrain, Error, TEXT("%s: 材质缺少地貌数据参数或 TerrainField 未声明为二维渲染目标，生成材质 %s，地表材质 %s。"),
            *GetPathName(), *GetPathNameSafe(FieldMaterial), *GetPathNameSafe(SurfaceMaterial));
        return false;
    }
    return true;
}

void AAerialTerrainBackdrop::BeginPlay()
{
    Super::BeginPlay();
    if (!ValidateConfiguration())
    {
        return;
    }

    // 提前暴露材质配置和已知编译错误。按需编译允许着色器图尚未完成所有排列；
    // 烘焙调用会准备 Canvas 实际用到的排列，不能用全排列完成状态判断资产失效。
    const EShaderPlatform Platform = GetFeatureLevelShaderPlatform_Checked(GetWorld()->GetFeatureLevel());
    for (UMaterialInterface* Material : { FieldMaterial.Get(), SurfaceMaterial.Get() })
    {
#if WITH_EDITOR
        Material->EnsureIsComplete();
#endif
        const FMaterialResource* Resource = Material->GetMaterialResource(Platform);
        if (!Resource)
        {
            UE_LOG(LogAerialTerrain, Error, TEXT("%s: 材质 %s 缺少当前平台的材质资源，地表未初始化。"),
                *GetPathName(), *GetPathNameSafe(Material));
            return;
        }
#if WITH_EDITOR
        if (!Resource->GetCompileErrors().IsEmpty())
        {
            UE_LOG(LogAerialTerrain, Error, TEXT("%s: 材质 %s 编译失败，地表未初始化：%s。"),
                *GetPathName(), *GetPathNameSafe(Material), *FString::Join(Resource->GetCompileErrors(), TEXT("；")));
            return;
        }
#else
        // 独立游戏从已烹饪的包中加载排列，不应在运行时使用默认材质替代无法加载的地貌材质。
        if (!Resource->HasValidGameThreadShaderMap())
        {
            UE_LOG(LogAerialTerrain, Error, TEXT("%s: 材质 %s 缺少已烹饪的着色器，地表未初始化。"),
                *GetPathName(), *GetPathNameSafe(Material));
            return;
        }
#endif
    }

    TerrainField = NewObject<UTextureRenderTarget2D>(this, TEXT("AerialTerrainField"), RF_Transient);
    TerrainField->RenderTargetFormat = RTF_RGBA16f;
    TerrainField->AddressX = TA_Wrap;
    TerrainField->AddressY = TA_Wrap;
    TerrainField->Filter = TF_Bilinear;
    TerrainField->ClearColor = FLinearColor::Black;
    TerrainField->InitAutoFormat(FieldResolution, FieldResolution);
    TerrainField->UpdateResourceImmediate(true);
    if (!TerrainField->GameThread_GetRenderTargetResource())
    {
        UE_LOG(LogAerialTerrain, Error, TEXT("%s: 生成材质 %s 的高空地表渲染目标创建失败。"),
            *GetPathName(), *GetPathNameSafe(FieldMaterial));
        return;
    }

    BakeInstance = UMaterialInstanceDynamic::Create(FieldMaterial, this);
    SurfaceInstance = UMaterialInstanceDynamic::Create(SurfaceMaterial, this);
    if (!BakeInstance || !SurfaceInstance)
    {
        UE_LOG(LogAerialTerrain, Error, TEXT("%s: 无法实例化生成材质 %s 或地表材质 %s。"),
            *GetPathName(), *GetPathNameSafe(FieldMaterial), *GetPathNameSafe(SurfaceMaterial));
        return;
    }

    // 唯一的地貌生成 pass；后续各帧对同一张数据图平移采样，无需回读 GPU。
    BakeInstance->SetScalarParameterValue(SeedParameter, static_cast<float>(TerrainSeed));
    UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, TerrainField, BakeInstance);

    const FBox MeshBounds = BackdropMesh->GetStaticMesh()->GetBoundingBox();
    const double MeshDiameter = MeshBounds.Max.X - MeshBounds.Min.X;
    const double HeadingRadians = FMath::DegreesToRadians(HeadingDegrees);
    const FVector2D Heading(FMath::Cos(HeadingRadians), FMath::Sin(HeadingRadians));
    TravelPeriodsPerSecond = Heading * (TravelSpeedCmPerSecond / MapPeriodCm);
    SamplingOffset = WrapMapUV(InitialMapUV);
    const FVector Sun = SunDirection.GetUnsafeNormal();

    SurfaceInstance->SetTextureParameterValue(FieldTextureParameter, TerrainField);
    SurfaceInstance->SetVectorParameterValue(GeometryParameter,
        FLinearColor(static_cast<float>(MeshDiameter / MapPeriodCm), HeightScaleCm,
            static_cast<float>(HeightScaleCm / MapPeriodCm), 1.0f / FieldResolution));
    SurfaceInstance->SetVectorParameterValue(SunParameter,
        FLinearColor(static_cast<float>(Sun.X), static_cast<float>(Sun.Y), static_cast<float>(Sun.Z), HazeStartCm));
    SurfaceInstance->SetVectorParameterValue(HazeParameter, FLinearColor(HazeColor.R, HazeColor.G, HazeColor.B, FadeEndCm));
    PushSamplingOffset();
    BackdropMesh->SetMaterial(0, SurfaceInstance);
    BackdropMesh->SetVisibility(true);
    SetActorTickEnabled(true);

    UE_LOG(LogAerialTerrain, Log, TEXT("%s: 已提交 GPU 地貌生成，种子 %d，地貌周期 %.2f km，航向 %.1f 度，航速 %.1f m/s。"),
        *GetPathName(), TerrainSeed, MapPeriodCm / 100000.0, HeadingDegrees, TravelSpeedCmPerSecond / 100.0);
}

void AAerialTerrainBackdrop::PushSamplingOffset() const
{
    SurfaceInstance->SetVectorParameterValue(TravelParameter,
        FLinearColor(static_cast<float>(SamplingOffset.X), static_cast<float>(SamplingOffset.Y), 0.0f, 0.0f));
}

void AAerialTerrainBackdrop::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // 双精度追踪每一帧的航程，材质只接收一个有界的二维偏移。
    SamplingOffset = WrapMapUV(SamplingOffset + TravelPeriodsPerSecond * DeltaSeconds);
    PushSamplingOffset();
}
