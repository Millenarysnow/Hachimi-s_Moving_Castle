#include "AerialCloudscape.h"

#include "AerialTerrainBackdrop.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/VolumeTexture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "RHIGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogAerialCloudscape, Log, All);

namespace
{
    const FName PhaseParameter(TEXT("CloudTravelPhase"));
    const FName PeriodParameter(TEXT("CloudPeriodKm"));
    const FName CoverageParameter(TEXT("CloudCoverage"));
    const FName ShapeParameter(TEXT("CloudShape"));
    const FName DensityParameter(TEXT("CloudExtinction"));
    const FName FadeStartParameter(TEXT("CloudFadeStartKm"));
    const FName FadeEndParameter(TEXT("CloudFadeEndKm"));
    const FName BandLowerParameter(TEXT("CloudBandLower"));
    const FName BandUpperParameter(TEXT("CloudBandUpper"));
    const FName SunlitTintParameter(TEXT("CloudSunlitTint"));
    const FName InteriorTintParameter(TEXT("CloudInteriorTint"));
    constexpr double CentimetersPerKilometer = 100000.0;

    // 编辑器实例可以覆盖配色，运行时拒绝不在线性反照率范围内的三个通道。
    bool IsValidCloudAlbedo(const FLinearColor& Color)
    {
        return Color.R >= 0.0f && Color.R <= 1.0f && Color.G >= 0.0f && Color.G <= 1.0f
            && Color.B >= 0.0f && Color.B <= 1.0f;
    }

    // 负方向也从 [0,1) 进入材质，细节纹理每个完整云周期重复整数次。
    double WrapCloudPhase(double UnwrappedPhase)
    {
        return UnwrappedPhase - FMath::FloorToDouble(UnwrappedPhase);
    }
}

AAerialCloudscape::AAerialCloudscape()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
}

void AAerialCloudscape::BeginPlay()
{
    Super::BeginPlay();

    if (!IsValid(CloudLayer) || CloudLayer->GetWorld() != GetWorld())
    {
        StopCloud(TEXT("未指定本运行世界的原生体积云 Actor"));
        return;
    }

    CloudComponent = CloudLayer->FindComponentByClass<UVolumetricCloudComponent>();
    if (!IsValid(CloudComponent) || !CloudComponent->IsRegistered() || !CloudComponent->IsVisible() ||
        !CloudComponent->bRenderInMainPass || CloudLayer->IsHidden())
    {
        StopCloud(TEXT("指定的体积云没有可见且已注册的云组件"));
        return;
    }

    if (!IsValid(TravelSource) || TravelSource->GetWorld() != GetWorld())
    {
        StopCloud(TEXT("未指定本运行世界的地表航程源"));
        return;
    }

    // 两个驱动会分别写材质相位。绑定冲突必须显式失败，不依据 Actor 初始化顺序选一个。
    for (TActorIterator<AAerialCloudscape> Driver(GetWorld()); Driver; ++Driver)
    {
        if (*Driver != this && Driver->CloudLayer == CloudLayer)
        {
            const FString Reason = FString::Printf(TEXT("%s 与本驱动重复绑定了同一个体积云 Actor"), *Driver->GetPathName());
            StopCloud(*Reason);
            return;
        }
    }

    for (TActorIterator<AVolumetricCloud> Actor(GetWorld()); Actor; ++Actor)
    {
        const UVolumetricCloudComponent* Other = Actor->FindComponentByClass<UVolumetricCloudComponent>();
        if (Other && Other != CloudComponent && Other->IsRegistered() && Other->IsVisible() && !Actor->IsHidden())
        {
            const FString Reason = FString::Printf(TEXT("%s 也启用了原生体积云，两个组件不能同时作为云层"), *Actor->GetPathName());
            StopCloud(*Reason);
            return;
        }
    }

    if (!InitializeMaterial())
    {
        return;
    }

    // BeginPlay 的 Actor 顺序并不确定；首次读航程安排在地表 Tick 之后。
    AddTickPrerequisiteActor(TravelSource);
    SetActorTickEnabled(true);
}

