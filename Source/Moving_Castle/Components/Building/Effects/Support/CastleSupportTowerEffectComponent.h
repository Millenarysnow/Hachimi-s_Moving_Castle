#pragma once

#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "CastleSupportTowerEffectComponent.generated.h"

/** 进入事件可能由 Mass 派发；这里只提交请求，不直接生成 Actor 或新实体。 */
UCLASS(ClassGroup=(Castle))
class MOVING_CASTLE_API UCastleSupportTowerEffectComponent : public UCastleBuildingEffectComponent
{
	GENERATED_BODY()
protected:
	virtual bool ExecuteEffect(const FCastleBuildingTriggerContext& Context) override;
};
