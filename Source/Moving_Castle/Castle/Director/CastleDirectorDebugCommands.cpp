// Copyright Epic Games, Inc. All Rights Reserved.
#include "CastleMainTowerComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Moving_Castle.h"
#if !UE_BUILD_SHIPPING
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/SkyIslands/CastleSkyIslandTypes.h"
#endif
#include <cstdlib>

namespace
{
	/** 对当前世界的主塔施加明确指定的伤害，供主塔流程验收。 */
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

#if !UE_BUILD_SHIPPING
	/** 控制台只解析明确的类型，经当前世界的导演执行完整进出岛流程。 */
	FAutoConsoleCommandWithWorldAndArgs EnterSkyIslandCommand(
		TEXT("CastleEnterSkyIsland"), TEXT("CastleEnterSkyIsland <Rest|Shop|NPC|Gamble>：从编辑阶段进入指定空岛。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const auto* Types = StaticEnum<ECastleSkyIslandType>();
			const int64 TypeValue = Args.Num() == 1 ? Types->GetValueByNameString(Args[0]) : INDEX_NONE;
			FText Reason;
			auto* Director = World ? World->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
			bool bEntered = false;
			if (TypeValue < static_cast<int64>(ECastleSkyIslandType::Shop) || TypeValue > static_cast<int64>(ECastleSkyIslandType::Rest)
				|| !Args[0].Equals(Types->GetNameStringByValue(TypeValue), ESearchCase::CaseSensitive))
			{
				Reason = INVTEXT("用法：CastleEnterSkyIsland <Rest|Shop|NPC|Gamble>。");
				UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *Reason.ToString());
			}
			else if (!Director)
			{
				Reason = INVTEXT("CastleEnterSkyIsland 需要具有导演子系统的游戏世界。");
				UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *Reason.ToString());
			}
			else
			{
				bEntered = Director->TryEnterSkyIslandByTypeForDebug(static_cast<ECastleSkyIslandType>(TypeValue), Reason);
			}
			// 成功提示由导演统一发布。命令自身的参数和世界错误也要让 PIE 用户看见。
			if (!bEntered && GEngine)
			{
				GEngine->AddOnScreenDebugMessage(INDEX_NONE, 6.0f, FColor::Red, Reason.ToString());
			}
		}));
#endif
}
