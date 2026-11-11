// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleMassEnemySpawner.h"

#include "CastleMainTowerComponent.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "MassEntityConfigAsset.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

ACastleMassEnemySpawner::ACastleMassEnemySpawner()
{
	// 生成器只在 BeginPlay 或外部显式调用时创建实体，不执行逐帧逻辑。
	static ConstructorHelpers::FObjectFinder<UMassEntityConfigAsset> EnemyConfig(
		TEXT("/Game/MovingCastle/Mass/DA_MassEnemyBase.DA_MassEnemyBase"));
	Configuration = EnemyConfig.Object;
	Count = 1;
	Columns = 1;
	Spacing = 1.0f;
	bStartMoving = false;
}

bool ACastleMassEnemySpawner::SpawnUnits()
{
	if (Count <= 0 || !FMath::IsFinite(InnerRadius) || !FMath::IsFinite(OuterRadius)
		|| InnerRadius < 0.0f || OuterRadius <= InnerRadius || !FMath::IsFinite(HeightOffset))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环生成失败：数量、半径或高度配置无效。"));
		return false;
	}

	UCastleMainTowerSubsystem* TowerSubsystem = GetWorld() != nullptr
		? GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>() : nullptr;
	FVector TargetLocation = FVector::ZeroVector;
	if (TowerSubsystem == nullptr || !TowerSubsystem->GetMainTowerTargetLocation(TargetLocation))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环生成失败：当前世界没有可用的主塔目标。"));
		return false;
	}

	if (!Configuration || !Configuration->FindTrait(UCastleMassEnemyTrait::StaticClass()))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环生成失败：配置没有 Castle Mass 敌人 Trait。"));
		return false;
	}

	if (UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>())
	{
		if (!SpawnedUnits.IsEmpty())
		{
			return false;
		}

		FRandomStream RandomStream(RandomSeed);
		const float InnerRadiusSquared = FMath::Square(InnerRadius);
		const float OuterRadiusSquared = FMath::Square(OuterRadius);
		TArray<FTransform> Transforms;
		Transforms.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// 对半径平方做均匀采样，避免圆环外侧因面积更大而被过度偏置。
			const float Radius = FMath::Sqrt(FMath::Lerp(InnerRadiusSquared, OuterRadiusSquared, RandomStream.GetFraction()));
			const float Angle = RandomStream.FRandRange(0.0f, UE_TWO_PI);
			const FVector Location = TargetLocation + FVector(FMath::Cos(Angle) * Radius,
				FMath::Sin(Angle) * Radius, HeightOffset);
			const FVector Direction = (TargetLocation - Location).GetSafeNormal();
			Transforms.Emplace(Direction.IsNearlyZero() ? FRotator::ZeroRotator : Direction.Rotation(), Location);
		}

		FText FailureReason;
		if (!Units->SpawnUnits(Configuration, Transforms, SpawnedUnits, FailureReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环批量生成失败：%s"), *FailureReason.ToString());
			return false;
		}
		return true;
	}

	UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环生成失败：世界没有 Mass 单位子系统。"));
	return false;
}
