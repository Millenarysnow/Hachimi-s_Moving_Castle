// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleTowerAttackEffectComponent.h"

#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/Data/CastleBuildingDefinition.h"
#include "Castle/Actors/CastleBuildings.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "Moving_Castle.h"

UCastleTowerAttackEffectComponent::UCastleTowerAttackEffectComponent()
{
	// 塔攻击需要用逐发计时，但无待射项时关闭 Tick，避免每座箭塔常驻逐帧开销。
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetComponentTickEnabled(false);
}

void UCastleTowerAttackEffectComponent::ExecuteEffect(const FCastleBuildingTriggerContext& Context)
{
	Super::ExecuteEffect(Context);

	// 基类已经接受本次触发；即使发射间隔未到或上一发仍在处理，本次也必须独立排队。
	if (PendingShotCount == TNumericLimits<int64>::Max())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("塔攻击待射队列溢出，建筑=%s；本次触发无法记录。"), *GetNameSafe(GetOwner()));
		return;
	}
	++PendingShotCount;
	bWaitingForExternalRetry = false;
	SetComponentTickEnabled(true);
}

void UCastleTowerAttackEffectComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (PendingShotCount <= 0)
	{
		ClearPendingShots();
		return;
	}
	if (bWaitingForExternalRetry)
	{
		return;
	}
	if (!IsInGameThread())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("塔攻击 Tick 必须运行在游戏线程，建筑=%s。"), *GetNameSafe(GetOwner()));
		bWaitingForExternalRetry = true;
		return;
	}
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("塔攻击收到无效 Tick 时间，建筑=%s，DeltaTime=%f。"), *GetNameSafe(GetOwner()), DeltaTime);
		bWaitingForExternalRetry = true;
		return;
	}

	const UCastleTowerAttackDataAsset* DataAsset = nullptr;
	const FCastleTowerAttackLevelData* LevelData = nullptr;
	if (!ResolveAttackConfiguration(DataAsset, LevelData))
	{
		bWaitingForExternalRetry = true;
		return;
	}
	const float IntervalSeconds = 1.0f / LevelData->AttacksPerSecond;
	if (!FMath::IsFinite(IntervalSeconds) || IntervalSeconds <= 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("塔攻击间隔无效，攻击资产=%s，等级=%d。"),
			*DataAsset->GetPathName(), LevelData->Level);
		bWaitingForExternalRetry = true;
		return;
	}

	// 每发都经过完整射速间隔；大 DeltaTime 会按到期顺序补发，但不会越过待射队列数量。
	if (SecondsUntilNextShot < 0.0f)
	{
		SecondsUntilNextShot = IntervalSeconds;
	}
	SecondsUntilNextShot -= DeltaTime;
	while (PendingShotCount > 0 && SecondsUntilNextShot <= 0.0f)
	{
		if (!FireOneShot(*DataAsset, *LevelData))
		{
			// 生成接口失败时保留当前待射项并暂停计时；新触发解除等待，避免逐帧重复错误日志。
			bWaitingForExternalRetry = true;
			return;
		}

		--PendingShotCount;
		if (PendingShotCount == 0)
		{
			ClearPendingShots();
			return;
		}
		SecondsUntilNextShot += IntervalSeconds;
	}
}

void UCastleTowerAttackEffectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 合成会销毁原建筑 Actor；待射队列只属于原实例，不迁移到合成结果。
	ClearPendingShots();
	Super::EndPlay(EndPlayReason);
}

