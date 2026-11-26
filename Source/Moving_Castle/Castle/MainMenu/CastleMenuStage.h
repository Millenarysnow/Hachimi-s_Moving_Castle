#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleMenuStage.generated.h"

class UFont;
class UMaterialInterface;
class UTexture2D;
class UCastleMenuModel;

/** 独立菜单舞台。模型随转轴运动，摄影机、布光和纸面保持在世界或屏幕空间。 */
UCLASS()
class ACastleMenuStage : public AActor
{
	GENERATED_BODY()
public:
	ACastleMenuStage();
	/** 三项资产均由菜单地图明确引用，随地图 Cook。 */
	UPROPERTY(EditAnywhere, Category="开始界面") TObjectPtr<UTexture2D> TitleTexture;
	UPROPERTY(EditAnywhere, Category="开始界面") TObjectPtr<UFont> MenuFont;
	UPROPERTY(EditAnywhere, Category="开始界面") TObjectPtr<UMaterialInterface> PaperMaterial;

	bool ShowModel(const UCastleMenuModel& Model, FString& Error);
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	virtual void Tick(float DeltaSeconds) override;
private:
	UPROPERTY() TObjectPtr<USceneComponent> Pivot;
	/** 已提交模型的全周回转圆柱，尺寸变化只重算相机、不重新采样存档。 */
	double SweepRadius = 0.0;
	double HalfHeight = 0.0;
	double SpinAngle = 0.0;
};
