#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "AerialCloudscape.generated.h"

class AAerialTerrainBackdrop;
class AVolumetricCloud;
class UMaterialInstanceDynamic;
class UVolumetricCloudComponent;

/** 只驱动关卡明确指定的原生云组件；不创建渲染体、移动世界或介入玩法。 */
UCLASS(NotBlueprintable)
class AERIALCLOUDSCAPE_API AAerialCloudscape final : public AInfo
{
    GENERATED_BODY()

public:
    AAerialCloudscape();
    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /** 与地表共享世界 XY 航程，不能用地貌 UV 或玩家朝向代替。 */
    UPROPERTY(EditInstanceOnly, Category="高空云海")
    TObjectPtr<AAerialTerrainBackdrop> TravelSource;

    /** 使用这一原生云组件的海拔和材质，关卡中不得再启用另一份原生云。 */
    UPROPERTY(EditInstanceOnly, Category="高空云海")
    TObjectPtr<AVolumetricCloud> CloudLayer;

    UPROPERTY(Transient)
    TObjectPtr<UVolumetricCloudComponent> CloudComponent;

    // 运行材质属于这个世界，不能写回材质资产或改变其他 PIE 实例的航程。
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> CloudInstance;

    double CloudPeriodCm = 0.0;
    bool bCloudBound = false;

    bool InitializeMaterial();
    void StopCloud(const TCHAR* Reason);
};
