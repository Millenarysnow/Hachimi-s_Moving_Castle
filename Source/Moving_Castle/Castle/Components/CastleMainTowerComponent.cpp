// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleMainTowerComponent.h"

#include "CastleMainTowerBalloonComponent.h"
#include "Engine/World.h"
#include "Items/CastleBadgeRuntimeSubsystem.h"
#include "Moving_Castle.h"

UCastleMainTowerComponent::UCastleMainTowerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UCastleMainTowerComponent::InitializeHealth()
{
	if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0.0f
		|| MaxBalloons < 1 || MaxBalloons > 100 || !FMath::IsFinite(HealthPerBalloon) || HealthPerBalloon <= 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔 %s 生命初始化失败：最大生命、每球生命须为有限正数，气球容量须在 1～100。"), *GetPathName());
		return false;
	}

	BaseMaxHealth = MaxHealth;
	HealthMultiplier = 1.0f;
	bTutorialHealthLockEnabled = false;
	CurrentHealth = MaxHealth;
	AttackCount = 0;
	return true;
}

bool UCastleMainTowerComponent::SetTowerShaftDimensions(const float ShaftWidth, const float ShaftHeight,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	const AActor* Owner = GetOwner();
	FCastleTowerPrism ValidatedPrism;
	if (!IsValid(Owner) || !FCastleTowerPrism::Build(Owner->GetActorTransform(), ShaftWidth, ShaftHeight,
		ValidatedPrism, OutFailureReason))
	{
		if (!IsValid(Owner)) { OutFailureReason = TEXT("主塔 Actor 无效。"); }
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔 %s 塔身尺寸更新失败：%s"),
			*GetPathName(), *OutFailureReason);
		return false;
	}

	TowerShaftWidth = ShaftWidth;
	TowerShaftHeight = ShaftHeight;
	return true;
}

bool UCastleMainTowerComponent::GetTowerShaftPrism(FCastleTowerPrism& OutPrism,
	FString& OutFailureReason) const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		OutFailureReason = TEXT("主塔 Actor 无效。");
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔 %s 塔身查询失败：%s"), *GetPathName(), *OutFailureReason);
		return false;
	}
	if (!FCastleTowerPrism::Build(Owner->GetActorTransform(), TowerShaftWidth, TowerShaftHeight,
		OutPrism, OutFailureReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔 %s 塔身查询失败：%s"), *GetPathName(), *OutFailureReason);
		return false;
	}
	return true;
}

bool UCastleMainTowerComponent::ApplyEnemyDamage(const float Damage)
{
	return ApplyDamageInternal(Damage, true);
}

bool UCastleMainTowerComponent::ApplyDamage(const float Damage)
{
	return ApplyDamageInternal(Damage, false);
}

bool UCastleMainTowerComponent::ApplyDamageInternal(const float Damage, const bool bEnemyAttack)
{
	if (!IsInGameThread() || bResolvingDamage || !FMath::IsFinite(Damage) || Damage < 0.0f
		|| !FMath::IsFinite(MaxHealth) || MaxHealth <= 0.0f || !FMath::IsFinite(CurrentHealth))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔 %s 受击失败：须在游戏线程提交非重入攻击，且生命状态有效、伤害为有限非负数。"), *GetPathName());
		return false;
	}

	if (CurrentHealth <= 0.0f)
	{
		// 主塔死亡后不再重复扣血；调用仍明确失败，避免把死亡状态静默当成成功攻击。
		return false;
	}

	TGuardValue<bool> DamageGuard(bResolvingDamage, true);
	const float PreviousHealth = CurrentHealth;
	float NextHealth = FMath::Max(GetMinimumHealthFloor(), PreviousHealth - Damage);
	if (bEnemyAttack && NextHealth == 0.0f)
	{
		auto* Runtime = GetWorld() ? GetWorld()->GetSubsystem<UCastleBadgeRuntimeSubsystem>() : nullptr;
		if (!Runtime)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("主塔 %s 结算敌方致命伤害失败：缺少本局徽章子系统。"), *GetPathName());
			return false;
		}
		if (!Runtime->TryResolveLastBalloonClasp(*this, NextHealth)) { return false; }
	}
	CurrentHealth = NextHealth;
	const float AppliedDamage = PreviousHealth - CurrentHealth;
	++AttackCount;
	// 破球警钟只由真实掉血引发；若气球扣保留了受击前的生命，本次通知的掉血量为零。
	NotifyHealthChanged(PreviousHealth, AppliedDamage);

	if (Damage > 0.0f && bTutorialHealthLockEnabled && AppliedDamage <= 0.0f)
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("教程主塔 %s 的一心锁血阻止伤害：%.2f / %.2f。"),
			*GetPathName(), CurrentHealth, MaxHealth);
	}
	else if (Damage > 0.0f)
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("主塔受到 %.2f 点伤害：%.2f / %.2f。"),
			bTutorialHealthLockEnabled ? AppliedDamage : Damage, CurrentHealth, MaxHealth);
	}
	else
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("主塔收到一次零伤害攻击：%.2f / %.2f。"), CurrentHealth, MaxHealth);
	}
	return true;
}

