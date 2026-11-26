// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleTowerPrism.h"

namespace
{
	/** UE 5.8 的 FVector 以分量提供有限值检查，不暴露 IsFinite 成员函数。 */
	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	/** 记录一张候选表面上的最近点、法线和平方距离，避免反复开平方。 */
	struct FCastlePrismSurfaceCandidate
	{
		FVector Point = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
		double DistanceSquared = TNumericLimits<double>::Max();
	};

	/** 保留候选点最小的几何距离，调用方按塔身面顺序提供平局规则。 */
	void ConsiderSurfaceCandidate(const FVector& LocalPoint, const FVector& CandidatePoint,
		const FVector& CandidateNormal, FCastlePrismSurfaceCandidate& InOutBest)
	{
		const double DistanceSquared = FVector::DistSquared(LocalPoint, CandidatePoint);
		if (DistanceSquared < InOutBest.DistanceSquared)
		{
			InOutBest.Point = CandidatePoint;
			InOutBest.Normal = CandidateNormal;
			InOutBest.DistanceSquared = DistanceSquared;
		}
	}
}

bool FCastleTowerPrism::Build(const FTransform& ActorTransform, const float ShaftWidth, const float ShaftHeight,
	FCastleTowerPrism& OutPrism, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!FMath::IsFinite(ShaftWidth) || ShaftWidth <= 1.0f || ShaftWidth > 100000.0f
		|| !FMath::IsFinite(ShaftHeight) || ShaftHeight <= 1.0f || ShaftHeight > 100000.0f
		|| ActorTransform.ContainsNaN())
	{
		OutFailureReason = TEXT("塔身边长、高度或 Actor 变换必须为有限有效值。");
		return false;
	}

	const FVector Scale = ActorTransform.GetScale3D().GetAbs();
	if (!IsFiniteVector(Scale) || Scale.X <= UE_SMALL_NUMBER || Scale.Y <= UE_SMALL_NUMBER || Scale.Z <= UE_SMALL_NUMBER)
	{
		OutFailureReason = TEXT("主塔 Actor 的三个缩放轴必须是有限非零值。");
		return false;
	}

	OutPrism.BottomCenter = ActorTransform.GetLocation();
	OutPrism.Rotation = ActorTransform.GetRotation().GetNormalized();
	OutPrism.HalfSize = FVector(ShaftWidth * Scale.X * 0.5f,
		ShaftWidth * Scale.Y * 0.5f, ShaftHeight * Scale.Z);
	if (!IsFiniteVector(OutPrism.BottomCenter) || !IsFiniteVector(OutPrism.HalfSize))
	{
		OutFailureReason = TEXT("主塔世界塔身尺寸计算溢出。");
		return false;
	}
	return true;
}

FVector FCastleTowerPrism::ToLocalPoint(const FVector& WorldPoint) const
{
	return Rotation.UnrotateVector(WorldPoint - BottomCenter);
}

FVector FCastleTowerPrism::GetUpVector() const
{
	return Rotation.GetAxisZ();
}

bool FCastleTowerPrism::QuerySurface(const FVector& WorldPoint, FVector& OutClosestPoint,
	FVector& OutOutwardNormal, float& OutSignedDistance) const
{
	OutClosestPoint = FVector::ZeroVector;
	OutOutwardNormal = FVector::ZeroVector;
	OutSignedDistance = 0.0f;
	if (!IsFiniteVector(WorldPoint) || !IsFiniteVector(BottomCenter) || !IsFiniteVector(HalfSize)
		|| HalfSize.X <= 0.0f || HalfSize.Y <= 0.0f || HalfSize.Z <= 0.0f || !Rotation.IsNormalized())
	{
		return false;
	}

	const FVector LocalPoint = ToLocalPoint(WorldPoint);
	const FVector HalfXY(HalfSize.X, HalfSize.Y, 0.0f);
	const bool bInsideShaft = FMath::Abs(LocalPoint.X) < HalfSize.X
		&& FMath::Abs(LocalPoint.Y) < HalfSize.Y
		&& LocalPoint.Z >= 0.0f && LocalPoint.Z <= HalfSize.Z;
	FCastlePrismSurfaceCandidate Best;

	// 主塔底面处于地面支撑面，不作为敌人的可穿入障碍面。
	ConsiderSurfaceCandidate(LocalPoint,
		FVector(HalfSize.X, FMath::Clamp(LocalPoint.Y, -HalfXY.Y, HalfXY.Y), FMath::Clamp(LocalPoint.Z, 0.0f, HalfSize.Z)),
		FVector::ForwardVector, Best);
	ConsiderSurfaceCandidate(LocalPoint,
		FVector(-HalfSize.X, FMath::Clamp(LocalPoint.Y, -HalfXY.Y, HalfXY.Y), FMath::Clamp(LocalPoint.Z, 0.0f, HalfSize.Z)),
		-FVector::ForwardVector, Best);
	ConsiderSurfaceCandidate(LocalPoint,
		FVector(FMath::Clamp(LocalPoint.X, -HalfXY.X, HalfXY.X), HalfSize.Y, FMath::Clamp(LocalPoint.Z, 0.0f, HalfSize.Z)),
		FVector::RightVector, Best);
	ConsiderSurfaceCandidate(LocalPoint,
		FVector(FMath::Clamp(LocalPoint.X, -HalfXY.X, HalfXY.X), -HalfSize.Y, FMath::Clamp(LocalPoint.Z, 0.0f, HalfSize.Z)),
		-FVector::RightVector, Best);
	ConsiderSurfaceCandidate(LocalPoint,
		FVector(FMath::Clamp(LocalPoint.X, -HalfXY.X, HalfXY.X), FMath::Clamp(LocalPoint.Y, -HalfXY.Y, HalfXY.Y), HalfSize.Z),
		FVector::UpVector, Best);

	OutClosestPoint = BottomCenter + Rotation.RotateVector(Best.Point);
	OutOutwardNormal = Rotation.RotateVector(Best.Normal).GetSafeNormal();
	const float Distance = FMath::Sqrt(static_cast<float>(Best.DistanceSquared));
	OutSignedDistance = bInsideShaft ? -Distance : Distance;
	return IsFiniteVector(OutClosestPoint) && IsFiniteVector(OutOutwardNormal) && FMath::IsFinite(OutSignedDistance);
}

bool FCastleTowerPrism::ProjectSphereOutside(FVector& InOutCenter, const float Radius,
	FVector& InOutVelocity) const
{
	if (!FMath::IsFinite(Radius) || Radius < 0.0f || !IsFiniteVector(InOutCenter) || !IsFiniteVector(InOutVelocity))
	{
		return false;
	}

	FVector SurfacePoint;
	FVector SurfaceNormal;
	float SignedDistance = 0.0f;
	if (!QuerySurface(InOutCenter, SurfacePoint, SurfaceNormal, SignedDistance))
	{
		return false;
	}
	if (SignedDistance >= Radius)
	{
		return true;
	}

	InOutCenter = SurfacePoint + SurfaceNormal * Radius;
	const float InwardSpeed = FVector::DotProduct(InOutVelocity, SurfaceNormal);
	if (InwardSpeed < 0.0f)
	{
		InOutVelocity -= SurfaceNormal * InwardSpeed;
	}
	return true;
}
