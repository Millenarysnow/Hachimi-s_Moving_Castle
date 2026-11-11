// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleMainTowerComponent.h"

#include "Engine/World.h"
#include "Moving_Castle.h"

UCastleMainTowerComponent::UCastleMainTowerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UCastleMainTowerComponent::InitializeHealth()
{
	if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔生命初始化失败：MaxHealth 必须是有限正数。"));
		return false;
	}

	CurrentHealth = MaxHealth;
	AttackCount = 0;
	return true;
}

bool UCastleMainTowerComponent::ApplyDamage(const float Damage)
{
	if (!FMath::IsFinite(Damage) || Damage < 0.0f || !FMath::IsFinite(MaxHealth) || MaxHealth <= 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔受击失败：伤害必须是有限非负数，且 MaxHealth 必须有效。"));
		return false;
	}

	if (CurrentHealth <= 0.0f)
	{
		// 主塔死亡后不再重复扣血；调用仍明确失败，避免把死亡状态静默当成成功攻击。
		return false;
	}

	const float PreviousHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - Damage);
	++AttackCount;
	OnHealthChanged.Broadcast(PreviousHealth, CurrentHealth, Damage);

	if (Damage > 0.0f)
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("主塔受到 %.2f 点伤害：%.2f / %.2f。"), Damage, CurrentHealth, MaxHealth);
	}
	else
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("主塔收到一次零伤害攻击：%.2f / %.2f。"), CurrentHealth, MaxHealth);
	}
	return true;
}

FVector UCastleMainTowerComponent::GetTargetLocation() const
{
	const AActor* Owner = GetOwner();
	return Owner != nullptr ? Owner->GetActorLocation() + FVector(0.0f, 0.0f, TargetHeightOffset) : FVector::ZeroVector;
}

void UCastleMainTowerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!InitializeHealth())
	{
		return;
	}

	if (UCastleMainTowerSubsystem* Subsystem = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>() : nullptr)
	{
		if (!Subsystem->RegisterMainTower(this))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("主塔组件注册失败：当前世界已经存在另一座有效主塔。"));
		}
	}
}

void UCastleMainTowerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UCastleMainTowerSubsystem* Subsystem = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>() : nullptr)
	{
		Subsystem->UnregisterMainTower(this);
	}

	Super::EndPlay(EndPlayReason);
}

bool UCastleMainTowerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UCastleMainTowerSubsystem::RegisterMainTower(UCastleMainTowerComponent* Tower)
{
	if (!IsInGameThread() || !IsValid(Tower) || Tower->GetWorld() != GetWorld())
	{
		return false;
	}

	if (UCastleMainTowerComponent* Existing = RegisteredMainTower.Get())
	{
		return Existing == Tower;
	}

	RegisteredMainTower = Tower;
	return true;
}

bool UCastleMainTowerSubsystem::UnregisterMainTower(UCastleMainTowerComponent* Tower)
{
	if (RegisteredMainTower.Get() != Tower)
	{
		return false;
	}

	RegisteredMainTower.Reset();
	return true;
}

UCastleMainTowerComponent* UCastleMainTowerSubsystem::GetRegisteredMainTower() const
{
	return RegisteredMainTower.Get();
}

bool UCastleMainTowerSubsystem::GetMainTowerTargetLocation(FVector& OutLocation) const
{
	OutLocation = FVector::ZeroVector;
	const UCastleMainTowerComponent* Tower = RegisteredMainTower.Get();
	if (!IsValid(Tower) || !Tower->IsAlive())
	{
		return false;
	}

	OutLocation = Tower->GetTargetLocation();
	return !OutLocation.ContainsNaN();
}

bool UCastleMainTowerSubsystem::GetMainTowerHealth(float& OutCurrentHealth, float& OutMaxHealth) const
{
	OutCurrentHealth = 0.0f;
	OutMaxHealth = 0.0f;
	const UCastleMainTowerComponent* Tower = RegisteredMainTower.Get();
	if (!IsValid(Tower))
	{
		return false;
	}

	OutCurrentHealth = Tower->CurrentHealth;
	OutMaxHealth = Tower->MaxHealth;
	return true;
}

bool UCastleMainTowerSubsystem::ApplyMainTowerDamage(const float Damage)
{
	UCastleMainTowerComponent* Tower = RegisteredMainTower.Get();
	return IsValid(Tower) && Tower->ApplyDamage(Damage);
}
