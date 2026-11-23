#include "CastlePinballNPCSkyIslandDataAsset.h"

#include "Moving_Castle.h"

UCastlePinballNPCSkyIslandDataAsset::UCastlePinballNPCSkyIslandDataAsset()
{
	// 第一排挡钉在空着的中路两侧，后排门和柱交错，方便玩家观察直投和反弹路径。
	MultiplierGates = {
		{{-250.0, -165.0}, 150.0, ECastlePinballGateMode::Multiply, 2},
		{{-250.0, 0.0}, 150.0, ECastlePinballGateMode::Add, 2},
		{{-90.0, 0.0}, 160.0, ECastlePinballGateMode::Multiply, 2},
		{{-90.0, 170.0}, 150.0, ECastlePinballGateMode::Add, 2}
	};
	Pegs = {
		{{-350.0, -100.0}, 19.0}, {{-350.0, 100.0}, 19.0},
		{{-170.0, -236.0}, 20.0}, {{-170.0, -94.0}, 20.0}, {{-170.0, 94.0}, 20.0},
		{{25.0, -152.0}, 22.0}, {{25.0, 122.0}, 22.0}, {{80.0, 238.0}, 22.0}
	};
}

bool UCastlePinballNPCSkyIslandDataAsset::Validate(FText& OutReason) const
{
	using namespace CastlePinball;
	OutReason = FText::GetEmpty();
	const auto Reject = [this, &OutReason](const FText& Reason)
	{
		OutReason = Reason;
		UE_LOG(LogMoving_Castle, Error, TEXT("弹珠机配置 %s：%s"), *GetPathName(), *Reason.ToString());
		return false;
	};
	const auto InRange = [](double Value, double Min, double Max) { return Value >= Min && Value <= Max; };
	if (!InRange(TiltAngleDegrees, 5.0, 60.0)) { return Reject(INVTEXT("倾角必须在 5° 到 60° 之间。")); }
	if (!(WallRestitution > 0.0f && WallRestitution <= 1.2f))
	{
		return Reject(INVTEXT("WallRestitution 必须大于 0 且不超过 1.2。"));
	}
	if (!(PegRestitution > 0.0f && PegRestitution <= 1.5f))
	{
		return Reject(INVTEXT("PegRestitution 必须大于 0 且不超过 1.5。"));
	}
	if (!InRange(PegBumperImpulse, 0.0, 800.0)) { return Reject(INVTEXT("PegBumperImpulse 必须在 0 到 800 cm/s 之间。")); }
	if (!InRange(MaxCoinSpeed, 400.0, 5000.0)) { return Reject(INVTEXT("MaxCoinSpeed 必须在 400 到 5000 cm/s 之间。")); }
	if (CoinLaunchCost < 1) { return Reject(INVTEXT("投币费用必须至少为 1。")); }
	if (!InRange(InitialNPCCount, 1, 20)) { return Reject(INVTEXT("初始 NPC 数量必须在 1 到 20 之间。")); }
	if (!InRange(CoinsRequiredPerNPC, 1, 50)) { return Reject(INVTEXT("招募金币数必须在 1 到 50 之间。")); }
	if (!InRange(MaxActiveCoins, 10, 1000)) { return Reject(INVTEXT("在场上限必须在 10 到 1000 之间。")); }
	if (!(LauncherMinAngle >= -75.0f && LauncherMinAngle < LauncherMaxAngle && LauncherMaxAngle <= 75.0f))
	{
		return Reject(INVTEXT("发射角须满足 -75° ≤ 最小值 < 最大值 ≤ 75°。"));
	}
	if (MultiplierGates.IsEmpty()) { return Reject(INVTEXT("至少需要一扇增殖门。")); }

	// 逐项验证局部坐标，使版面标线和实际触发区域始终对应，包括拒绝 NaN。
	for (int32 Index = 0; Index < MultiplierGates.Num(); ++Index)
	{
		const FCastlePinballGate& Gate = MultiplierGates[Index];
		if (!InRange(Gate.Center.X, -HalfLength, HalfLength) || !(Gate.Width > 0.0)
			|| !(FMath::Abs(Gate.Center.Y) + Gate.Width * 0.5 <= HalfWidth)
			|| Gate.Value < 1 || (Gate.Mode != ECastlePinballGateMode::Multiply && Gate.Mode != ECastlePinballGateMode::Add))
		{
			return Reject(FText::Format(INVTEXT("增殖门 {0} 的位置、宽度、模式或数值无效。"), Index + 1));
		}
	}
	for (int32 Index = 0; Index < Pegs.Num(); ++Index)
	{
		const FCastlePinballPeg& Peg = Pegs[Index];
		if (!(Peg.Radius > 0.0 && FMath::Abs(Peg.Center.X) + Peg.Radius <= HalfLength
			&& FMath::Abs(Peg.Center.Y) + Peg.Radius <= HalfWidth))
		{
			return Reject(FText::Format(INVTEXT("反弹钉 {0} 的半径或位置超出斜面。"), Index + 1));
		}
	}
	return true;
}
