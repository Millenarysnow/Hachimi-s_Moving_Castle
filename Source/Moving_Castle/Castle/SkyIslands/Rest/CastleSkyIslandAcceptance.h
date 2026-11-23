#pragma once

#include "Castle/SkyIslands/CastleSkyIslandTypes.h"

#if !UE_BUILD_SHIPPING
class UWorld;

/** 3A 两条验收命令共用的入口。选择本步已经生成的节点，实际传送由导演执行。 */
void CastleQueueSkyIslandAcceptance(UWorld* World, ECastleSkyIslandType Type,
	TSubclassOf<ACastleSkyIslandBase> IslandClass);
#endif
