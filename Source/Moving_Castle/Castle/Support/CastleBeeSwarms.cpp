#include "CastleSupportTowerSubsystem.h"
#include "Audio/CastleAudioSubsystem.h"

#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleSupportFlightActors.h"
#include "CastleSupportTowerData.h"
#include "Engine/World.h"
#include "Items/CastleBadgeRuntimeSubsystem.h"
#include "Moving_Castle.h"

namespace
{
	constexpr double AttackWindow = 10.0;
	constexpr int32 SwarmBoostThreshold = 12;

	/** 选当前生命，不用最大生命；同血量按 Mass 实体序号确定次序。 */
	const FCastleSupportUnit* HighestHealth(TConstArrayView<FCastleSupportUnit> Enemies, const FMassEntityHandle Excluded)
	{
		const FCastleSupportUnit* Result = nullptr;
		for (const FCastleSupportUnit& Enemy : Enemies)
		{
			if (Enemy.Health > 0.0f && Enemy.Handle.Entity != Excluded && (!Result || Enemy.Health > Result->Health)) { Result = &Enemy; }
		}
		return Result;
	}
}

bool UCastleSupportTowerSubsystem::BuildHiveSchedule(const ACastleBoard& Board, const ACastleBuildingBase& Source,
	const int64 SourceNumber, FBeeSchedule& OutSchedule) const
{
	struct FVisit { const ACastleBuildingBase* Building; int32 Step; };
	TArray<FVisit> Queue{{ &Source, 0 }};
	// 一个房间可从两条支路进入（一次递增、一次递减）。保留两种搜索状态，否则可能错过后续分支。
	TSet<const ACastleBuildingBase*> Ascending;
	TSet<const ACastleBuildingBase*> Descending;
	TSet<const ACastleBuildingBase*> Emitted;
	Emitted.Add(&Source);
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const FVisit Visit = Queue[Head];
		TArray<ACastleBuildingBase*> Neighbors;
		if (!Board.GetFaceAdjacentBuildings(Visit.Building, true, Neighbors)) { return false; }
		for (ACastleBuildingBase* Neighbor : Neighbors)
		{
			if (Neighbor == &Source) { continue; }
			const int64 CurrentNumber = Visit.Building == &Source ? SourceNumber : Visit.Building->GetEffectiveBuildingNumber();
			const int64 Difference = Neighbor->GetEffectiveBuildingNumber() - CurrentNumber;
			// 只有主巢接出口允许同号；进入备用链后不能再经过同号或改变递增/递减方向。
			if (Visit.Building == &Source ? FMath::Abs(Difference) > 1
				: (FMath::Abs(Difference) != 1 || (Visit.Step != 0 && Difference != Visit.Step))) { continue; }
			if (Difference == 0)
			{
				Queue.Add({Neighbor, 0});
			}
			else
			{
				TSet<const ACastleBuildingBase*>& Seen = Difference > 0 ? Ascending : Descending;
				if (Seen.Contains(Neighbor)) { continue; }
				Seen.Add(Neighbor);
				Queue.Add({Neighbor, static_cast<int32>(Difference)});
			}
			if (!Emitted.Contains(Neighbor))
			{
				Emitted.Add(Neighbor);
				OutSchedule.Backups.Add(Neighbor);
			}
		}
	}
	return true;
}

bool UCastleSupportTowerSubsystem::LaunchSwarm(FSwarm&& Batch, const FVector& Start, const UCastleSupportTowerData& Data)
{
	ACastleBeeSwarmVisual* Visual = GetWorld()->SpawnActor<ACastleBeeSwarmVisual>(Start, FRotator::ZeroRotator);
	if (!Visual || !Visual->Configure(Batch.Count, Data.FlightMaterial))
	{
		if (Visual) { Visual->Destroy(); }
		UE_LOG(LogMoving_Castle, Error, TEXT("蜂巢 %s 生成 %d 只蜜蜂失败，支援资产=%s。"),
			*GetPathNameSafe(Batch.Source.Get()), Batch.Count, *Data.GetPathName());
		return false;
	}
	Batch.Visual = Visual;
	UE_LOG(LogMoving_Castle, Display, TEXT("蜂巢 Lv%d %s：%s出动 %d 只。"), Batch.Level, *GetNameSafe(Batch.Source.Get()),
		Batch.Schedule ? (Batch.Source == Batch.Home ? TEXT("主巢") : TEXT("备用巢")) : TEXT("蜂群"), Batch.Count);
	Swarms.Add(MoveTemp(Batch));
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Bees);
	return true;
}

