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
	UPROPERTY(EditAnywhere, Category="死亡分裂") int32 SpawnCount = 3;
	UPROPERTY(EditAnywhere, Category="死亡分裂") float SpawnSpreadRadius = 80.0f;
protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
		FAdditionalTraitRequirements& OutTraitRequirements) const override;
};
