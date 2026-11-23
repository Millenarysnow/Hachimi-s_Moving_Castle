// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleMainTowerBalloonComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

UCastleMainTowerBalloonComponent::UCastleMainTowerBalloonComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BalloonMesh = Sphere.Object;
	StringMesh = Cylinder.Object;
	BalloonMaterial = Material.Object;
}

bool UCastleMainTowerBalloonComponent::Initialize(USceneComponent* InAnchor, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (Anchor || !IsValid(InAnchor) || InAnchor->GetOwner() != GetOwner() || !InAnchor->IsRegistered()
		|| !IsValid(BalloonMesh) || !IsValid(StringMesh) || !IsValid(BalloonMaterial))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 气球初始化失败：塔顶锚点或占位 Sphere、Cylinder、BasicShapeMaterial 资源无效。"),
			*GetPathNameSafe(GetOwner())));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	Anchor = InAnchor;
	return true;
}

bool UCastleMainTowerBalloonComponent::SetCapacity(const int32 Capacity, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	AActor* Tower = GetOwner();
	const auto Reject = [&](const TCHAR* Detail)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 无法创建 %d 个气球：%s。"), *GetPathNameSafe(Tower), Capacity, Detail));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	};
	if (!IsValid(Anchor) || !Anchor->IsRegistered() || Capacity < 1 || Capacity > 100)
	{
		return Reject(TEXT("塔顶锚点无效或容量不在 1～100"));
	}
	TArray<UStaticMeshComponent*> Pending;
	const auto CreateMesh = [&](UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Tower);
		Part->SetupAttachment(Anchor);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetStaticMesh(Mesh);
		Part->SetVisibility(false);
		Tower->AddInstanceComponent(Part);
		Pending.Add(Part);
		return Part;
	};

	// 用固定螺旋次序展开气球；新增气球时旧序号的空间位置和颜色都保持不动。
	const FLinearColor Colors[] = {FLinearColor(0.95f, 0.13f, 0.26f), FLinearColor(0.98f, 0.62f, 0.06f),
		FLinearColor(0.12f, 0.64f, 0.95f), FLinearColor(0.57f, 0.29f, 0.90f), FLinearColor(0.15f, 0.76f, 0.48f)};
	for (int32 Index = Balloons.Num(); Index < Capacity; ++Index)
	{
		const double Angle = Index * 2.399963229728653;
		const double Radius = 38.0 * FMath::Sqrt(static_cast<double>(Index));
		const FVector Center(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 85.0 + (Index % 4) * 24.0);
		const FVector Knot(Center.X * 0.20, Center.Y * 0.20, 0.0);
		const FVector End = Center - FVector(0, 0, 28);
		const FVector Cable = End - Knot;
		UStaticMeshComponent* Balloon = CreateMesh(BalloonMesh);
		UStaticMeshComponent* String = CreateMesh(StringMesh);
		Balloon->SetRelativeLocation(Center);
		Balloon->SetRelativeScale3D(FVector(0.44, 0.44, 0.56));
		String->SetRelativeTransform(FTransform(FQuat::FindBetweenNormals(FVector::UpVector, Cable.GetSafeNormal()),
			(Knot + End) * 0.5, FVector(0.015, 0.015, Cable.Size() / 100.0)));
		UMaterialInstanceDynamic* Color = Balloon->CreateDynamicMaterialInstance(0, BalloonMaterial);
		if (!IsValid(Color))
		{
			for (UStaticMeshComponent* Part : Pending) { Part->DestroyComponent(); }
			return Reject(TEXT("无法创建气球动态材质"));
		}
		Color->SetVectorParameterValue(TEXT("Color"), Colors[Index % UE_ARRAY_COUNT(Colors)]);
	}
	for (UStaticMeshComponent* Part : Pending)
	{
		Part->RegisterComponent();
		if (!Part->IsRegistered())
		{
			for (UStaticMeshComponent* Created : Pending) { Created->DestroyComponent(); }
			return Reject(TEXT("气球或球绳组件注册失败"));
		}
	}
	for (int32 Index = 0; Index < Pending.Num(); Index += 2)
	{
		Balloons.Add(Pending[Index]);
		Strings.Add(Pending[Index + 1]);
	}
	return true;
}

void UCastleMainTowerBalloonComponent::DiscardCapacityAbove(const int32 Capacity)
{
	check(Capacity >= LiveCount && Balloons.Num() == Strings.Num());
	while (Balloons.Num() > Capacity)
	{
		Balloons.Pop()->DestroyComponent();
		Strings.Pop()->DestroyComponent();
	}
}

void UCastleMainTowerBalloonComponent::SetLiveCount(const int32 Count)
{
	check(Count >= 0 && Count <= Balloons.Num());
	for (int32 Index = FMath::Min(Count, LiveCount); Index < FMath::Max(Count, LiveCount); ++Index)
	{
		const bool bLive = Index < Count;
		Balloons[Index]->SetVisibility(bLive);
		Strings[Index]->SetVisibility(bLive);
	}
	if (Count < LiveCount)
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("主塔 %s 破裂 %d 个气球，剩余 %d 个。"), *GetPathNameSafe(GetOwner()), LiveCount - Count, Count);
	}
	LiveCount = Count;
}
