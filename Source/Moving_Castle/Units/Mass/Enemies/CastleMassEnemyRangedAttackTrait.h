#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "CastleMassEnemyRangedAttackTrait.generated.h"

class UMassEntityConfigAsset;

/** 瘴翼龙崽等敌人的远程能力；射程内优先攻击护盾，否则攻击唯一主塔。 */
UCLASS(meta=(DisplayName="Castle Mass 敌人远程攻击"))
class MOVING_CASTLE_API UCastleMassEnemyRangedAttackTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="远程") float AttackDamage = 15.0f;
	UPROPERTY(EditAnywhere, Category="远程") float AttackInterval = 1.25f;
	UPROPERTY(EditAnywhere, Category="远程") float AttackRange = 2200.0f;
	/** 必须显式配置敌方追踪弹，不借用玩家箭矢或普通子弹。 */
	UPROPERTY(EditAnywhere, Category="远程") TObjectPtr<UMassEntityConfigAsset> ProjectileConfiguration;
protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
		FAdditionalTraitRequirements& OutTraitRequirements) const override;
};