bool UCastleTowerAttackEffectComponent::ResolveAttackConfiguration(const UCastleTowerAttackDataAsset*& OutDataAsset,
	const FCastleTowerAttackLevelData*& OutLevelData) const
{
	OutDataAsset = nullptr;
	OutLevelData = nullptr;
	const ACastleBuildingBase* Building = Cast<ACastleBuildingBase>(GetOwner());
	if (!IsValid(Building))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("塔攻击组件缺少有效建筑拥有者，组件=%s。"), *GetPathName());
		return false;
	}
	const UCastleBuildingDefinition* Definition = Building->GetBuildingDefinition();
	if (!IsValid(Definition) || !IsValid(Definition->AttackData))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑 %s 缺少有效的塔攻击 Data Asset，建筑定义=%s。"),
			*Building->GetName(), *GetPathNameSafe(Definition));
		return false;
	}
	FText FailureReason;
	if (!Definition->AttackData->ValidateConfiguration(FailureReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("塔攻击配置无效，资产=%s，原因=%s。"),
			*Definition->AttackData->GetPathName(), *FailureReason.ToString());
		return false;
	}
	const FCastleTowerAttackLevelData* ExactLevel = Definition->AttackData->FindLevel(Building->GetBuildingLevel());
	if (ExactLevel == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("塔攻击资产 %s 没有建筑 %s 的当前等级 %d，拒绝发射。"),
			*Definition->AttackData->GetPathName(), *Building->GetName(), Building->GetBuildingLevel());
		return false;
	}
	OutDataAsset = Definition->AttackData;
	OutLevelData = ExactLevel;
	return true;
}

bool UCastleTowerAttackEffectComponent::FireOneShot(const UCastleTowerAttackDataAsset& DataAsset,
	const FCastleTowerAttackLevelData& LevelData)
{
	ACastleBuildingBase* Building = Cast<ACastleBuildingBase>(GetOwner());
	if (!IsValid(Building))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("塔攻击发射时建筑已失效，组件=%s。"), *GetPathName());
		return false;
	}
	UCastleMassUnitSubsystem* Units = GetWorld() ? GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>() : nullptr;
	if (!IsValid(Units))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("塔攻击无法访问当前世界的 Mass 单位子系统，建筑=%s。"), *Building->GetName());
		return false;
	}
	UMassEntitySubsystem* MassEntities = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!IsValid(MassEntities))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("塔攻击无法访问当前世界的 Mass 实体管理器，建筑=%s。"), *Building->GetName());
		return false;
	}
	if (MassEntities->GetMutableEntityManager().IsProcessing())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 正在处理时拒绝塔攻击发射，建筑=%s；待射项保留。"), *Building->GetName());
		return false;
	}

	// 发射点沿建筑自身局部 Z 偏移，随后转换到世界空间供三维半径查询和 Mass 生成共用。
	const FVector Origin = Building->GetActorTransform().TransformPosition(FVector(0.0f, 0.0f, DataAsset.LaunchHeightOffset));
	if (Origin.ContainsNaN())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("塔攻击发射点包含无效坐标，建筑=%s，攻击资产=%s。"),
			*Building->GetName(), *DataAsset.GetPathName());
		return false;
	}
	FCastleMassUnitHandle Target;
	if (!Units->FindNearestEnemy(Origin, LevelData.AttackRadius, Target))
	{
		UE_LOG(LogMoving_Castle, Warning, TEXT("箭塔 %s 本次待射作废：攻击半径内没有存活敌人。"), *Building->GetName());
		return true;
	}

	FCastleMassHomingProjectileSpawn Spawn;
	Spawn.Origin = Origin;
	Spawn.Target = Target;
	Spawn.Damage = LevelData.ProjectileDamage;
	TArray<FCastleMassUnitHandle> SpawnedProjectiles;
	FText FailureReason;
	if (!Units->SpawnHomingProjectiles(DataAsset.ProjectileConfiguration, { Spawn }, SpawnedProjectiles, FailureReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("箭塔 %s 生成追踪子弹失败，攻击资产=%s，原因=%s；待射数量保留。"),
			*Building->GetName(), *DataAsset.GetPathName(), *FailureReason.ToString());
		return false;
	}
	return true;
}

void UCastleTowerAttackEffectComponent::ClearPendingShots()
{
	PendingShotCount = 0;
	SecondsUntilNextShot = -1.0f;
	bWaitingForExternalRetry = false;
	SetComponentTickEnabled(false);
}
