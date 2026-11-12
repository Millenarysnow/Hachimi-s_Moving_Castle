#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "CastleMassMeleeAttackTrait.generated.h"

/** 可选的直线追塔与近身攻击能力；具体近战兵种在 DA 中显式添加，不能由敌人基类默认装配。 */
UCLASS(meta=(DisplayName="Castle Mass 近战追塔攻击"))
class MOVING_CASTLE_API UCastleMassMeleeAttackTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	/** 主塔每次受到的伤害，允许测试兵配置为 0。 */
	UPROPERTY(EditAnywhere, Category="近战|攻击", meta=(ClampMin="0.0"))
	float AttackDamage = 0.0f;

	/** 连续攻击间隔，必须是正数。 */
	UPROPERTY(EditAnywhere, Category="近战|攻击", meta=(ClampMin="0.01", Units="s"))
	float AttackInterval = 1.0f;

	/** 进入该三维距离后停止追踪并攻击主塔。 */
	UPROPERTY(EditAnywhere, Category="近战|攻击", meta=(ClampMin="0.0", Units="cm"))
	float AttackRange = 150.0f;

protected:
	/** 依赖通用敌人身份和移动数据，只向模板增加独立的近战状态。 */
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

	/** 阻止非法配置进入实体模板，不用默认数值掩盖配置错误。 */
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
		FAdditionalTraitRequirements& OutTraitRequirements) const override;
};
