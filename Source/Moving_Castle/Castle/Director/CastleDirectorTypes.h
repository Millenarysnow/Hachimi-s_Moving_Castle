// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "CastleDirectorTypes.generated.h"

/** 单局流程仅由导演修改；Inactive 表示本世界沿用原有原型行为。 */
UENUM(BlueprintType)
enum class ECastleDirectorPhase : uint8
{
	Inactive,
	Editing,
	Battle,
	Reward,
	GameOver
};

/** 战斗与空岛共用节点表现，导演根据节点类型进入相应阶段。 */
UENUM(BlueprintType)
enum class ECastleDirectorNodeKind : uint8
{
	Normal,
	Elite,
	RewardIsland,
	RestIsland
};

/** 奖励意图由按钮提交，实际效果统一在导演执行。 */
UENUM(BlueprintType)
enum class ECastleDirectorReward : uint8
{
	Experience,
	Healing,
	Shop
};

/** 升级面板的一行奖励，数字、NPC 和万能卡各自独立领取。 */
enum class ECastleTowerUpgradeReward : uint8
{
	PathPoint,
	NPC,
	CustomRoom
};

/** 前两种奖励保存数量，自定义房每张占一个项目。 */
struct FCastleTowerUpgradeRewardEntry
{
	ECastleTowerUpgradeReward Kind;
	int32 Count;
};
