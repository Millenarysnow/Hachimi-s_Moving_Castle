// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleTowerDispatchConfigDataAsset.h"

#include "Moving_Castle.h"

bool UCastleTowerDispatchConfigDataAsset::ValidateConfiguration(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (InitialNPCCount < 1 || !FMath::IsFinite(HoldDispatchInterval) || HoldDispatchInterval < 0.02f
		|| !FMath::IsFinite(AutoDispatchInterval) || AutoDispatchInterval < 0.05f || !ToggleAutoDispatchKey.IsValid())
	{
		OutReason = FText::FromString(FString::Printf(TEXT("出兵配置 %s 无效：开局数量须至少为 1，长按与自动间隔须有限且分别至少 0.02 秒和 0.05 秒，快捷键须有效。"),
			*GetPathName()));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	return true;
}
