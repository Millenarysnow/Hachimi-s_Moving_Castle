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
	/** 设计坐标表中的 14 个切图按发布脚本的顺序保存，由菜单地图随 Cook 引用。 */
	UPROPERTY(EditAnywhere, Category="开始界面") TArray<TObjectPtr<UTexture2D>> MenuTextures;
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
