#pragma once

#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "CastleSpeedRoomEffectComponent.generated.h"

/** 加速房使用原生 NPC 进入事件，通过支援倍率体系为该 NPC 单独加速。 */
UCLASS(ClassGroup = (Castle), BlueprintType)
class MOVING_CASTLE_API UCastleSpeedRoomEffectComponent : public UCastleBuildingEffectComponent
{
	GENERATED_BODY()

public:
	/** 每次进入登记的移动速度倍率，必须为大于 1 的有限数。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "建筑|加速房")
	float SpeedMultiplier = 1.5f;

	/** 从本次进入开始计时的游戏世界秒数，必须为有限正数。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "建筑|加速房")
	float BuffDuration = 5.0f;

protected:
	virtual bool ExecuteEffect(const FCastleBuildingTriggerContext& Context) override;
};
