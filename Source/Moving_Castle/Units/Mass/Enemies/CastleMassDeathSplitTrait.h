#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "CastleMassDeathSplitTrait.generated.h"

class UMassEntityConfigAsset;

/** 熔岩巨骸专用的可选死亡能力，幼骸不装配此 Trait。 */
UCLASS(meta=(DisplayName="Castle Mass 死亡分裂"))
class MOVING_CASTLE_API UCastleMassDeathSplitTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="死亡分裂") TObjectPtr<UMassEntityConfigAsset> ChildConfiguration;
	UPROPERTY(EditAnywhere, Category="死亡分裂", meta=(ClampMin="1", ClampMax="64")) int32 SpawnCount = 3;
	UPROPERTY(EditAnywhere, Category="死亡分裂", meta=(ClampMin="0.01", ClampMax="5000.0", Units="cm")) float SpawnSpreadRadius = 80.0f;
	UPROPERTY(EditAnywhere, Category="死亡分裂", meta=(ClampMin="0.01", ClampMax="5.0", Units="s")) float BurstDuration = 0.35f;
	UPROPERTY(EditAnywhere, Category="死亡分裂", meta=(ClampMin="0.0", ClampMax="1000.0", Units="cm")) float BurstArcHeight = 60.0f;
protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
		FAdditionalTraitRequirements& OutTraitRequirements) const override;
};
