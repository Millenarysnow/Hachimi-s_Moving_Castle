// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleMassEnemySpawner.h"

#include "CastleMainTowerComponent.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Castle/Combat/CastleTowerPrism.h"
#include "Engine/World.h"
#include "MassEntityConfigAsset.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

ACastleMassEnemySpawner::ACastleMassEnemySpawner()
{
	// 生成器只在 BeginPlay 或外部显式调用时创建实体，不执行逐帧逻辑。
	static ConstructorHelpers::FObjectFinder<UMassEntityConfigAsset> EnemyConfig(
		TEXT("/Game/MovingCastle/Mass/DA_MassMeleeTest.DA_MassMeleeTest"));
	Configuration = EnemyConfig.Object;
	Count = 1;
	Columns = 1;
	Spacing = 1.0f;
	bStartMoving = false;
}

bool ACastleMassEnemySpawner::SpawnUnits()
{
	if (Count <= 0 || !FMath::IsFinite(InnerRadius) || !FMath::IsFinite(OuterRadius)
		|| InnerRadius < 0.0f || OuterRadius <= InnerRadius || !FMath::IsFinite(HeightOffset)
		|| MinClusterCount <= 0 || MaxClusterCount < MinClusterCount
		|| static_cast<int64>(Count) * MaxClusterCount > MAX_int32
		|| !FMath::IsFinite(ClusterSpreadRadius) || ClusterSpreadRadius < 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环生成失败：%s，数量、半径、高度或簇数量范围无效。"), *GetPathNameSafe(Configuration));
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

	const UCastleMassEnemyTrait* EnemyTrait = Configuration
		? Cast<UCastleMassEnemyTrait>(Configuration->FindTrait(UCastleMassEnemyTrait::StaticClass())) : nullptr;
	if (!IsValid(EnemyTrait))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环生成失败：配置没有 Castle Mass 敌人 Trait。"));
		return false;
	}

	if (UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>())
	{
		FCastleTowerPrism TowerPrism;
		if (!TowerSubsystem->GetMainTowerPrism(TowerPrism))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环生成失败：主塔 %s 的塔身几何无效。"),
				*GetPathNameSafe(TowerSubsystem->GetRegisteredMainTower()));
			return false;
		}
		const float EffectiveRadius = EnemyTrait->Radius * AttributeOverrides.ScaleMultiplier;
		if (!FMath::IsFinite(EffectiveRadius) || EffectiveRadius < 0.0f)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环生成失败：配置 %s 的逻辑半径无效。"), *GetPathNameSafe(Configuration));
			return false;
		}
		if (!SpawnedUnits.IsEmpty())
		{
			return false;
		}

		FRandomStream RandomStream(RandomSeed);
		const float InnerRadiusSquared = FMath::Square(InnerRadius);
		const float OuterRadiusSquared = FMath::Square(OuterRadius);
		TArray<FTransform> Transforms;
		Transforms.Reserve(Count * MaxClusterCount);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// 对半径平方做均匀采样，避免圆环外侧因面积更大而被过度偏置。
			const float Radius = FMath::Sqrt(FMath::Lerp(InnerRadiusSquared, OuterRadiusSquared, RandomStream.GetFraction()));
			const float Angle = RandomStream.FRandRange(0.0f, UE_TWO_PI);
			const FVector Location = TargetLocation + FVector(FMath::Cos(Angle) * Radius,
				FMath::Sin(Angle) * Radius, HeightOffset);
			const int32 ClusterCount = RandomStream.RandRange(MinClusterCount, MaxClusterCount);
			const float ClusterPhase = RandomStream.FRandRange(0.0f, UE_TWO_PI);
			for (int32 Member = 0; Member < ClusterCount; ++Member)
			{
				const float OffsetRadius = FMath::Sqrt(RandomStream.GetFraction()) * ClusterSpreadRadius;
				const float OffsetAngle = ClusterPhase + UE_TWO_PI * Member / ClusterCount;
				const float HeightJitter = ClusterCount > 1
					? RandomStream.FRandRange(-EffectiveRadius, EffectiveRadius) : 0.0f;
				FVector MemberLocation = Location + FVector(FMath::Cos(OffsetAngle) * OffsetRadius,
					FMath::Sin(OffsetAngle) * OffsetRadius, HeightJitter);
				FVector SphereCenter = MemberLocation + TowerPrism.GetUpVector() * EffectiveRadius;
				FVector NoVelocity = FVector::ZeroVector;
				if (!TowerPrism.ProjectSphereOutside(SphereCenter, EffectiveRadius, NoVelocity))
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环生成失败：配置 %s 的实体不能投影到主塔外侧。"), *GetPathNameSafe(Configuration));
					return false;
				}
				MemberLocation = SphereCenter - TowerPrism.GetUpVector() * EffectiveRadius;
				FVector Local = TowerPrism.ToLocalPoint(MemberLocation);
				if (Local.Z < 0.0f)
				{
					Local.Z = 0.0f;
					MemberLocation = TowerPrism.BottomCenter + TowerPrism.Rotation.RotateVector(Local);
				}
				Transforms.Emplace((TargetLocation - MemberLocation).Rotation(), MemberLocation);
			}
		}

		FText FailureReason;
		if (!Units->SpawnEnemies(Configuration, Transforms, AttributeOverrides, SpawnedUnits, FailureReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环批量生成失败：%s"), *FailureReason.ToString());
			return false;
		}
		return true;
	}

	UE_LOG(LogMoving_Castle, Error, TEXT("Mass 敌人圆环生成失败：世界没有 Mass 单位子系统。"));
	return false;
}
