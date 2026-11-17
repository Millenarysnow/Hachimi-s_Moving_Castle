#include "CastleSupportTowerEffectComponent.h"

#include "CastleBuildingBase.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"
#include "Engine/World.h"
#include "Moving_Castle.h"

bool UCastleSupportTowerEffectComponent::ExecuteEffect(const FCastleBuildingTriggerContext& Context)
{
	ACastleBuildingBase* Building = Cast<ACastleBuildingBase>(GetOwner());
	UCastleSupportTowerSubsystem* Support = GetWorld() ? GetWorld()->GetSubsystem<UCastleSupportTowerSubsystem>() : nullptr;
	if (!IsValid(Building) || !Support)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("支援塔 %s 触发失败：建筑身份或世界支援子系统无效。"), *GetPathNameSafe(GetOwner()));
		return false;
	}
	return Support->QueueLaunch(*Building);
}