bool UCastleMainTowerComponent::EnableTutorialHealthLock(FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const float HealthFloor = MaxHealth / 20.0f;
	if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0.0f || !FMath::IsFinite(CurrentHealth)
		|| CurrentHealth < HealthFloor || CurrentHealth > MaxHealth)
	{
		OutReason = FText::Format(INVTEXT("教程主塔 {0} 无法启用一心锁血：当前生命或最大生命无效，或当前生命已低于锁定下限。"),
			FText::FromString(GetPathName()));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}

	bTutorialHealthLockEnabled = true;
	UE_LOG(LogMoving_Castle, Display, TEXT("新手教程主塔 %s 已启用一心锁血，下限为 %.2f / %.2f。"),
		*GetPathName(), HealthFloor, MaxHealth);
	return true;
}

float UCastleMainTowerComponent::GetMinimumHealthFloor() const
{
	return bTutorialHealthLockEnabled ? MaxHealth / 20.0f : 0.0f;
}

bool UCastleMainTowerComponent::ApplyHealing(const float Amount)
{
	if (!FMath::IsFinite(Amount) || Amount < 0.0f || !FMath::IsFinite(MaxHealth) || MaxHealth <= 0.0f
		|| !FMath::IsFinite(CurrentHealth) || CurrentHealth <= 0.0f || CurrentHealth > MaxHealth)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔回血失败：组件 %s 的回血量必须是有限非负数，且主塔生命状态和 MaxHealth 必须有效。"), *GetPathName());
		return false;
	}

	const float PreviousHealth = CurrentHealth;
	CurrentHealth = FMath::Min(MaxHealth, CurrentHealth + Amount);
	NotifyHealthChanged(PreviousHealth, PreviousHealth - CurrentHealth);
	UE_LOG(LogMoving_Castle, Display, TEXT("主塔恢复 %.2f 点生命：%.2f / %.2f。"),
		CurrentHealth - PreviousHealth, CurrentHealth, MaxHealth);
	return true;
}

void UCastleMainTowerComponent::GetBalloonCounts(int32& OutCurrentBalloons, int32& OutMaxBalloons) const
{
	OutMaxBalloons = MaxBalloons;
	OutCurrentBalloons = CurrentHealth <= 0.0f ? 0
		: FMath::Clamp(FMath::CeilToInt(static_cast<double>(CurrentHealth) * MaxBalloons / MaxHealth), 1, MaxBalloons);
}

void UCastleMainTowerComponent::NotifyHealthChanged(const float PreviousHealth, const float Damage)
{
	// 地面棋盘可继续使用仅有生命组件的主塔，已接入的塔顶先刷新再通知 HUD。
	if (BalloonPresentation)
	{
		// 吊挂套件用原始生命比例采样。逻辑气球已向上取整，不能作为视觉颗数的输入。
		BalloonPresentation->UpdateHealth(CurrentHealth, MaxHealth);
	}
	OnHealthChanged.Broadcast(PreviousHealth, CurrentHealth, Damage);
}

bool UCastleMainTowerComponent::AttachBalloonPresentation(UCastleMainTowerBalloonComponent* Presentation, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (BalloonPresentation || !IsValid(Presentation) || Presentation->GetOwner() != GetOwner())
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 无法登记气球表现：组件无效、不属于同一座主塔或已登记。"), *GetPathName()));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	if (!Presentation->UpdateHealth(CurrentHealth, MaxHealth))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 的吊挂生命无效。"), *GetPathName()));
		return false;
	}
	BalloonPresentation = Presentation;
	return true;
}

bool UCastleMainTowerComponent::PrepareTowerBalloonTier(const int32 NewMaxBalloons, const float NewBaseMaxHealth,
	const bool bFullHeal, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const float NewMaxHealth = NewBaseMaxHealth * HealthMultiplier;
	if (NewMaxBalloons < 1 || NewMaxBalloons > 100 || !FMath::IsFinite(NewMaxHealth) || NewMaxHealth <= 0.0f
		|| (!bFullHeal && CurrentHealth > NewMaxHealth))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 拒绝气球等级变更：容量须在 1～100，且新上限须为有限正数并容纳保留的生命。"),
			*GetPathName()));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	// 逻辑容量单独校验。实际套件等级由棋盘的开放层数选择和提交。
	return true;
}

bool UCastleMainTowerComponent::SetTowerBalloonTier(const int32 NewMaxBalloons, const float NewBaseMaxHealth,
	const bool bFullHeal, FText& OutReason)
{
	if (!PrepareTowerBalloonTier(NewMaxBalloons, NewBaseMaxHealth, bFullHeal, OutReason)) { return false; }
	CommitPreparedBalloonTier(NewMaxBalloons, NewBaseMaxHealth, bFullHeal);
	return true;
}

