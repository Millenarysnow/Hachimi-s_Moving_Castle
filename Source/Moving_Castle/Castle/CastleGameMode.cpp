// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleGameMode.h"

#include "CastleFlightPawn.h"
#include "CastleHUD.h"
#include "CastlePlayerController.h"

ACastleGameMode::ACastleGameMode()
{
	// 运行时完全使用项目自己的控制器、飞行 Pawn 和 HUD。
	PlayerControllerClass = ACastlePlayerController::StaticClass();
	DefaultPawnClass = ACastleFlightPawn::StaticClass();
	HUDClass = ACastleHUD::StaticClass();
}
