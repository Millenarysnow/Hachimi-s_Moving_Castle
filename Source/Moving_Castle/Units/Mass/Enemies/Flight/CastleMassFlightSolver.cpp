// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleMassFlightSolver.h"

namespace
{
	constexpr float MaxSubstepSeconds = 1.0f / 30.0f;
	constexpr float SeparationShellScale = 2.6f;
	constexpr float FlockShellScale = 4.5f;
	constexpr float AlignmentWeight = 0.35f;
	constexpr float CohesionWeight = 0.12f;
	constexpr float TowerFlowMargin = 260.0f;
	constexpr float TangentialFlowWeight = 0.90f;
	constexpr float MinimumAgentSpacingScale = 0.85f;

	/** 统一检查世界向量三个分量，含拒绝正负无穷。 */
	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	/**
	 * 构造稳定的整数空间格坐标。
	 *
	 * 只有在 int32 能精确表达格索引及其邻域时才成功，超出表示范围会明确失败，
	 * 避免窄化溢出把远处实体错误放进原点邻域。
	 */
	bool TryMakeGridKey(const FVector& Location, const float CellSize, const int32 NeighborCellRange,
		FIntVector& OutGridKey)
	{
		const double X = FMath::FloorToDouble(Location.X / CellSize);
		const double Y = FMath::FloorToDouble(Location.Y / CellSize);
		const double Z = FMath::FloorToDouble(Location.Z / CellSize);
		const double Minimum = static_cast<double>(MIN_int32 + NeighborCellRange);
		const double Maximum = static_cast<double>(MAX_int32 - NeighborCellRange);
		if (!FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(Z)
			|| X < Minimum || X > Maximum || Y < Minimum || Y > Maximum || Z < Minimum || Z > Maximum)
		{
			return false;
		}
		OutGridKey = FIntVector(static_cast<int32>(X), static_cast<int32>(Y), static_cast<int32>(Z));
		return true;
	}

	/** 以实体索引构造可复现的三维单位向量，完全重合时不依赖当前速度符号。 */
	FVector MakeStablePairDirection(const int32 FirstIndex, const int32 SecondIndex)
	{
		const int32 LowIndex = FMath::Min(FirstIndex, SecondIndex);
		const int32 HighIndex = FMath::Max(FirstIndex, SecondIndex);
		const uint32 Hash = HashCombineFast(::GetTypeHash(LowIndex), ::GetTypeHash(HighIndex));
		const double Z = FMath::Lerp(-0.85, 0.85, static_cast<double>(Hash & 0xFFFFu) / 65535.0);
		const double Angle = UE_TWO_PI * static_cast<double>(Hash >> 16) / 65535.0;
		FVector Direction(FMath::Sqrt(1.0 - Z * Z) * FMath::Cos(Angle),
			FMath::Sqrt(1.0 - Z * Z) * FMath::Sin(Angle), Z);
		if (FirstIndex > SecondIndex) { Direction *= -1.0; }
		return Direction;
	}

	/** 构造在给定表面法线切平面内且由实体索引决定的稳定破对称方向。 */
	FVector MakeStableTangent(const FVector& SurfaceNormal, const int32 EntityIndex)
	{
		const FVector Axis = FMath::Abs(SurfaceNormal.Z) < 0.8f ? FVector::UpVector : FVector::ForwardVector;
		const FVector TangentX = FVector::CrossProduct(SurfaceNormal, Axis).GetSafeNormal();
		const FVector TangentY = FVector::CrossProduct(SurfaceNormal, TangentX).GetSafeNormal();
		const float Phase = FMath::Fmod(static_cast<float>(EntityIndex) * 0.61803398875f, 1.0f) * UE_TWO_PI;
		return (TangentX * FMath::Cos(Phase) + TangentY * FMath::Sin(Phase)).GetSafeNormal();
	}

	/** 对 0..1 归一化距离做平滑衰减，保证远途群聚不会在塔面附近维持刚性编队。 */
	float SmoothTowerAttenuation(const float SurfaceDistance)
	{
		const float Alpha = FMath::Clamp(SurfaceDistance / TowerFlowMargin, 0.0f, 1.0f);
		return Alpha * Alpha * (3.0f - 2.0f * Alpha);
	}
}

