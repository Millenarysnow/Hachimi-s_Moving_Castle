#include "Items/CastleBadgeRuntimeSubsystem.h"

bool UCastleBadgeRuntimeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// 世界切换时随单局销毁，跨地图持有的徽章继续归 GameInstance 子系统。
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
