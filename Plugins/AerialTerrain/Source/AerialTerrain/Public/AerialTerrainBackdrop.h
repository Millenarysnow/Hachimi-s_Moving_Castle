#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AerialTerrainBackdrop.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/** 固定在水平世界坐标中的远景面片。航程只影响材质采样，不参与关卡逻辑。 */
UCLASS(NotBlueprintable)
class AERIALTERRAIN_API AAerialTerrainBackdrop final : public AActor
{
    GENERATED_BODY()

public:
    AAerialTerrainBackdrop();
    virtual void Tick(float DeltaSeconds) override;

    /** 读取自本次航行开始以来未回绕的世界 XY 航程（厘米）。地表未初始化时不写输出。 */
    bool TryGetTraveledDistanceCm(FVector2D& OutDistanceCm) const;

protected:
    virtual void BeginPlay() override;

private:
    // 显式填写烘焙与显示资源。材质参数名由插件自有材质与本类共同维护。
    UPROPERTY(VisibleAnywhere, Category="高空地表")
    TObjectPtr<UStaticMeshComponent> BackdropMesh;

    UPROPERTY(EditAnywhere, Category="高空地表")
    TObjectPtr<UMaterialInterface> FieldMaterial;

    UPROPERTY(EditAnywhere, Category="高空地表")
    TObjectPtr<UMaterialInterface> SurfaceMaterial;

    // 16 位非负整数种子可精确传给 GPU，便于在同一地貌中更改出场位置而不重新选择种子。
    UPROPERTY(EditAnywhere, Category="地貌")
    int32 TerrainSeed = 12021;

    UPROPERTY(EditAnywhere, Category="地貌", meta=(Units="cm"))
    double MapPeriodCm = 6144000.0;

    UPROPERTY(EditAnywhere, Category="地貌", meta=(Units="cm"))
    float HeightScaleCm = 235000.0f;

    // 单位是完整地貌周期；关卡可据此选定出场时看到的海岸或内陆。
    UPROPERTY(EditAnywhere, Category="航程")
    FVector2D InitialMapUV = FVector2D(0.42, 0.31);

    // 世界水平航向以 +X 为零度。向 +X 推进采样坐标，地貌在视野中沿 -X 后退。
    UPROPERTY(EditAnywhere, Category="航程", meta=(Units="deg"))
    double HeadingDegrees = 45.0;

    UPROPERTY(EditAnywhere, Category="航程", meta=(Units="cm/s"))
    double TravelSpeedCmPerSecond = 16500.0;

    // 朝向光源的向量，供地表材质按高度差着色；地图可配合当地的太阳光方向布置。
    UPROPERTY(EditAnywhere, Category="远景着色")
    FVector SunDirection = FVector(-0.64, 0.12, 0.76);

    UPROPERTY(EditAnywhere, Category="远景着色", meta=(Units="cm"))
    float HazeStartCm = 850000.0f;

    UPROPERTY(EditAnywhere, Category="远景着色", meta=(Units="cm"))
    float FadeEndCm = 3050000.0f;

    UPROPERTY(EditAnywhere, Category="远景着色")
    FLinearColor HazeColor = FLinearColor(0.43f, 0.61f, 0.78f);

    // 每个运行世界各持有一张线性数据图，多个 PIE 实例之间也没有可写共享资产。
    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> TerrainField;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> BakeInstance;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> SurfaceInstance;

    /** 检查必填资产、材质契约和可见范围，保证只在整套数据准备好之后显示。 */
    bool ValidateConfiguration() const;
    void PushSamplingOffset() const;

    // 地表出场 UV 只选地貌，不属于航程；使用独立周期的背景层可以共享同一个世界位移。
    FVector2D TraveledDistanceCm = FVector2D::ZeroVector;
    FVector2D TravelVelocityCmPerSecond = FVector2D::ZeroVector;
    bool bTravelInitialized = false;
};
