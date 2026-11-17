#include "CastleShieldTowerSubsystem.h"
#include "CastleBuildingBase.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"
#include "Components/ActorComponent.h"

#include "CastleMassFragments.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Moving_Castle.h"

bool UCastleShieldTowerSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

uint64 UCastleShieldTowerSubsystem::UpsertActiveShield(UObject* OwnerKey, const FVector& WorldLocation,
	float AttractionRadius, int64 RemainingBlocks, float ReflectDamage)
{
	if (!IsInGameThread() || !IsValid(OwnerKey) || WorldLocation.ContainsNaN()
		|| !FMath::IsFinite(AttractionRadius) || AttractionRadius <= 0.0f || RemainingBlocks <= 0
		|| !FMath::IsFinite(ReflectDamage) || ReflectDamage < 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("激活护盾失败：%s，须在游戏线程提供有效对象、有限坐标、正吸引半径/次数及非负有限反伤。"), *GetPathNameSafe(OwnerKey));
		return 0;
	}
	uint64 Id = 0;
	for (const auto& Pair : ActiveShields)
	{
		if (Pair.Value.Owner.Get() == OwnerKey) { Id = Pair.Key; break; }
	}
	if (Id == 0)
	{
		// 不允许回绕为零或复用历史 ID；即使极端耗尽也明确失败。
		if (NextShieldId == 0)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("激活护盾失败：%s，世界护盾 ID 已耗尽。"), *OwnerKey->GetPathName());
			return 0;
		}
		Id = NextShieldId++;
	}
	ActiveShields.Add(Id, {OwnerKey, WorldLocation, AttractionRadius, RemainingBlocks, ReflectDamage});
	OnShieldStateChanged.Broadcast(OwnerKey, RemainingBlocks);
	return Id;
}

void UCastleShieldTowerSubsystem::DeactivateShield(UObject* OwnerKey)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("关闭护盾失败：%s，必须在游戏线程调用。"), *GetPathNameSafe(OwnerKey));
		return;
	}
	for (auto It = ActiveShields.CreateIterator(); It; ++It)
	{
		if (It.Value().Owner.Get() == OwnerKey)
		{
			It.RemoveCurrent();
			OnShieldStateChanged.Broadcast(OwnerKey, 0);
			return;
		}
	}
}

bool UCastleShieldTowerSubsystem::GetShieldRemainingBlocks(const UObject* OwnerKey, int64& OutRemainingBlocks) const
{
	OutRemainingBlocks = 0;
	if (!IsInGameThread())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("查询护盾失败：%s，必须在游戏线程调用。"), *GetPathNameSafe(OwnerKey));
		return false;
	}
	for (const auto& Pair : ActiveShields)
	{
		if (Pair.Value.Owner.IsValid() && Pair.Value.Owner.Get() == OwnerKey && Pair.Value.RemainingBlocks > 0)
		{
			OutRemainingBlocks = Pair.Value.RemainingBlocks;
			return true;
		}
	}
	return false;
}

bool UCastleShieldTowerSubsystem::FindNearestActiveShield(const FVector& QueryLocation, float MaxDistance,
	bool bRequireWithinAttractionRadius, uint64& OutShieldId, FVector& OutTargetLocation) const
{
	OutShieldId = 0;
	OutTargetLocation = FVector::ZeroVector;
	if (!IsInGameThread() || QueryLocation.ContainsNaN() || !FMath::IsFinite(MaxDistance) || MaxDistance < 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("查找护盾失败：世界 %s，须在游戏线程提供有限坐标和有限非负查询距离。"), *GetNameSafe(GetWorld()));
		return false;
	}
	double NearestSquared = FMath::Square(static_cast<double>(MaxDistance));
	for (const auto& Pair : ActiveShields)
	{
		const FActiveShield& Shield = Pair.Value;
		// 这是契约定义的候选集合：只查激活、Owner 有效且满足距离条件的护盾。
		if (!Shield.Owner.IsValid() || Shield.RemainingBlocks <= 0) { continue; }
		const double DistanceSquared = FVector::DistSquared(QueryLocation, Shield.Location);
		if (DistanceSquared > NearestSquared || (bRequireWithinAttractionRadius
			&& DistanceSquared > FMath::Square(static_cast<double>(Shield.AttractionRadius)))) { continue; }
		if (DistanceSquared == NearestSquared && OutShieldId != 0 && Pair.Key > OutShieldId) { continue; }
		NearestSquared = DistanceSquared;
		OutShieldId = Pair.Key;
		OutTargetLocation = Shield.Location;
	}
	return OutShieldId != 0;
}

bool UCastleShieldTowerSubsystem::GetActiveShieldLocation(uint64 ShieldId, FVector& OutLocation) const
{
	OutLocation = FVector::ZeroVector;
	if (!IsInGameThread())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("读取锁定护盾失败：必须在游戏线程调用。"));
		return false;
	}
	const FActiveShield* Shield = ActiveShields.Find(ShieldId);
	if (!Shield || !Shield->Owner.IsValid() || Shield->RemainingBlocks <= 0) { return false; }
	OutLocation = Shield->Location;
	return true;
}

bool UCastleShieldTowerSubsystem::ApplyAttackToShield(uint64 ShieldId, const FCastleMassUnitHandle& Attacker,
	int64& OutRemainingBlocksAfterHit)
{
	OutRemainingBlocksAfterHit = 0;
	if (!IsInGameThread())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("护盾受击失败：ID=%llu，必须在游戏线程调用。"), ShieldId);
		return false;
	}
	FActiveShield* Shield = ActiveShields.Find(ShieldId);
	if (!Shield || !Shield->Owner.IsValid() || Shield->RemainingBlocks <= 0) { return false; }
	UObject* Owner = Shield->Owner.Get();
	float ReflectDamage = Shield->ReflectDamage;
	// 红色为空投落地时入圈的塔提供攻击加成；护盾挡击次数不受影响，仅反伤随攻击力变化。
	if (const auto* Effect = Cast<UActorComponent>(Owner))
	{
		if (const auto* Tower = Cast<ACastleBuildingBase>(Effect->GetOwner()))
		{
			const auto* Support = GetWorld()->GetSubsystem<UCastleSupportTowerSubsystem>();
			if (!Support)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("盾塔 %s 受击结算失败：缺少支援塔子系统。"), *Tower->GetPathName());
				return false;
			}
			ReflectDamage *= Support->GetTowerDamageMultiplier(*Tower);
		}
	}
	OutRemainingBlocksAfterHit = --Shield->RemainingBlocks;
	// 先完成注册表变更，再广播，委托里即使重启护盾也不会被旧命中的注销覆盖。
	if (OutRemainingBlocksAfterHit == 0) { ActiveShields.Remove(ShieldId); }
	if (ReflectDamage > 0.0f && Attacker.World.Get() == GetWorld())
	{
		UMassEntitySubsystem* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
		if (Mass && Mass->GetEntityManager().IsEntityActive(Attacker.Entity))
		{
			const FMassEntityView View(Mass->GetEntityManager(), Attacker.Entity);
			const auto* Unit = View.GetFragmentDataPtr<FCastleMassUnitFragment>();
			if (View.HasTag<FCastleMassEnemyTag>() && Unit && Unit->Kind == ECastleMassUnitKind::Enemy && Unit->Health > 0.0f)
			{
				GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>()->ApplyImmediateDamageToEnemy(Attacker, ReflectDamage);
			}
		}
	}
	OnShieldStateChanged.Broadcast(Owner, OutRemainingBlocksAfterHit);
	return true;
}
