#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "MassMovableVisualizationTrait.h"
#include "CastleMassTypes.h"
#include "CastleMassTraits.generated.h"

class UAnimSequenceTransformProviderData;

/** 三类单位的公共组成。具体行为用 Tag 查询扩展，不为单个单位分配 UObject。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleMassUnitTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category="单位|移动")
	FCastleMassMovementSettings Movement;

	UPROPERTY(EditAnywhere, Category="单位", meta=(ClampMin="0.01"))
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, Category="单位", meta=(ClampMin="0.0", Units="cm"))
	float Radius = 35.0f;

	UPROPERTY(EditAnywhere, Category="单位")
	int32 Team = 0;

	UPROPERTY(EditAnywhere, Category="单位", meta=(ClampMin="0.0", Units="s"))
	float Lifetime = 0.0f;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
		FAdditionalTraitRequirements& OutTraitRequirements) const override;
	virtual ECastleMassUnitKind GetKind() const PURE_VIRTUAL(UCastleMassUnitTrait::GetKind, return ECastleMassUnitKind::Enemy;);
};

UCLASS(meta=(DisplayName="Castle Mass 敌人基类"))
class MOVING_CASTLE_API UCastleMassEnemyTrait : public UCastleMassUnitTrait
{
	GENERATED_BODY()
public:
	UCastleMassEnemyTrait();

	/** 敌人每次攻击主塔造成的伤害；原型基础敌人默认为 0。 */
	UPROPERTY(EditAnywhere, Category="敌人|攻击", meta=(ClampMin="0.0"))
	float AttackDamage = 0.0f;

	/** 同一敌人两次攻击之间的间隔，单位为秒。 */
	UPROPERTY(EditAnywhere, Category="敌人|攻击", meta=(ClampMin="0.01", Units="s"))
	float AttackInterval = 1.0f;

	/** 敌人停止移动并开始攻击的主塔距离，单位为厘米。 */
	UPROPERTY(EditAnywhere, Category="敌人|攻击", meta=(ClampMin="0.0", Units="cm"))
	float AttackRange = 150.0f;

protected:
	virtual ECastleMassUnitKind GetKind() const override { return ECastleMassUnitKind::Enemy; }
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
		FAdditionalTraitRequirements& OutTraitRequirements) const override;
};

UCLASS(meta=(DisplayName="Castle Mass NPC 基类"))
class MOVING_CASTLE_API UCastleMassNPCTrait : public UCastleMassUnitTrait
{
	GENERATED_BODY()
public:
	UCastleMassNPCTrait();
protected:
	virtual ECastleMassUnitKind GetKind() const override { return ECastleMassUnitKind::NPC; }
};

UCLASS(meta=(DisplayName="Castle Mass 子弹基类"))
class MOVING_CASTLE_API UCastleMassProjectileTrait : public UCastleMassUnitTrait
{
	GENERATED_BODY()
public:
	UCastleMassProjectileTrait();

	/** 向下的重力加速度；0 表示直线子弹。 */
	UPROPERTY(EditAnywhere, Category="子弹", meta=(ClampMin="0.0"))
	float Gravity = 0.0f;

	UPROPERTY(EditAnywhere, Category="子弹", meta=(ClampMin="0.0"))
	float Damage = 10.0f;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	virtual ECastleMassUnitKind GetKind() const override { return ECastleMassUnitKind::Projectile; }
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
		FAdditionalTraitRequirements& OutTraitRequirements) const override;
};

/** UE 5.8 原生 ISKM + GPU 动画序列提供器；近中远三个可见 LOD 都保持实例化骨骼显示。 */
UCLASS(meta=(DisplayName="Castle Mass 人形批量骨骼动画"))
class MOVING_CASTLE_API UCastleMassHumanoidVisualizationTrait : public UMassMovableVisualizationTrait
{
	GENERATED_BODY()
public:
	UCastleMassHumanoidVisualizationTrait();

	/** 提供器同时指定骨骼网格和序列集合，离线构建后可随项目 Cook。 */
	UPROPERTY(EditAnywhere, Category="人形动画")
	TObjectPtr<UAnimSequenceTransformProviderData> AnimationProvider;

	UPROPERTY(EditAnywhere, Category="人形动画", meta=(ClampMin="0"))
	int32 IdleSequence = 0;

	UPROPERTY(EditAnywhere, Category="人形动画", meta=(ClampMin="0"))
	int32 MoveSequence = 1;

	/** 移动序列的固定播放倍率，在切换到该序列时提交给 GPU。 */
	UPROPERTY(EditAnywhere, Category="人形动画", meta=(ClampMin="0.01"))
	float MovePlayRate = 1.0f;

	UPROPERTY(EditAnywhere, Category="人形动画", meta=(ClampMin="0.0", Units="s"))
	float BlendTime = 0.15f;

	/** 默认以脚底为实体原点，将 UE 模板人物的朝向转换为 +X。 */
	UPROPERTY(EditAnywhere, Category="人形动画")
	FTransform MeshOffset = FTransform(FRotator(0.0, -90.0, 0.0));

	UPROPERTY(EditAnywhere, Category="人形动画")
	bool bCastShadows = false;

	/** 0 使用引擎的屏幕尺寸阈值，负数允许远处实例始终播放动画。 */
	UPROPERTY(EditAnywhere, Category="人形动画")
	float AnimationMinScreenSize = 0.0f;

protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
	virtual bool ValidateTemplate(const FMassEntityTemplateBuildContext& BuildContext, const UWorld& World,
		FAdditionalTraitRequirements& OutTraitRequirements) const override;
};
