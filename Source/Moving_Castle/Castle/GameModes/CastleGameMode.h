// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CastleGameMode.generated.h"

/**
 * 移动城堡的自有 GameMode。
 *
 * 这里明确指定项目自己的 PlayerController、飞行 Pawn 和 HUD，后续敌人、NPC
 * 和战斗系统都可以在这个生命周期入口继续扩展，而不依赖第三人称模板类。
 */
UCLASS()
class MOVING_CASTLE_API ACastleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/** 配置移动城堡的默认控制对象。 */
	ACastleGameMode();
};
