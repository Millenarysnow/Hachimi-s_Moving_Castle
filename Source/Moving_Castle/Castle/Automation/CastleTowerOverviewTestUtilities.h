// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "Castle/Actors/CastleBoard.h"
#include "HAL/FileManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/OutputDeviceNull.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"

namespace CastleTowerOverviewTests
{
	/** 将验收截图保存在被 .gitignore 排除的 Saved/WorkerB/PIE 目录。 */
	inline void Capture(const TCHAR* Filename)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WorkerB/PIE"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString Path = FPaths::Combine(Directory, Filename);
		FScreenshotRequest::RequestScreenshot(Path, true, false, false);
	}

	/** 使用场景中的主塔和已开放塔层，独立核对飞行位置、俯仰与面向战场的朝向。 */
	inline void Check(FAutomationTestBase& Test, const ACastleBoard& Board, const APlayerController& Player)
	{
		const APawn* Pawn = Player.GetPawn();
		if (!Test.TestNotNull(TEXT("塔顶到达后的玩家 Pawn"), Pawn)
			|| !Test.TestNotNull(TEXT("塔顶参照的正式主塔"), Board.MainTowerActor.Get())) { return; }
		const FVector FromTower = Pawn->GetActorLocation() - Board.MainTowerActor->GetActorLocation();
		const FVector Behind = -Board.GetActorForwardVector().GetSafeNormal2D() * 1800.0;
		Test.TestTrue(TEXT("到达当前开放层顶面上方 2500 cm、塔后 1800 cm"),
			FVector(FromTower.X, FromTower.Y, 0.0).Equals(Behind, 5.0)
			&& FMath::IsNearlyEqual(FromTower.Z, Board.GetUnlockedLayerCount() * Board.LayerHeight + 2500.0, 5.0));
		const FRotator ExpectedView(-32.0, Board.GetActorRotation().Yaw, 0.0);
		Test.TestTrue(TEXT("到达后以 32 度俯角朝向塔前战场"), Player.GetControlRotation().Equals(ExpectedView, 1.0));
		Test.TestTrue(TEXT("到达时清除飞行速度"), Pawn->GetVelocity().IsNearlyZero(1.0));
	}
}
#endif