void UCastleSupportTowerSubsystem::TickSwarms(const float DeltaTime, TArray<FCastleSupportUnit>& Enemies)
{
	if (Swarms.IsEmpty()) { return; }
	UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
	auto* Runtime = GetWorld()->GetSubsystem<UCastleBadgeRuntimeSubsystem>();
	if (!Runtime)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("蜂群世界 %s 缺少本局徽章子系统，无法结算攻击。"), *GetPathNameSafe(GetWorld()));
		return;
	}
	TMap<FMassEntityHandle, FCastleSupportUnit*> ByHandle;
	for (auto& Enemy : Enemies) { ByHandle.Add(Enemy.Handle.Entity, &Enemy); }
	TMap<FMassEntityHandle, int64> AttachedCount;
	TMap<FMassEntityHandle, int32> BoostingBatches;
	TArray<FSwarm> NextBatches;
	TArray<FVector> NextLocations;
	const float FlightMultiplier = GetFlightMultiplier();
	auto SetTarget = [](FSwarm& Batch, const FCastleSupportUnit* Enemy)
	{
		Batch.Target = Enemy ? Enemy->Handle.Entity : FMassEntityHandle();
		Batch.AttackSeconds = 0.0;
		Batch.HitProgress = 0.0;
		Batch.bAttached = false;
		Batch.bReturning = Enemy == nullptr;
		if (Enemy)
		{
			UE_LOG(LogMoving_Castle, Display, TEXT("蜂巢 %s：%d 只锁定敌人 %d，当前血量 %.2f。"),
				*GetNameSafe(Batch.Source.Get()), Batch.Count, Batch.Target.Index, Enemy->Health);
		}
	};
	for (FSwarm& Batch : Swarms)
	{
		if (!Batch.Source.IsValid() || !Batch.Home.IsValid() || !Batch.Visual.IsValid())
		{
			// 回收及合成会结束旧实例的支援；不把蜂群或备用链移交给新塔。
			if (ACastleBeeSwarmVisual* Visual = Batch.Visual.Get()) { Visual->Destroy(); }
			UE_LOG(LogMoving_Castle, Display, TEXT("蜂巢 %s 或归巢对象已离场，结束该批蜂群。"), *GetNameSafe(Batch.Source.Get()));
			Batch.Visual.Reset();
			continue;
		}
		FCastleSupportUnit* Target = ByHandle.FindRef(Batch.Target);
		if (!Batch.bReturning && (!Target || Target->Health <= 0.0f))
		{
			SetTarget(Batch, HighestHealth(Enemies, {}));
			Target = ByHandle.FindRef(Batch.Target);
		}
		ACastleBeeSwarmVisual* Visual = Batch.Visual.Get();
		const FVector Destination = Batch.bReturning ? Batch.HomeLocation : Target->Location;
		const double Speed = Batch.Speed * FlightMultiplier;
		const double TimeToAttach = Batch.bAttached ? 0.0 : FVector::Dist(Visual->GetActorLocation(), Destination) / Speed;
		// 已附着的群随目标移动；飞行途中以真实速度追踪，尚未抵达的不计入 12 只阈值。
		const FVector NewPosition = Batch.bAttached ? Destination : FMath::VInterpConstantTo(Visual->GetActorLocation(), Destination,
			DeltaTime, Speed);
		Visual->SetActorLocation(NewPosition);
		Visual->AddActorWorldRotation(FRotator(0.0, 135.0 * DeltaTime, 0.0));
		if (!NewPosition.Equals(Destination, 0.01)) { continue; }
		if (Batch.bReturning)
		{
			UE_LOG(LogMoving_Castle, Display, TEXT("蜂巢 %s：%d 只已全部返回 %s。"),
				*GetNameSafe(Batch.Source.Get()), Batch.Count, *GetNameSafe(Batch.Home.Get()));
			Visual->Destroy();
			Batch.Visual.Reset();
			if (Batch.Schedule && Batch.Schedule->Backups.IsValidIndex(Batch.Schedule->Next))
			{
				ACastleBuildingBase* Next = Batch.Schedule->Backups[Batch.Schedule->Next].Get();
				ACastleBoard* Board = ACastleBoard::FindBoardForBuilding(Next);
				if (!Board || !Next->CanReceiveBuildingTrigger())
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("蜂巢 %s 备用队列第 %d 座已失效，整条余链结束。"),
						*GetPathNameSafe(Batch.Source.Get()), Batch.Schedule->Next + 1);
					continue;
				}
				const int64 Count = Next->GetEffectiveBuildingNumber();
				if (Count > MAX_int32)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("蜂巢备用房 %s 的结算蜂数 %lld 超出批次容量，终止本次备用链。"),
						*Next->GetPathName(), Count);
					continue;
				}
				FSwarm& Backup = NextBatches.AddDefaulted_GetRef();
				Backup.Source = Batch.Source;
				Backup.Home = Next;
				Backup.Schedule = Batch.Schedule;
				Backup.Count = static_cast<int32>(Count);
				Backup.Level = Batch.Level;
				Backup.Damage = Batch.Damage;
				Backup.Speed = Batch.Speed;
				Backup.HomeLocation = Next->GetActorLocation() + Board->GetActorUpVector() * (Board->LayerHeight + 80.0);
				NextLocations.Add(Backup.HomeLocation);
				++Batch.Schedule->Next;
			}
			continue;
		}
		Batch.bAttached = true;
		// 跨帧抵达只结算实际贴在目标上的时间，不能把路上的半秒当成攻击。
		Batch.AttachedDelta = FMath::Max(0.0, DeltaTime - TimeToAttach);
		AttachedCount.FindOrAdd(Batch.Target) += Batch.Count;
		if (Batch.Level >= 2) { ++BoostingBatches.FindOrAdd(Batch.Target); }
	}

	for (FSwarm& Batch : Swarms)
	{
		if (!Batch.Visual.IsValid() || !Batch.bAttached) { continue; }
		FCastleSupportUnit* Target = ByHandle.FindChecked(Batch.Target);
		if (Target->Health <= 0.0f) { SetTarget(Batch, HighestHealth(Enemies, {})); continue; }
		const double Step = FMath::Min(Batch.AttachedDelta, AttackWindow - Batch.AttackSeconds);
		const bool bBoosted = AttachedCount.FindChecked(Batch.Target) > SwarmBoostThreshold && BoostingBatches.FindRef(Batch.Target) > 0;
		Batch.AttackSeconds += Step;
		Batch.HitProgress += Step * (bBoosted ? 4.0 : 2.0);
		while (Batch.HitProgress + UE_DOUBLE_SMALL_NUMBER >= 1.0 && Target->Health > 0.0f)
		{
			const float Damage = Batch.Count * Batch.Damage * GetTowerDamageMultiplier(*Batch.Source.Get());
			if (!Units->ApplyImmediateDamageToEnemy(Target->Handle, Damage))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("蜂巢 %s 对实体 %d 的伤害结算失败，本批结束。"),
					*GetPathNameSafe(Batch.Source.Get()), Batch.Target.Index);
				Batch.Visual->Destroy();
				Batch.Visual.Reset();
				break;
			}
			Target->Health = FMath::Max(0.0f, Target->Health - Damage);
			Batch.HitProgress -= 1.0;
			// 同帧下一批蜂群已看到零生命，无法重复领取本次真实击杀的玩法金币。
			if (Target->Health == 0.0f && !Runtime->TryAwardBeeKill()) { return; }
			if (Target->Health == 0.0f && Batch.Level == 4 && Batch.Count >= 2)
			{
				// 一次实际致死只繁殖一次，产生位置取被杀实体，而不是塔中心或蜂群视觉中心。
				FSwarm& Child = NextBatches.AddDefaulted_GetRef();
				Child.Source = Batch.Source;
				Child.Home = Batch.Home;
				Child.HomeLocation = Batch.HomeLocation;
				Child.Level = Batch.Level;
				Child.Count = Batch.Count / 2;
				Child.Damage = Batch.Damage;
				Child.Speed = Batch.Speed;
				if (const FCastleSupportUnit* Victim = HighestHealth(Enemies, {})) { Child.Target = Victim->Handle.Entity; }
				NextLocations.Add(Target->Location);
				UE_LOG(LogMoving_Castle, Display, TEXT("蜂巢 %s 击杀敌人 %d，孵化 %d 只。"),
					*GetNameSafe(Batch.Source.Get()), Target->Handle.Entity.Index, Child.Count);
			}
		}
		if (!Batch.Visual.IsValid()) { continue; }
		if (Target->Health == 0.0f || Batch.AttackSeconds >= AttackWindow)
		{
			AttachedCount.FindChecked(Batch.Target) -= Batch.Count;
			if (Batch.Level >= 2) { --BoostingBatches.FindChecked(Batch.Target); }
			const bool bCanRetarget = Target->Health == 0.0f || Batch.Level >= 2;
			SetTarget(Batch, bCanRetarget ? HighestHealth(Enemies, Batch.Target) : nullptr);
		}
	}
	Swarms.RemoveAll([](const FSwarm& Batch) { return !Batch.Visual.IsValid(); });
	for (int32 Index = 0; Index < NextBatches.Num(); ++Index)
	{
		FSwarm& Next = NextBatches[Index];
		const auto* Data = Next.Source->GetBuildingDefinition()->SupportData.Get();
		LaunchSwarm(MoveTemp(Next), NextLocations[Index], *Data);
	}
}
