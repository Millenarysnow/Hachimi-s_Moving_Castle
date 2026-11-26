#include "Items/CastleRoomTraitComponent.h"

#include "CastleBuildingDefinition.h"
#include "Moving_Castle.h"

bool UCastleRoomTraitComponent::TryInitializeTrait(const FCastleBuildingDescriptor& Descriptor, FText& OutReason)
{
	// 保留失败入口供放置事务接线，阶段内没有已初始化的特质组件。
	OutReason = INVTEXT("房间特质组件尚未实现（Phase 0）。");
	UE_LOG(LogMoving_Castle, Error, TEXT("建筑定义 %s 无法初始化房间特质（组件 %s）：%s"),
		*GetPathNameSafe(Descriptor.Definition.Get()), *GetPathName(), *OutReason.ToString());
	return false;
}

bool UCastleRoomTraitComponent::TryInteractWithPortal(FText& OutReason)
{
	// 传送流程实现前拒绝交互，尚未交付的传送不消耗房间特质。
	OutReason = INVTEXT("传送门房间交互尚未实现（Phase 0）。");
	UE_LOG(LogMoving_Castle, Error, TEXT("传送门组件 %s 无法交互：%s"), *GetPathName(), *OutReason.ToString());
	return false;
}
