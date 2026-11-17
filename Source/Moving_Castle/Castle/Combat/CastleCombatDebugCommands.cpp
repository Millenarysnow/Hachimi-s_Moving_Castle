#include "Castle/Combat/CastleCombatSubsystem.h"

#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Units/Mass/CastleMassFragments.h"
#include "Units/Mass/CastleMassUnitSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Algo/AllOf.h"
#include "MassEntitySubsystem.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "String/LexFromString.h"
#include "Moving_Castle.h"

namespace CastleCombatDebug
{
	/** 同步显示控制台执行结果，缺配置或参数错误必须在日志和屏幕上都可见。 */
	void Feedback(const FString& Message, const bool bSuccess)
	{
		if (bSuccess) { UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *Message); }
		else { UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *Message); }
		if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 8.0f, bSuccess ? FColor::Green : FColor::Red, Message); }
	}

	FAutoConsoleCommandWithWorldAndArgs StartBattleCommand(TEXT("CastleStartBattle"),
		TEXT("CastleStartBattle <Normal|Elite> <序号>：按战斗配置开启一场战斗。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		auto* Combat = World ? World->GetSubsystem<UCastleCombatSubsystem>() : nullptr;
		int64 OrdinalValue = 0;
		if (!Combat || Args.Num() != 2 || (Args[0] != TEXT("Normal") && Args[0] != TEXT("Elite"))
			|| Args[1].IsEmpty() || !Args[1].IsNumeric() || !LexTryParseString(OrdinalValue, *Args[1]) || OrdinalValue < 1 || OrdinalValue > MAX_int32
			|| !Algo::AllOf(Args[1], [](const TCHAR C) { return C >= TEXT('0') && C <= TEXT('9'); }))
		{
			Feedback(TEXT("CastleStartBattle：需要 Game/PIE 世界；用法 CastleStartBattle <Normal|Elite> <正整数序号>。"), false);
			return;
		}
		const int32 Ordinal = static_cast<int32>(OrdinalValue);
		FText Failure;
		if (!Combat->IsCombatInitialized())
		{
			const TCHAR* Path = TEXT("/Game/MovingCastle/Data/Combat/DA_CombatConfig");
			auto* Config = LoadObject<UCastleCombatConfigDataAsset>(nullptr, Path);
			if (!Config) { Feedback(FString::Printf(TEXT("CastleStartBattle：配置资产 %s 缺失或加载失败。"), Path), false); return; }
			if (!Combat->InitializeCombat(Config, Failure)) { Feedback(Failure.ToString(), false); return; }
		}
		if (!Combat->StartBattle(Args[0] == TEXT("Normal") ? ECastleBattleKind::Normal : ECastleBattleKind::Elite, Ordinal, Failure))
		{
			Feedback(Failure.ToString(), false);
			return;
		}
		Feedback(FString::Printf(TEXT("CastleStartBattle：%s %d 开战成功。"), *Args[0], Ordinal), true);
	}));

	FAutoConsoleCommandWithWorldAndArgs KillAllCommand(TEXT("CastleKillAllEnemies"),
		TEXT("CastleKillAllEnemies：伤害当前世界全部存活敌人，保留正常分裂和回收。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		auto* Units = World ? World->GetSubsystem<UCastleMassUnitSubsystem>() : nullptr;
		auto* Mass = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
		if (!Args.IsEmpty() || !Units || !Mass || !IsInGameThread() || Mass->GetEntityManager().IsProcessing())
		{
			Feedback(TEXT("CastleKillAllEnemies：命令不接受参数，需要 Game/PIE 世界、游戏线程且 Mass 未处理。"), false);
			return;
		}
		auto& Manager = Mass->GetMutableEntityManager();
		TArray<FCastleMassUnitHandle> Enemies;
		bool bInvalidEnemy = false;
		FMassEntityQuery Query(Manager.AsShared());
		Query.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All);
		Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
		FMassExecutionContext Context(Manager, 0, false);
		Query.ForEachEntityChunk(Context, [World, &Enemies, &bInvalidEnemy](FMassExecutionContext& Chunk)
		{
			const auto Data = Chunk.GetFragmentView<FCastleMassUnitFragment>();
			if (Data.IsEmpty()) { bInvalidEnemy = true; return; }
			for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
			{
				if (!FMath::IsFinite(Data[It].Health)) { bInvalidEnemy = true; return; }
				// 命令的目标按 D13 定义就是存活敌人；已死亡者等待既有死亡链路，不重复施伤。
				if (Data[It].Health > 0)
				{
					auto& Enemy = Enemies.AddDefaulted_GetRef();
					Enemy.World = World;
					Enemy.Entity = Chunk.GetEntity(It);
				}
			}
		});
		if (bInvalidEnemy) { Feedback(TEXT("CastleKillAllEnemies：EnemyTag 实体缺少生命状态或生命非有限数，整批拒绝。"), false); return; }
		if (!Enemies.IsEmpty() && !Units->DamageUnits(Enemies, TNumericLimits<float>::Max()))
		{
			Feedback(TEXT("CastleKillAllEnemies：批量伤害被拒绝。"), false);
			return;
		}
		Feedback(FString::Printf(TEXT("CastleKillAllEnemies：本次处理 %d 只存活敌人，等待正常死亡、分裂及回收。"), Enemies.Num()), true);
	}));
}