void UCastleMainTowerComponent::CommitPreparedBalloonTier(
	const int32 NewMaxBalloons, const float NewBaseMaxHealth, const bool bFullHeal)
{
	const float PreviousHealth = CurrentHealth;
	BaseMaxHealth = NewBaseMaxHealth;
	MaxHealth = BaseMaxHealth * HealthMultiplier;
	MaxBalloons = NewMaxBalloons;
	if (bFullHeal) { CurrentHealth = MaxHealth; }
	else if (bTutorialHealthLockEnabled) { CurrentHealth = FMath::Max(CurrentHealth, GetMinimumHealthFloor()); }
	NotifyHealthChanged(PreviousHealth, PreviousHealth - CurrentHealth);
}

void UCastleMainTowerComponent::DiscardPreparedBalloonTier()
{
	if (BalloonPresentation) { BalloonPresentation->DiscardPreparedTier(); }
}

bool UCastleMainTowerComponent::SetMaxHealthMultiplier(const float Multiplier, const bool bScaleCurrentHealth)
{
	const float NewMaxHealth = BaseMaxHealth * Multiplier;
	const float NewCurrentHealth = bScaleCurrentHealth
		? static_cast<float>(static_cast<double>(CurrentHealth) / MaxHealth * NewMaxHealth) : CurrentHealth;
	const float ValidatedCurrentHealth = bTutorialHealthLockEnabled
		? FMath::Max(NewCurrentHealth, NewMaxHealth / 20.0f) : NewCurrentHealth;
	if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0.0f || !FMath::IsFinite(NewMaxHealth) || NewMaxHealth <= 0.0f
		|| !FMath::IsFinite(ValidatedCurrentHealth)
		|| ValidatedCurrentHealth > NewMaxHealth || (CurrentHealth > 0.0f && ValidatedCurrentHealth <= 0.0f))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔 %s 拒绝生命倍率 %g：新最大生命或缩放后的当前生命无效。"), *GetPathName(), Multiplier);
		return false;
	}
	const float PreviousHealth = CurrentHealth;
	HealthMultiplier = Multiplier;
	MaxHealth = NewMaxHealth;
	CurrentHealth = ValidatedCurrentHealth;
	// 这次只同步已持有徽章的生命倍率，区别于战斗受伤或主动回血。
	NotifyHealthChanged(PreviousHealth, 0.0f);
	return true;
}

bool UCastleMainTowerComponent::TryConsumeBalloons(const int32 BalloonCount, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const float Cost = HealthPerBalloon * BalloonCount;
	const float RemainingHealth = CurrentHealth - Cost;
	int32 CurrentBalloons, Capacity;
	GetBalloonCounts(CurrentBalloons, Capacity);
	if (BalloonCount <= 0 || BalloonCount >= CurrentBalloons || !FMath::IsFinite(Cost) || Cost <= 0.0f
		|| RemainingHealth <= 0.0f || RemainingHealth < GetMinimumHealthFloor() || RemainingHealth >= CurrentHealth)
	{
		const TCHAR* HealthRule = bTutorialHealthLockEnabled
			? TEXT("数量须为正，回收后需保留一个存活气球且至少保留一颗心")
			: TEXT("数量须为正且回收后需要保留至少一个存活气球");
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 无法回收 %d 个气球：%s。"),
			*GetPathName(), BalloonCount, HealthRule));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	// 空岛消耗不是敌人攻击，因此保留攻击计数，但仍广播真实扣血量。
	const float PreviousHealth = CurrentHealth;
	CurrentHealth = RemainingHealth;
	NotifyHealthChanged(PreviousHealth, Cost);
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

bool UCastleMainTowerSubsystem::GetMainTowerPrism(FCastleTowerPrism& OutPrism) const
{
	UCastleMainTowerComponent* Tower = RegisteredMainTower.Get();
	if (!IsValid(Tower) || !Tower->IsAlive())
	{
		return false;
	}
	FString FailureReason;
	return Tower->GetTowerShaftPrism(OutPrism, FailureReason);
}

bool UCastleMainTowerSubsystem::GetMainTowerSurface(const FVector& WorldPoint, FVector& OutClosestPoint,
	FVector& OutOutwardNormal, float& OutSignedDistance) const
{
	OutClosestPoint = FVector::ZeroVector;
	OutOutwardNormal = FVector::ZeroVector;
	OutSignedDistance = 0.0f;
	if (!FMath::IsFinite(WorldPoint.X) || !FMath::IsFinite(WorldPoint.Y) || !FMath::IsFinite(WorldPoint.Z))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔表面查询失败：查询点不是有限世界坐标。"));
		return false;
	}
	FCastleTowerPrism Prism;
	if (!GetMainTowerPrism(Prism))
	{
		return false;
	}
	if (!Prism.QuerySurface(WorldPoint, OutClosestPoint, OutOutwardNormal, OutSignedDistance))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔 %s 表面查询失败：塔身几何或查询点无效。"),
			*GetPathNameSafe(GetRegisteredMainTower()));
		return false;
	}
	return true;
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

bool UCastleMainTowerSubsystem::ApplyEnemyMainTowerDamage(const float Damage)
{
	UCastleMainTowerComponent* Tower = RegisteredMainTower.Get();
	return IsValid(Tower) && Tower->ApplyEnemyDamage(Damage);
}
