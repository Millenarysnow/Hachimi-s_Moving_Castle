#include "Components/Building/Effects/CastleSpeedRoomEffectComponent.h"

#include "Castle/Support/CastleSupportTowerSubsystem.h"
#include "Engine/World.h"
#include "Moving_Castle.h"

bool UCastleSpeedRoomEffectComponent::ExecuteEffect(const FCastleBuildingTriggerContext& Context)
{
	if (Context.Source == ECastleBuildingTriggerSource::Manual)
	{
		// 主动调用没有可施加倍率的对象，返回失败让基类恢复触发记录。
		UE_LOG(LogMoving_Castle, Warning, TEXT("加速房 %s 的主动调用没有 NPC，未施加加速。"),
			*GetPathNameSafe(GetOwner()));
		return false;
	}
	if (!FMath::IsFinite(SpeedMultiplier) || SpeedMultiplier <= 1.0f || !FMath::IsFinite(BuffDuration) || BuffDuration <= 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("加速房 %s 的倍率 %.2f 或持续时间 %.2f 无效。"),
			*GetPathNameSafe(GetOwner()), SpeedMultiplier, BuffDuration);
		return false;
	}

	UCastleSupportTowerSubsystem* Support = GetWorld()->GetSubsystem<UCastleSupportTowerSubsystem>();
	if (!Support)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("加速房 %s 无法访问当前世界的支援子系统。"), *GetPathNameSafe(GetOwner()));
		return false;
	}
	return Support->ApplyNPCMovementSpeedBoost(Context.NPC, SpeedMultiplier, BuffDuration);
}
