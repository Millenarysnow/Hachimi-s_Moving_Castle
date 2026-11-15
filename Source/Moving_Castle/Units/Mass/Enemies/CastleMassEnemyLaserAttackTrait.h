#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "CastleMassEnemyLaserAttackTrait.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** 熔岩幼骸持续激光能力的模板配置；所有数值在模板构建前严格校验。 */
UCLASS(meta=(DisplayName="Castle Mass 敌人持续激光攻击"))
class MOVING_CASTLE_API UCastleMassEnemyLaserAttackTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="激光攻击", meta=(ClampMin="0.0")) float AttackDamage = 5.0f;
	UPROPERTY(EditAnywhere, Category="激光攻击", meta=(ClampMin="0.01", Units="s")) float AttackInterval = 1.0f;
	UPROPERTY(EditAnywhere, Category="激光攻击", meta=(ClampMin="0.01", Units="cm")) float AttackRange = 800.0f;
	UPROPERTY(EditAnywhere, Category="激光攻击") FVector MuzzleLocalOffset = FVector(25.0f, 0.0f, 65.0f);
	UPROPERTY(EditAnywhere, Category="激光显示", meta=(ClampMin="0.01", Units="cm")) float BeamThickness = 10.0f;
	UPROPERTY(EditAnywhere, Category="激光显示") TObjectPtr<UStaticMesh> BeamMesh;
	UPROPERTY(EditAnywhere, Category="激光显示") TObjectPtr<UMaterialInterface> BeamMaterial;
protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
		FAdditionalTraitRequirements& OutTraitRequirements) const override;
};