bool AAerialCloudscape::InitializeMaterial()
{
    UMaterialInterface* Material = CloudComponent->GetMaterial();
    const UMaterial* BaseMaterial = IsValid(Material) ? Material->GetMaterial() : nullptr;
    if (!FApp::CanEverRender() || !BaseMaterial || BaseMaterial->MaterialDomain != MD_Volume ||
        !Material->GetUsageByFlag(MATUSAGE_VolumetricCloud))
    {
        StopCloud(TEXT("需要可渲染世界和显式启用体积云用法的 Volume 材质"));
        return false;
    }

    FLinearColor PhaseValue;
    UTexture* Coverage = nullptr;
    UTexture* Shape = nullptr;
    float PeriodKm = 0.0f;
    float Extinction = 0.0f;
    float FadeStartKm = 0.0f;
    float FadeEndKm = 0.0f;
    if (!Material->GetVectorParameterValue(FMaterialParameterInfo(PhaseParameter), PhaseValue) ||
        !Material->GetTextureParameterValue(FMaterialParameterInfo(CoverageParameter), Coverage) || !Cast<UTexture2D>(Coverage) ||
        !Material->GetTextureParameterValue(FMaterialParameterInfo(ShapeParameter), Shape) || !Cast<UVolumeTexture>(Shape) ||
        !Material->GetScalarParameterValue(FMaterialParameterInfo(PeriodParameter), PeriodKm) || !FMath::IsFinite(PeriodKm) || PeriodKm <= 0.0f ||
        !Material->GetScalarParameterValue(FMaterialParameterInfo(DensityParameter), Extinction) || !FMath::IsFinite(Extinction) || Extinction <= 0.0f ||
        !Material->GetScalarParameterValue(FMaterialParameterInfo(FadeStartParameter), FadeStartKm) || !FMath::IsFinite(FadeStartKm) ||
        !Material->GetScalarParameterValue(FMaterialParameterInfo(FadeEndParameter), FadeEndKm) || !FMath::IsFinite(FadeEndKm) ||
        FadeStartKm <= 0.0f || FadeEndKm <= FadeStartKm)
    {
        StopCloud(TEXT("材质缺少云相位、周期、消光、覆盖图、三维形状图，或远景参数无效"));
        return false;
    }

    float BandLower = 0.0f;
    float BandUpper = 0.0f;
    FLinearColor SunlitTint;
    FLinearColor InteriorTint;
    if (!Material->GetScalarParameterValue(FMaterialParameterInfo(BandLowerParameter), BandLower) ||
        !Material->GetScalarParameterValue(FMaterialParameterInfo(BandUpperParameter), BandUpper) ||
        !(BandLower >= 0.0f && BandLower < BandUpper && BandUpper <= 1.0f) ||
        !Material->GetVectorParameterValue(FMaterialParameterInfo(SunlitTintParameter), SunlitTint) || !IsValidCloudAlbedo(SunlitTint) ||
        !Material->GetVectorParameterValue(FMaterialParameterInfo(InteriorTintParameter), InteriorTint) || !IsValidCloudAlbedo(InteriorTint))
    {
        StopCloud(*FString::Printf(TEXT("材质 %s 的云顶、云腹反照率或软色阶上下界无效"), *Material->GetPathName()));
        return false;
    }

    const UTexture2D* WeatherMap = CastChecked<UTexture2D>(Coverage);
    const UVolumeTexture* ShapeVolume = CastChecked<UVolumeTexture>(Shape);
    if (WeatherMap->SRGB || WeatherMap->AddressX != TA_Wrap || WeatherMap->AddressY != TA_Wrap ||
        ShapeVolume->SRGB || ShapeVolume->AddressMode != TA_Wrap || ShapeVolume->GetSizeZ() <= 1)
    {
        const FString Reason = FString::Printf(TEXT("覆盖图 %s 和形状体 %s 必须是线性周期数据且拥有真实体积切片"),
            *WeatherMap->GetPathName(), *ShapeVolume->GetPathName());
        StopCloud(*Reason);
        return false;
    }

    // 只接受在着色器自然淡出之后终止追踪的配置，否则斜看会出现随镜头移动的硬边。
    if (!FMath::IsFinite(CloudComponent->LayerBottomAltitude) || CloudComponent->LayerBottomAltitude <= 0.0f ||
        !FMath::IsFinite(CloudComponent->LayerHeight) || CloudComponent->LayerHeight <= 0.0f ||
        !FMath::IsFinite(CloudComponent->TracingStartMaxDistance) || CloudComponent->TracingStartMaxDistance <= FadeEndKm ||
        CloudComponent->TracingMaxDistanceMode != EVolumetricCloudTracingMaxDistanceMode::DistanceFromPointOfView ||
        !FMath::IsFinite(CloudComponent->TracingMaxDistance) || CloudComponent->TracingMaxDistance <= FadeEndKm)
    {
        StopCloud(TEXT("云层海拔或厚度无效，或原生追踪没有按视点距离覆盖材质的完整淡出范围"));
        return false;
    }

    const EShaderPlatform Platform = GetFeatureLevelShaderPlatform_Checked(GetWorld()->GetFeatureLevel());
#if WITH_EDITOR
    Material->EnsureIsComplete();
#endif
    const FMaterialResource* Resource = Material->GetMaterialResource(Platform);
    if (!Resource)
    {
        StopCloud(TEXT("材质缺少当前平台的着色资源"));
        return false;
    }
#if WITH_EDITOR
    if (!Resource->GetCompileErrors().IsEmpty())
    {
        const FString Reason = FString::Printf(TEXT("材质编译失败：%s"), *FString::Join(Resource->GetCompileErrors(), TEXT("；")));
        StopCloud(*Reason);
        return false;
    }
#else
    if (!Resource->HasValidGameThreadShaderMap())
    {
        StopCloud(TEXT("材质缺少当前平台已烹煮的着色器"));
        return false;
    }
#endif

    // 首次取得航程前不把 MID 绑定到代理，避免在同一渲染帧提交两次初始材质。
    CloudPeriodCm = static_cast<double>(PeriodKm) * CentimetersPerKilometer;
    CloudInstance = UMaterialInstanceDynamic::Create(Material, this);
    if (!IsValid(CloudInstance))
    {
        StopCloud(TEXT("无法创建本世界的动态体积材质"));
        return false;
    }
    return true;
}

