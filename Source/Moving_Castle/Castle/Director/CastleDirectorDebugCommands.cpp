// Copyright Epic Games, Inc. All Rights Reserved.
#include "CastleMainTowerComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Moving_Castle.h"
#include <cstdlib>

namespace
{
	/** 唯一的导演验收控制台命令；不提供跳关、奖励或阶段修改的生产入口。 */
	FAutoConsoleCommandWithWorldAndArgs DamageMainTowerCommand(
		TEXT("CastleDamageMainTower"), TEXT("CastleDamageMainTower <有限正数>：伤害当前世界主塔。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			float Damage = 0.0f;
			bool bValid = false;
			if (World && Args.Num() == 1)
			{
				// 标准库返回结束指针；必须消费完整参数，不能把 100abc 猜成 100。
				const FTCHARToUTF8 EncodedInput(*Args[0]);
				char* End = nullptr;
				const char* Start = EncodedInput.Get();
				Damage = std::strtof(Start, &End);
				bValid = End != Start && End && *End == 0 && FMath::IsFinite(Damage) && Damage > 0.0f;
			}
			const bool bApplied = bValid && World->GetSubsystem<UCastleMainTowerSubsystem>()->ApplyMainTowerDamage(Damage);
			const FString Message = bApplied ? FString::Printf(TEXT("主塔已受到 %.2f 点伤害。"), Damage)
				: TEXT("CastleDamageMainTower 失败：参数必须是有限正数，且当前世界必须有存活主塔。");
			if (bApplied) { UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *Message); }
			else { UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *Message); }
			if (GEngine) { GEngine->AddOnScreenDebugMessage(2026111705, 6.0f, bApplied ? FColor::Cyan : FColor::Red, Message); }
		}));
}
