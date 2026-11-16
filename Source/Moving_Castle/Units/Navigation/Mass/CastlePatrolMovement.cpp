#include "Mass/CastlePatrolMovement.h"

#include "CastleNavigationSubsystem.h"
#include "CastleMassTypes.h"

bool CastlePatrolMovement::Advance(const FCastleSharedNavigationPath& Path, const FCastleMassMovementSettings& Settings,
	const double StartDistance, const double DeltaTime, double& InOutSpeed, double& OutTravel)
{
	OutTravel = 0.0;
	if (Path.Length <= UE_DOUBLE_SMALL_NUMBER) { InOutSpeed = 0.0; return true; }
	double RemainingTime = DeltaTime;
	double Cursor = StartDistance;
	while (RemainingTime > UE_DOUBLE_SMALL_NUMBER)
	{
		double Boundary = Path.Length;
		FVector Facing;
		const bool bLadder = Path.FindLadderRange(Cursor, Boundary, Facing);
		if (bLadder && (!FMath::IsFinite(Settings.LadderSpeedMultiplier)
			|| Settings.LadderSpeedMultiplier <= 0.01f || Settings.LadderSpeedMultiplier > 10.0f))
		{
			InOutSpeed = 0.0;
			return false;
		}
		const double Limit = Settings.MaxSpeed * (bLadder ? Settings.LadderSpeedMultiplier : 1.0);
		if (Limit <= 0.0) { InOutSpeed = 0.0; return true; }
		// 与原巡逻语义一致：降低上限立即限速；加速仍按配置积分，不能把整个大帧当加速段。
		const double Speed = FMath::Min(InOutSpeed, Limit);
		const double Acceleration = Settings.Acceleration;
		const double AccelerationTime = Acceleration > 0.0 ? (Limit - Speed) / Acceleration : 0.0;
		const double AccelerationDistance = (Speed + Limit) * AccelerationTime * 0.5;
		const double Span = Boundary - Cursor;
		const double BoundaryTime = Acceleration > 0.0 && Span < AccelerationDistance
			? 2.0 * Span / (FMath::Sqrt(Speed * Speed + 2.0 * Acceleration * Span) + Speed)
			: AccelerationTime + (Span - AccelerationDistance) / Limit;
		const double StepTime = FMath::Min(RemainingTime, BoundaryTime);
		const double AcceleratingTime = FMath::Min(StepTime, AccelerationTime);
		const double StepDistance = Speed * AcceleratingTime + 0.5 * Acceleration * AcceleratingTime * AcceleratingTime
			+ Limit * (StepTime - AcceleratingTime);
		InOutSpeed = Acceleration > 0.0 ? FMath::Min(Limit, Speed + Acceleration * StepTime) : Limit;
		// 到达边界时使用精确累计距离，避免浮点误差让下一轮停留在同一边界前反复积分。
		OutTravel += StepTime >= BoundaryTime ? Span : StepDistance;
		Cursor = StepTime >= BoundaryTime ? (Boundary == Path.Length ? 0.0 : Boundary) : Cursor + StepDistance;
		RemainingTime -= StepTime;
	}
	return true;
}