bool FCastleMassFlightSolver::Solve(const TArray<FCastleMassFlightBody>& Bodies,
	const float DeltaTime, const FCastleTowerPrism* Tower,
	TArray<FCastleMassFlightResult>& OutResults, FString& OutFailureReason)
{
	OutResults.Reset();
	OutFailureReason.Reset();
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f)
	{
		OutFailureReason = TEXT("Boids 积分帧时间必须为有限非负数。");
		return false;
	}
	if (DeltaTime / MaxSubstepSeconds > static_cast<float>(MAX_int32))
	{
		OutFailureReason = TEXT("Boids 积分帧时间过大，所需子步数超出可表示范围。");
		return false;
	}
	for (const FCastleMassFlightBody& Body : Bodies)
	{
		if (Body.EntityIndex == INDEX_NONE || !IsFiniteVector(Body.Location) || !IsFiniteVector(Body.Velocity)
			|| !IsFiniteVector(Body.Target) || !IsFiniteVector(Body.DesiredVelocity)
			|| !FMath::IsFinite(Body.Radius) || Body.Radius < 0.0f
			|| !FMath::IsFinite(Body.MaxSpeed) || Body.MaxSpeed < 0.0f
			|| !FMath::IsFinite(Body.Acceleration) || Body.Acceleration < 0.0f
			|| !FMath::IsFinite(Body.AcceptanceRadius) || Body.AcceptanceRadius < 0.0f)
		{
			OutFailureReason = FString::Printf(TEXT("敌人实体 %d 的位置、速度、半径或移动参数无效。"), Body.EntityIndex);
			return false;
		}
		if (Body.bTrackingMainTower && (!FMath::IsFinite(Body.AttackRange) || Body.AttackRange <= 0.0f
			|| !FMath::IsFinite(Body.IdealStandOff) || Body.IdealStandOff <= 0.0f))
		{
			OutFailureReason = FString::Printf(TEXT("敌人实体 %d 的塔面攻击范围或理想贴靠距离无效。"), Body.EntityIndex);
			return false;
		}
		if (Body.bTrackingMainTower && !Tower)
		{
			OutFailureReason = FString::Printf(TEXT("敌人实体 %d 追踪主塔时缺少有效塔身几何。"), Body.EntityIndex);
			return false;
		}
	}

	OutResults.SetNum(Bodies.Num());
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		OutResults[Index].Location = Bodies[Index].Location;
		OutResults[Index].Velocity = Bodies[Index].Velocity;
	}
	if (DeltaTime <= 0.0f || Bodies.IsEmpty()) { return true; }

	float LargestRadius = 0.0f;
	for (const FCastleMassFlightBody& Body : Bodies) { LargestRadius = FMath::Max(LargestRadius, Body.Radius); }
	const float MaximumNeighborDistance = FMath::Max(1.0f, LargestRadius * 2.0f * FlockShellScale);
	if (!FMath::IsFinite(MaximumNeighborDistance))
	{
		OutFailureReason = TEXT("Boids 邻域半径超出可表示范围。");
		return false;
	}
	const float CellSize = FMath::Max(100.0f, MaximumNeighborDistance / 3.0f);
	const int32 CellRange = FMath::CeilToInt(MaximumNeighborDistance / CellSize);
	if (!FMath::IsFinite(MaximumNeighborDistance) || !FMath::IsFinite(CellSize) || CellRange < 1 || CellRange > 3)
	{
		OutFailureReason = TEXT("Boids 邻域网格尺寸超出可表示范围。");
		return false;
	}
	const int32 SubstepCount = FMath::Max(1, FMath::CeilToInt(DeltaTime / MaxSubstepSeconds));
	const float SubstepSeconds = DeltaTime / static_cast<float>(SubstepCount);
	TArray<FVector> Locations;
	TArray<FVector> Velocities;
	TArray<bool> HasTargets;
	Locations.Reserve(Bodies.Num());
	Velocities.Reserve(Bodies.Num());
	for (const FCastleMassFlightBody& Body : Bodies)
	{
		Locations.Add(Body.Location);
		Velocities.Add(Body.Velocity);
		HasTargets.Add(Body.bHasTarget);
	}

	const FVector TowerUp = Tower ? Tower->GetUpVector() : FVector::UpVector;
	const auto ProjectGroundAndTowerTop = [Tower](FVector& Location, FVector& Velocity)
	{
		if (!Tower) { return; }
		FVector Local = Tower->ToLocalPoint(Location);
		if (Local.Z < 0.0f)
		{
			Local.Z = 0.0f;
			const float DownwardSpeed = FVector::DotProduct(Velocity, Tower->GetUpVector());
			if (DownwardSpeed < 0.0f) { Velocity -= Tower->GetUpVector() * DownwardSpeed; }
		}
		else if (Local.Z > Tower->HalfSize.Z)
		{
			Local.Z = Tower->HalfSize.Z;
			const float UpwardSpeed = FVector::DotProduct(Velocity, Tower->GetUpVector());
			if (UpwardSpeed > 0.0f) { Velocity -= Tower->GetUpVector() * UpwardSpeed; }
		}
		Location = Tower->BottomCenter + Tower->Rotation.RotateVector(Local);
	};

	for (int32 Substep = 0; Substep < SubstepCount; ++Substep)
	{
		TMap<FIntVector, TArray<int32>> SpatialGrid;
		SpatialGrid.Reserve(Bodies.Num());
		TArray<FIntVector> GridKeys;
		GridKeys.SetNumUninitialized(Bodies.Num());
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			const FVector Center = Locations[Index] + TowerUp * Bodies[Index].Radius;
			if (!TryMakeGridKey(Center, CellSize, CellRange, GridKeys[Index]))
			{
				OutFailureReason = FString::Printf(TEXT("敌人实体 %d 的位置超出 Boids 邻域网格索引范围。"), Bodies[Index].EntityIndex);
				return false;
			}
			SpatialGrid.FindOrAdd(GridKeys[Index]).Add(Index);
		}

		TArray<float> TowerDistances;
		TArray<FVector> TowerPoints;
		TArray<FVector> TowerNormals;
		TowerDistances.SetNumZeroed(Bodies.Num());
		TowerPoints.SetNumZeroed(Bodies.Num());
		TowerNormals.SetNumZeroed(Bodies.Num());
		if (Tower)
		{
			for (int32 Index = 0; Index < Bodies.Num(); ++Index)
			{
				const FVector Center = Locations[Index] + TowerUp * Bodies[Index].Radius;
				if (!Tower->QuerySurface(Center, TowerPoints[Index], TowerNormals[Index], TowerDistances[Index]))
				{
					OutFailureReason = FString::Printf(TEXT("主塔表面查询失败，关联敌人实体 %d。"), Bodies[Index].EntityIndex);
					return false;
				}
			}
		}

		TArray<FVector> NextLocations = Locations;
		TArray<FVector> NextVelocities = Velocities;
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			const FCastleMassFlightBody& Body = Bodies[Index];
			if (Body.bSpawnBurst) { continue; }

			const FVector CurrentLocation = Locations[Index];
			const FVector CurrentVelocity = Velocities[Index];
			const FVector Center = CurrentLocation + TowerUp * Body.Radius;
			const FIntVector Cell = GridKeys[Index];
			FVector Separation = FVector::ZeroVector;
			FVector Alignment = FVector::ZeroVector;
			FVector Cohesion = FVector::ZeroVector;
			FVector BlockingCentroid = FVector::ZeroVector;
			int32 SeparationCount = 0;
			int32 FlockCount = 0;
			int32 BlockingCount = 0;
			FVector ApproachDirection = FVector::ZeroVector;
			if (Tower)
			{
				ApproachDirection = (TowerPoints[Index] - Center).GetSafeNormal();
				if (ApproachDirection.IsNearlyZero()) { ApproachDirection = -TowerNormals[Index]; }
			}

			for (int32 X = -CellRange; X <= CellRange; ++X)
			{
				for (int32 Y = -CellRange; Y <= CellRange; ++Y)
				{
					for (int32 Z = -CellRange; Z <= CellRange; ++Z)
					{
						const TArray<int32>* Neighbors = SpatialGrid.Find(Cell + FIntVector(X, Y, Z));
						if (!Neighbors) { continue; }
						for (const int32 OtherIndex : *Neighbors)
						{
							if (OtherIndex == Index) { continue; }
							const FCastleMassFlightBody& Other = Bodies[OtherIndex];
							if (Other.bStopMovement) { continue; }
							const FVector OtherCenter = Locations[OtherIndex] + TowerUp * Other.Radius;
							const FVector Offset = Center - OtherCenter;
							const double DistanceSquared = Offset.SizeSquared();
							const float PairRadius = Body.Radius + Other.Radius;
							if (PairRadius <= 0.0f) { continue; }
							const double Distance = FMath::Sqrt(DistanceSquared);
							const float SeparationShell = PairRadius * SeparationShellScale;
							if (Distance < SeparationShell)
							{
								const FVector Away = Distance > UE_SMALL_NUMBER
									? Offset / Distance : MakeStablePairDirection(Body.EntityIndex, Other.EntityIndex);
								const float MinimumSpacing = PairRadius * MinimumAgentSpacingScale;
								float Weight = (SeparationShell - static_cast<float>(Distance)) / SeparationShell;
								if (Distance < MinimumSpacing)
								{
									Weight += (MinimumSpacing - static_cast<float>(Distance)) / MinimumSpacing * 2.0f;
								}
								Separation += Away * Weight;
								++SeparationCount;
							}
							if (Distance >= PairRadius * FlockShellScale) { continue; }
							Alignment += Velocities[OtherIndex];
							Cohesion += OtherCenter;
							++FlockCount;

							if (Tower && TowerDistances[Index] <= TowerFlowMargin
								&& TowerDistances[OtherIndex] < TowerDistances[Index]
								&& FVector::DotProduct((OtherCenter - Center).GetSafeNormal(), ApproachDirection) > 0.15f)
							{
								BlockingCentroid += OtherCenter;
								++BlockingCount;
							}
						}
					}
				}
			}

			if (SeparationCount > 0) { Separation /= static_cast<float>(SeparationCount); }
			if (Body.bTrackingMainTower && Body.bInAttackRange)
			{
				// 攻击态只收敛塔面法向速度；同伴推力仍能沿完整三维切平面滑移。
				const FVector Normal = TowerNormals[Index];
				const float NormalLoad = FMath::Max(0.0f, -FVector::DotProduct(Separation, Normal));
				Separation -= Normal * FVector::DotProduct(Separation, Normal);
				if (NormalLoad > UE_SMALL_NUMBER)
				{
					// 后排正对前排施压时，将一部分压墙力转换为稳定切向滑移，避免整排钉死。
					Separation += MakeStableTangent(Normal, Body.EntityIndex) * NormalLoad * 0.65f;
				}
			}

			FVector DesiredVelocity = FVector::ZeroVector;
			bool bReachedGenericTarget = false;
			if (Body.bTrackingMainTower && Tower)
			{
				if (Body.bInAttackRange)
				{
					const float NormalError = Body.IdealStandOff - TowerDistances[Index];
					const float NormalSpeed = FMath::Clamp(NormalError * 2.5f, -Body.MaxSpeed, Body.MaxSpeed);
					DesiredVelocity = TowerNormals[Index] * NormalSpeed;
				}
				else
				{
					FVector ToStandOff = TowerPoints[Index] + TowerNormals[Index] * Body.IdealStandOff - Center;
					if (ToStandOff.IsNearlyZero()) { ToStandOff = ApproachDirection; }
					DesiredVelocity = ToStandOff.GetSafeNormal() * Body.MaxSpeed;
				}
			}
			else if (HasTargets[Index])
			{
				const FVector ToTarget = Body.Target - CurrentLocation;
				const double Distance = ToTarget.Size();
				if (Distance <= Body.AcceptanceRadius)
				{
					OutResults[Index].bReachedTarget = true;
					bReachedGenericTarget = true;
					HasTargets[Index] = false;
				}
				else
				{
					const float BrakingSpeed = Body.Acceleration > 0.0f
						? FMath::Min(Body.MaxSpeed, FMath::Sqrt(2.0f * Body.Acceleration
							* static_cast<float>(Distance - Body.AcceptanceRadius)))
						: Body.MaxSpeed;
					DesiredVelocity = ToTarget.GetSafeNormal() * BrakingSpeed;
				}
			}
			else
			{
				// SetUnitsVelocity 仍通过移动目标片段提供无目标的期望速度。
				DesiredVelocity = Body.DesiredVelocity.GetClampedToMaxSize(Body.MaxSpeed);
			}

			const float TowerFlockAttenuation = Tower ? SmoothTowerAttenuation(TowerDistances[Index]) : 1.0f;
			if (FlockCount > 0)
			{
				Alignment = Alignment / static_cast<float>(FlockCount) - CurrentVelocity;
				Cohesion = (Cohesion / static_cast<float>(FlockCount) - Center).GetSafeNormal() * Body.MaxSpeed;
				DesiredVelocity += (Alignment * AlignmentWeight + Cohesion * CohesionWeight) * TowerFlockAttenuation;
			}
			if (SeparationCount > 0) { DesiredVelocity += Separation.GetSafeNormal() * Body.MaxSpeed * 1.5f; }
			if (BlockingCount > 0)
			{
				BlockingCentroid /= static_cast<float>(BlockingCount);
				FVector TangentAway = Center - BlockingCentroid;
				TangentAway -= TowerNormals[Index] * FVector::DotProduct(TangentAway, TowerNormals[Index]);
				if (TangentAway.IsNearlyZero()) { TangentAway = MakeStableTangent(TowerNormals[Index], Body.EntityIndex); }
				DesiredVelocity += TangentAway.GetSafeNormal() * Body.MaxSpeed * TangentialFlowWeight;
			}
			DesiredVelocity = DesiredVelocity.GetClampedToMaxSize(Body.MaxSpeed);

			FVector NewVelocity = Body.bStopMovement || bReachedGenericTarget ? FVector::ZeroVector
				: (Body.Acceleration > 0.0f
					? FMath::VInterpConstantTo(CurrentVelocity, DesiredVelocity, SubstepSeconds, Body.Acceleration)
					: DesiredVelocity);
			if (NewVelocity.SizeSquared() > FMath::Square(Body.MaxSpeed))
			{
				NewVelocity = NewVelocity.GetClampedToMaxSize(Body.MaxSpeed);
			}
			FVector NewLocation = Body.bStopMovement || bReachedGenericTarget
				? CurrentLocation : CurrentLocation + NewVelocity * SubstepSeconds;
			if (Tower)
			{
				FVector SphereCenter = NewLocation + TowerUp * Body.Radius;
				if (!Tower->ProjectSphereOutside(SphereCenter, Body.Radius, NewVelocity))
				{
					OutFailureReason = FString::Printf(TEXT("敌人实体 %d 的主塔球体投影失败。"), Body.EntityIndex);
					return false;
				}
				NewLocation = SphereCenter - TowerUp * Body.Radius;
				ProjectGroundAndTowerTop(NewLocation, NewVelocity);
			}
			NextLocations[Index] = NewLocation;
			NextVelocities[Index] = NewVelocity;
		}

		// 所有新位置都依赖同一份子步快照，整批算完才提交到下一子步。
		Locations = MoveTemp(NextLocations);
		Velocities = MoveTemp(NextVelocities);
	}

	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		OutResults[Index].Location = Locations[Index];
		OutResults[Index].Velocity = Bodies[Index].bStopMovement ? FVector::ZeroVector : Velocities[Index];
	}
	return true;
}
