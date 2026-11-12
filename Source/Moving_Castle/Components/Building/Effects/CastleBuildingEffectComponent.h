#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/Building/Effects/CastleBuildingTriggerInterface.h"
#include "CastleBuildingEffectComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCastleBuildingEffectTriggered, const FCastleBuildingTriggerContext&, Context);

/**
 * 每栋建筑独立持有的原生效果状态。基础组件提供真实触发计数和表现通知，
 * 具体攻击、产出应在 C++ ExecuteEffect 中扩展，不以动画忙碌或冷却吞掉进入事件。
 */
UCLASS(ClassGroup=(Castle), BlueprintType)
class MOVING_CASTLE_API UCastleBuildingEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCastleBuildingEffectComponent();

	/** 建筑接口的唯一转发目标；校验失败明确返回 false，不自动补建组件或更换来源。 */
	bool Trigger(const FCastleBuildingTriggerContext& Context);

	/** 运行实例的成功触发次数；同帧多人进入也分别递增，合成后新实例重新计数。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="建筑|效果")
	int64 TriggerCount = 0;

	/** 保留最近一次来源供观察；它不是拥有实体生命周期的强引用。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="建筑|效果")
	FCastleBuildingTriggerContext LastTriggerContext;

	/** 原生效果完成后通知声音、粒子和 UI；不参与进入判定或事件去重。 */
	UPROPERTY(BlueprintAssignable, Category="建筑|效果")
	FCastleBuildingEffectTriggered OnEffectTriggered;

protected:
	/** 具体效果的 C++ 扩展点。基础版本只提供计数，不偷偷赋予道路或塔未经设计的收益。 */
	virtual void ExecuteEffect(const FCastleBuildingTriggerContext& Context);
};
