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

/** 本期节点只有两种战斗；同一步中的节点类型完全相同。 */
UENUM(BlueprintType)
enum class ECastleDirectorNodeKind : uint8
{
	Normal,
	Elite
};

/** 奖励意图由按钮提交，实际效果统一在导演执行。 */
UENUM(BlueprintType)
enum class ECastleDirectorReward : uint8
{
	Experience,
	Healing,
	Shop
};