void AAerialCloudscape::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!IsValid(CloudComponent) || !IsValid(CloudInstance))
    {
        StopCloud(TEXT("运行中的云组件或动态体积材质失效"));
        return;
    }

    FVector2D TravelCm;
    if (!IsValid(TravelSource) || !TravelSource->TryGetTraveledDistanceCm(TravelCm) ||
        !FMath::IsFinite(TravelCm.X) || !FMath::IsFinite(TravelCm.Y))
    {
        StopCloud(TEXT("指定的地表尚未建立有效航程，无法滚动云密度"));
        return;
    }

    if (bCloudBound && CloudComponent->GetMaterial() != CloudInstance)
    {
        StopCloud(TEXT("运行中云组件的材质被其他系统替换，无法继续独占驱动"));
        return;
    }

    const FVector2D Phase(WrapCloudPhase(TravelCm.X / CloudPeriodCm), WrapCloudPhase(TravelCm.Y / CloudPeriodCm));
    CloudInstance->SetVectorParameterValue(PhaseParameter, FLinearColor(Phase.X, Phase.Y, 0.0f, 0.0f));
    if (!bCloudBound)
    {
        CloudComponent->SetMaterial(CloudInstance);
        if (CloudComponent->GetMaterial() != CloudInstance)
        {
            StopCloud(TEXT("原生云组件拒绝绑定动态体积材质"));
            return;
        }
        bCloudBound = true;
        UE_LOG(LogAerialCloudscape, Log, TEXT("%s: 已启用高空云海 %s，地表航程源 %s，云密度周期 %.2f km。"),
            *GetPathName(), *CloudComponent->GetPathName(), *TravelSource->GetPathName(), CloudPeriodCm / CentimetersPerKilometer);
    }
}

void AAerialCloudscape::StopCloud(const TCHAR* Reason)
{
    const UMaterialInterface* Material = IsValid(CloudComponent) ? CloudComponent->GetMaterial() : nullptr;
    UE_LOG(LogAerialCloudscape, Error, TEXT("%s: 停止高空云海。云层 %s，航程源 %s，材质 %s：%s。"),
        *GetPathName(), *GetPathNameSafe(CloudLayer), *GetPathNameSafe(TravelSource), *GetPathNameSafe(Material), Reason);
    // 错误时明确关闭自己的组件，不换回旧云，也不接管其他组件。
    if (IsValid(CloudComponent))
    {
        CloudComponent->SetVisibility(false);
    }
    SetActorTickEnabled(false);
}

void AAerialCloudscape::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    SetActorTickEnabled(false);
    if (IsValid(TravelSource))
    {
        RemoveTickPrerequisiteActor(TravelSource);
    }
    // 如果关卡脚本在运行中销毁驱动，短时间内也不要留下冻住的云。
    if (IsValid(CloudComponent) && CloudComponent->GetMaterial() == CloudInstance)
    {
        CloudComponent->SetVisibility(false);
    }
    Super::EndPlay(EndPlayReason);
}
