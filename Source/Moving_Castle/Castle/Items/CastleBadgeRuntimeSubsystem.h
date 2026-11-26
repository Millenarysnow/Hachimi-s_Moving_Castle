#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "CastleBadgeRuntimeSubsystem.generated.h"

/** 单局与单波徽章状态的归属。Phase 1B 在游戏线程接入事件并向 Mass 提供数值快照。 */
UCLASS()
class MOVING_CASTLE_API UCastleBadgeRuntimeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

protected:
	/** 仅游戏和 PIE 世界具备独立的单局事件与结算状态。 */
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
};
