// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleDirectorConfigDataAsset.h"

#include "MassEntityConfigAsset.h"
#include "Engine/StaticMesh.h"
#include "Blueprint/UserWidget.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Moving_Castle.h"

bool UCastleDirectorConfigDataAsset::ValidateConfig(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	const FString AssetName = GetPathName();
	const auto Reject = [this, &OutFailureReason, &AssetName](const TCHAR* Field, const FString& Value)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("配置资产 %s 字段 %s 值 %s 无效。"), *AssetName, Field, *Value));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
		return false;
	};
	if (NodeCountMin < 2 || NodeCountMin > NodeCountMax || NodeCountMax > 3)
	{
		return Reject(TEXT("NodeCountMin/NodeCountMax"), FString::Printf(TEXT("%d/%d"), NodeCountMin, NodeCountMax));
	}
	if (EliteEveryNSteps < 2) { return Reject(TEXT("EliteEveryNSteps"), FString::FromInt(EliteEveryNSteps)); }
	if (!FMath::IsFinite(NodeRingRadius) || NodeRingRadius <= 0.0f) { return Reject(TEXT("NodeRingRadius"), FString::SanitizeFloat(NodeRingRadius)); }
	if (!FMath::IsFinite(NodeHeightOffset)) { return Reject(TEXT("NodeHeightOffset"), FString::SanitizeFloat(NodeHeightOffset)); }
	if (!FMath::IsFinite(NodeArcDegrees) || NodeArcDegrees <= 0.0f || NodeArcDegrees > 180.0f)
	{
		return Reject(TEXT("NodeArcDegrees"), FString::SanitizeFloat(NodeArcDegrees));
	}
	if (!FMath::IsFinite(NodeTriggerRadius) || NodeTriggerRadius <= 0.0f) { return Reject(TEXT("NodeTriggerRadius"), FString::SanitizeFloat(NodeTriggerRadius)); }
	if (!IsValid(NodeMesh)) { return Reject(TEXT("NodeMesh"), TEXT("None")); }
	if (!FMath::IsFinite(NodeMeshScale) || NodeMeshScale <= 0.0f) { return Reject(TEXT("NodeMeshScale"), FString::SanitizeFloat(NodeMeshScale)); }
	if (!IsValid(NPCConfiguration)) { return Reject(TEXT("NPCConfiguration"), TEXT("None")); }
	if (NPCCount < 1 || NPCCount > 128) { return Reject(TEXT("NPCCount"), FString::FromInt(NPCCount)); }
	if (RewardExp < 1) { return Reject(TEXT("RewardExp"), FString::FromInt(RewardExp)); }
	if (ExpPerLevel < 1) { return Reject(TEXT("ExpPerLevel"), FString::FromInt(ExpPerLevel)); }
	if (BaseRoomLimit < 1) { return Reject(TEXT("BaseRoomLimit"), FString::FromInt(BaseRoomLimit)); }
	if (RoomLimitPerLevel < 0) { return Reject(TEXT("RoomLimitPerLevel"), FString::FromInt(RoomLimitPerLevel)); }
	if (!FMath::IsFinite(HealFraction) || HealFraction <= 0.0f || HealFraction > 1.0f)
	{
		return Reject(TEXT("HealFraction"), FString::SanitizeFloat(HealFraction));
	}
	if (!HUDWidgetClass) { return Reject(TEXT("HUDWidgetClass"), TEXT("None")); }
	if (!RewardWidgetClass) { return Reject(TEXT("RewardWidgetClass"), TEXT("None")); }
	if (!GameOverWidgetClass) { return Reject(TEXT("GameOverWidgetClass"), TEXT("None")); }
	if (!NodeLabelWidgetClass) { return Reject(TEXT("NodeLabelWidgetClass"), TEXT("None")); }
	if (!IsValid(CombatConfig)) { return Reject(TEXT("CombatConfig"), TEXT("None")); }
	return true;
}
