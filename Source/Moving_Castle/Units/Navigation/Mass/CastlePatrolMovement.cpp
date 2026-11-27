#include "Mass/CastlePatrolMovement.h"

#include "CastleNavigationSubsystem.h"
#include "CastleMassTypes.h"

bool CastlePatrolMovement::Advance(const FCastleSharedNavigationPath& Path, const FCastleMassMovementSettings& Settings,
	const double StartDistance, const double DeltaTime, double& InOutSpeed, double& OutTravel, const bool bStopAtEnd)
{
	OutTravel = 0.0;
	if (Path.Length <= UE_DOUBLE_SMALL_NUMBER) { InOutSpeed = 0.0; return true; }
	double RemainingTime = DeltaTime;
	double Cursor = StartDistance;
	constexpr double RoadMultiplier = 1.5;
	double HighwayEnd;
	// 将上帧输出换回基础速度。道路直接乘位移速度，与梯子倍率及原有加速度积分叠乘。
	double BaseSpeed = InOutSpeed / (Path.FindHighwayRange(Cursor, HighwayEnd) ? RoadMultiplier : 1.0);
	while (RemainingTime > UE_DOUBLE_SMALL_NUMBER)
	{
		double Boundary = Path.Length;
		FVector Facing;
		const bool bLadder = Path.FindLadderRange(Cursor, Boundary, Facing);
		const double Scale = Path.FindHighwayRange(Cursor, HighwayEnd) ? RoadMultiplier : 1.0;
		Boundary = FMath::Min(Boundary, HighwayEnd);
		if (bLadder && (!FMath::IsFinite(Settings.LadderSpeedMultiplier)
			|| Settings.LadderSpeedMultiplier <= 0.01f || Settings.LadderSpeedMultiplier > 10.0f))
		{
			InOutSpeed = 0.0;
			return false;
		}
		const double Limit = Settings.MaxSpeed * (bLadder ? Settings.LadderSpeedMultiplier : 1.0);
		if (Limit <= 0.0) { InOutSpeed = 0.0; return true; }
		// 与原巡逻语义一致：降低上限立即限速；加速仍按配置积分，不能把整个大帧当加速段。
		const double Speed = FMath::Min(BaseSpeed, Limit);
		const double Acceleration = Settings.Acceleration;
		const double AccelerationTime = Acceleration > 0.0 ? (Limit - Speed) / Acceleration : 0.0;
		const double AccelerationDistance = (Speed + Limit) * AccelerationTime * 0.5;
		const double Span = (Boundary - Cursor) / Scale;
		const double BoundaryTime = Acceleration > 0.0 && Span < AccelerationDistance
			? 2.0 * Span / (FMath::Sqrt(Speed * Speed + 2.0 * Acceleration * Span) + Speed)
			: AccelerationTime + (Span - AccelerationDistance) / Limit;
		const double StepTime = FMath::Min(RemainingTime, BoundaryTime);
		const double AcceleratingTime = FMath::Min(StepTime, AccelerationTime);
		const double StepDistance = Speed * AcceleratingTime + 0.5 * Acceleration * AcceleratingTime * AcceleratingTime
			+ Limit * (StepTime - AcceleratingTime);
		BaseSpeed = Acceleration > 0.0 ? FMath::Min(Limit, Speed + Acceleration * StepTime) : Limit;
		// 到达边界时使用精确累计距离，避免浮点误差让下一轮停留在同一边界前反复积分。
		OutTravel += StepTime >= BoundaryTime ? Boundary - Cursor : StepDistance * Scale;
		// 空投临时路线只走一次；抵达终点后不消费余下时间重复行走。
		if (bStopAtEnd && StepTime >= BoundaryTime && Boundary == Path.Length) { InOutSpeed = 0.0; return true; }
		Cursor = StepTime >= BoundaryTime ? (Boundary == Path.Length ? 0.0 : Boundary) : Cursor + StepDistance * Scale;
		RemainingTime -= StepTime;
	}
	// 一帧恰好结束在格面时，也输出下一格的速度，避免保留一帧上一格的道路倍率。
	InOutSpeed = BaseSpeed * (Path.FindHighwayRange(Cursor, HighwayEnd) ? RoadMultiplier : 1.0);
	return true;
}
