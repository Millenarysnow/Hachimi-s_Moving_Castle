// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleDirectorConfigDataAsset.h"

#include "MassEntityConfigAsset.h"
#include "Engine/StaticMesh.h"
#include "Blueprint/UserWidget.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Castle/UI/Director/CastleTowerUpgradeRewardWidget.h"
#include "Castle/UI/Items/CastleWildcardRoomPickerWidget.h"
#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Moving_Castle.h"
#include "CastleTowerDispatchConfigDataAsset.h"

UCastleDirectorConfigDataAsset::UCastleDirectorConfigDataAsset()
{
	// 等级表与策划给定的数字上限、开放层、容量、气球和赠送项一一对应。
	TowerLevels = {
		{1, 3, 3, 150, 5, 1000.0f, 0, 0, 0},
		{2, 4, 5, 250, 10, 2000.0f, 1, 2, 0},
		{3, 5, 7, 350, 20, 4000.0f, 1, 5, 0},
		{4, 6, 9, 450, 40, 8000.0f, 1, 10, 5}
	};
}

bool UCastleDirectorConfigDataAsset::ValidateTowerLevels(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	const auto Reject = [&](int32 Level)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("配置 %s 的成长表必须含连续四级且数值有效递增，失败行 %d。"),
			*GetPathName(), Level));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
		return false;
	};
	if (TowerLevels.Num() != 4 || InitialNPCCount < 1 || InitialPathPointLimit < 1 || InitialPathPointLimit > 99)
	{
		return Reject(0);
	}
	for (int32 Index = 0; Index < TowerLevels.Num(); ++Index)
	{
		const FCastleTowerLevelConfig& Row = TowerLevels[Index];
		if (Row.Level != Index + 1 || Row.MaxShopNumber < 1 || Row.MaxShopNumber > 9 || Row.UnlockedLayers < 1
			|| Row.UnlockedLayers > 9 || Row.RoomLimit < 1 || Row.BalloonCount < 1 || Row.BalloonCount > 100
			|| !FMath::IsFinite(Row.BaseMaxHealth) || Row.BaseMaxHealth <= 0.0f
			|| Row.PathPointReward < 0 || Row.CustomRoomReward < 0 || Row.NPCReward < 0)
		{
			return Reject(Index + 1);
		}
		if (Index == 0) { continue; }
		const FCastleTowerLevelConfig& Previous = TowerLevels[Index - 1];
		if (Row.MaxShopNumber <= Previous.MaxShopNumber || Row.UnlockedLayers <= Previous.UnlockedLayers
			|| Row.RoomLimit <= Previous.RoomLimit || Row.BalloonCount <= Previous.BalloonCount
			|| Row.BaseMaxHealth <= Previous.BaseMaxHealth)
		{
			return Reject(Index + 1);
		}
	}
	return true;
}

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
	const int64 NormalAndRewardWeight = static_cast<int64>(NormalNodeWeight) + RewardNodeWeight;
	const int64 TotalNodeWeight = NormalAndRewardWeight + RestNodeWeight;
	if (NormalNodeWeight < 0 || RewardNodeWeight < 0 || RestNodeWeight < 0 || TotalNodeWeight <= 0)
	{
		return Reject(TEXT("NodeWeights"), FString::Printf(TEXT("%d/%d/%d"), NormalNodeWeight, RewardNodeWeight, RestNodeWeight));
	}
	// 三节点有两个抽样位。首个抽样位抽到休息岛后，第二个必须仍有普通战斗或奖励空岛可抽。
	if (NodeCountMax >= 3 && NormalAndRewardWeight <= 0)
	{
		return Reject(TEXT("NodeWeights/NodeCountMax"), FString::Printf(
			TEXT("普通=%d 奖励=%d 休息=%d 最大节点数=%d（普通与奖励权重之和须大于零）"),
			NormalNodeWeight, RewardNodeWeight, RestNodeWeight, NodeCountMax));
	}
	if (!FMath::IsFinite(SkyIslandSpawnDistance) || SkyIslandSpawnDistance < 3000.0f || !FMath::IsFinite(SkyIslandHeightOffset))
	{
		return Reject(TEXT("SkyIslandSpawnDistance/SkyIslandHeightOffset"),
			FString::Printf(TEXT("%g/%g"), SkyIslandSpawnDistance, SkyIslandHeightOffset));
	}
	if (!SkyIslandInteractKey.IsValid()) { return Reject(TEXT("SkyIslandInteractKey"), SkyIslandInteractKey.ToString()); }
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
	if (!IsValid(DispatchConfig)) { return Reject(TEXT("DispatchConfig"), TEXT("None")); }
	if (!DispatchConfig->ValidateConfiguration(OutFailureReason)) { return false; }
	if (RewardExp < 1) { return Reject(TEXT("RewardExp"), FString::FromInt(RewardExp)); }
	if (ExpPerLevel < 1) { return Reject(TEXT("ExpPerLevel"), FString::FromInt(ExpPerLevel)); }
	if (!ValidateTowerLevels(OutFailureReason)) { return false; }
	if (!FMath::IsFinite(HealFraction) || HealFraction <= 0.0f || HealFraction > 1.0f)
	{
		return Reject(TEXT("HealFraction"), FString::SanitizeFloat(HealFraction));
	}
	if (!HUDWidgetClass) { return Reject(TEXT("HUDWidgetClass"), TEXT("None")); }
	if (!RewardWidgetClass) { return Reject(TEXT("RewardWidgetClass"), TEXT("None")); }
	if (!GameOverWidgetClass) { return Reject(TEXT("GameOverWidgetClass"), TEXT("None")); }
	if (!NodeLabelWidgetClass) { return Reject(TEXT("NodeLabelWidgetClass"), TEXT("None")); }
	if (!TowerUpgradeRewardWidgetClass) { return Reject(TEXT("TowerUpgradeRewardWidgetClass"), TEXT("None")); }
	if (!WildcardRoomPickerClass) { return Reject(TEXT("WildcardRoomPickerClass"), TEXT("None")); }
	if (!IsValid(CombatConfig)) { return Reject(TEXT("CombatConfig"), TEXT("None")); }
	return true;
}
