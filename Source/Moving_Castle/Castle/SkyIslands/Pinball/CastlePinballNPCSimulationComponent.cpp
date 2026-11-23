#include "CastlePinballNPCSimulationComponent.h"

#include "Moving_Castle.h"

namespace
{
	// 每次发射的初速固定，转向只改变速度分量。两类碰撞共用不超过 1 的恢复系数。
	constexpr double LaunchSpeed = 375.0;
	constexpr double Restitution = 0.78;
	constexpr double SideLimit = CastlePinball::HalfWidth - CastlePinball::CoinRadius;
	constexpr double TopLimit = -CastlePinball::HalfLength + CastlePinball::CoinRadius;
	constexpr double ContactSkin = 0.01;
	constexpr int32 MaxContactsPerStep = 16;

	enum class EContact : uint8 { None, Wall, Peg, NPC, Drain };

	// 求相对运动线段进入圆的第一时刻。NPC 已与金币重叠时也应计一次命中。
	double CircleContact(const FVector2D& Offset, const FVector2D& Delta, double Radius, bool bAcceptOverlap)
	{
		const double C = Offset.SizeSquared() - FMath::Square(Radius);
		const double B = FVector2D::DotProduct(Offset, Delta);
		if (C <= 0.0) { return bAcceptOverlap || B < 0.0 ? 0.0 : 2.0; }
		const double A = Delta.SizeSquared();
		const double Discriminant = FMath::Square(B) - A * C;
		if (A <= UE_SMALL_NUMBER || B >= 0.0 || Discriminant < 0.0) { return 2.0; }
		return (-B - FMath::Sqrt(Discriminant)) / A;
	}
}

bool UCastlePinballNPCSimulationComponent::Initialize(
	const UCastlePinballNPCSkyIslandDataAsset& InConfig, int32 Seed, FText& OutReason)
{
	using namespace CastlePinball;
	if (!InConfig.Validate(OutReason)) { return false; }
	Clear();
	Config = &InConfig;
	DownslopeGravity = 980.0 * FMath::Sin(FMath::DegreesToRadians(static_cast<double>(InConfig.TiltAngleDegrees)));
	// 单步内新增的金币绝不会超过这个容量，父金币引用在生成子金币时保持有效。
	Coins.Reserve(InConfig.MaxActiveCoins);
	NPCs.Reserve(InConfig.InitialNPCCount);
	NPCStepStarts.SetNum(InConfig.InitialNPCCount);
	FRandomStream Random(Seed);
	const int32 Columns = FMath::Min(6, InConfig.InitialNPCCount);
	const int32 Rows = FMath::DivideAndRoundUp(InConfig.InitialNPCCount, Columns);
	const double LaneSpacing = 2.0 * HalfWidth / Columns;
	for (int32 Index = 0; Index < InConfig.InitialNPCCount; ++Index)
	{
		FCastlePinballNPC& NPC = NPCs.AddDefaulted_GetRef();
		NPC.LaneCenter = -HalfWidth + (Index % Columns + 0.5) * LaneSpacing;
		NPC.HalfPatrolLength = LaneSpacing * 0.24;
		NPC.LateralSpeed = (Index % 2 == 0 ? 1.0 : -1.0) * (39.0 + Index % 5 * 7.0);
		const double X = 135.0 + (Index / Columns) * (Rows > 1 ? 310.0 / (Rows - 1) : 0.0);
		NPC.Position = FVector2D(X, NPC.LaneCenter + Random.FRandRange(-0.8, 0.8) * NPC.HalfPatrolLength);
	}
	return true;
}

void UCastlePinballNPCSimulationComponent::Clear()
{
	// 岛的 EndPlay 先解除外部回调，再清理状态；初始化可以复用同一组件。
	Config = nullptr;
	Coins.Reset();
	NPCs.Reset();
	NPCStepStarts.Reset();
	TimeRemainder = 0.0;
	DownslopeGravity = 0.0;
	NextCoinId = 1;
	RecruitedCount = 0;
	bFailed = false;
}

bool UCastlePinballNPCSimulationComponent::CanLaunch(double LateralPosition, double Angle, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!Config || bFailed || !(FMath::Abs(LateralPosition) <= SideLimit)
		|| !(Angle >= Config->LauncherMinAngle && Angle <= Config->LauncherMaxAngle))
	{
		OutReason = INVTEXT("弹珠机场地或发射参数无效。");
		UE_LOG(LogMoving_Castle, Error, TEXT("%s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	if (RecruitedCount == NPCs.Num())
	{
		OutReason = INVTEXT("所有 NPC 已招募。");
		return false;
	}
	if (Coins.Num() >= Config->MaxActiveCoins)
	{
		OutReason = INVTEXT("场内金币已达到上限，请等待金币离场。");
		UE_LOG(LogMoving_Castle, Warning, TEXT("%s：%s"), *Config->GetPathName(), *OutReason.ToString());
		return false;
	}
	return true;
}

void UCastlePinballNPCSimulationComponent::CommitLaunch(double LateralPosition, double Angle)
{
	using namespace CastlePinball;
	check(Config && !bFailed && Coins.Num() < Config->MaxActiveCoins);
	FCastlePinballCoin& Coin = Coins.AddDefaulted_GetRef();
	Coin.Id = NextCoinId++;
	Coin.Position = FVector2D(LaunchX, LateralPosition);
	const double Radians = FMath::DegreesToRadians(Angle);
	Coin.Velocity = LaunchSpeed * FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
}

bool UCastlePinballNPCSimulationComponent::Advance(double DeltaSeconds, FText& OutReason)
{
	using namespace CastlePinball;
	OutReason = FText::GetEmpty();
	if (!Config || bFailed || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0)
	{
		OutReason = INVTEXT("弹珠机场地或模拟时长无效。");
		UE_LOG(LogMoving_Castle, Error, TEXT("%s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	TimeRemainder += DeltaSeconds;
	// 只消费完整的一步，保留帧尾余额。比较中的容差吸收浮点加减产生的累计舍入误差。
	while (TimeRemainder + UE_DOUBLE_SMALL_NUMBER >= FixedStep)
	{
		if (!Step(OutReason))
		{
			bFailed = true;
			UE_LOG(LogMoving_Castle, Error, TEXT("弹珠机配置 %s 停止模拟：%s"), *Config->GetPathName(), *OutReason.ToString());
			return false;
		}
		TimeRemainder -= FixedStep;
	}
	return true;
}

bool UCastlePinballNPCSimulationComponent::Step(FText& OutReason)
{
	using namespace CastlePinball;
	for (int32 Index = 0; Index < NPCs.Num(); ++Index)
	{
		FCastlePinballNPC& NPC = NPCs[Index];
		NPCStepStarts[Index] = NPC.Position;
		if (NPC.bRecruited) { continue; }

		// 把超出的距离沿原轨迹反射，折返点不丢一步的移动量。
		NPC.Position.Y += NPC.LateralSpeed * FixedStep;
		const double Offset = NPC.Position.Y - NPC.LaneCenter;
		if (FMath::Abs(Offset) > NPC.HalfPatrolLength)
		{
			const double Edge = NPC.LaneCenter + FMath::Sign(Offset) * NPC.HalfPatrolLength;
			NPC.Position.Y = 2.0 * Edge - NPC.Position.Y;
			NPC.LateralSpeed = -NPC.LateralSpeed;
		}
	}

	// 固定初始批次，子金币从下一步开始运动。稳定移除以保持同帧命中的结算顺序。
	int32 InitialCount = Coins.Num();
	for (int32 Index = 0; Index < InitialCount;)
	{
		ECoinStepResult Result = AdvanceCoin(Coins[Index], OutReason);
		if (Result == ECoinStepResult::Error) { return false; }
		if (Result == ECoinStepResult::Remove)
		{
			Coins.RemoveAt(Index, 1, EAllowShrinking::No);
			--InitialCount;
		}
		else
		{
			++Index;
		}
	}
	return true;
}

UCastlePinballNPCSimulationComponent::ECoinStepResult UCastlePinballNPCSimulationComponent::AdvanceCoin(
	FCastlePinballCoin& Coin, FText& OutReason)
{
	using namespace CastlePinball;
	Coin.Velocity.X += DownslopeGravity * FixedStep;
	double Remaining = FixedStep;
	for (int32 ContactCount = 0; Remaining > UE_DOUBLE_SMALL_NUMBER; ++ContactCount)
	{
		if (ContactCount == MaxContactsPerStep)
		{
			OutReason = FText::Format(INVTEXT("金币 {0} 单步内接触过多，请检查反弹钉与护栏布局。"), Coin.Id);
			return ECoinStepResult::Error;
		}
		const FVector2D Start = Coin.Position;
		const FVector2D Delta = Coin.Velocity * Remaining;
		const FVector2D End = Start + Delta;
		double Fraction = 1.0;
		EContact Contact = EContact::None;
		int32 ContactIndex = INDEX_NONE;
		FVector2D Normal = FVector2D::ZeroVector;

		// 取最早的接触。接触恰在步尾时也在本步结算。
		const auto Consider = [&](double Time, EContact Kind, int32 Index, FVector2D InNormal)
		{
			if (Time >= 0.0 && (Time < Fraction || (Contact == EContact::None && Time == Fraction)))
			{
				Fraction = Time;
				Contact = Kind;
				ContactIndex = Index;
				Normal = InNormal;
			}
		};
		if (Delta.X > 0.0 && End.X >= HalfLength) { Consider((HalfLength - Start.X) / Delta.X, EContact::Drain, INDEX_NONE, {}); }
		if (Delta.X < 0.0 && End.X <= TopLimit) { Consider((TopLimit - Start.X) / Delta.X, EContact::Wall, INDEX_NONE, {1.0, 0.0}); }
		if (Delta.Y > 0.0 && End.Y >= SideLimit) { Consider((SideLimit - Start.Y) / Delta.Y, EContact::Wall, INDEX_NONE, {0.0, -1.0}); }
		if (Delta.Y < 0.0 && End.Y <= -SideLimit) { Consider((-SideLimit - Start.Y) / Delta.Y, EContact::Wall, INDEX_NONE, {0.0, 1.0}); }

		for (int32 Index = 0; Index < Config->Pegs.Num(); ++Index)
		{
			const FCastlePinballPeg& Peg = Config->Pegs[Index];
			const FVector2D Offset = Start - Peg.Center;
			const double Time = CircleContact(Offset, Delta, CoinRadius + Peg.Radius, false);
			if (Time <= 1.0)
			{
				Consider(Time, EContact::Peg, Index, (Offset + Delta * Time).GetSafeNormal());
			}
		}
		for (int32 Index = 0; Index < NPCs.Num(); ++Index)
		{
			const FCastlePinballNPC& NPC = NPCs[Index];
			if (NPC.bRecruited) { continue; }
			const FVector2D NPCDelta = (NPC.Position - NPCStepStarts[Index]) * (Remaining / FixedStep);
			const FVector2D NPCStart = NPC.Position - NPCDelta;
			Consider(CircleContact(Start - NPCStart, Delta - NPCDelta, CoinRadius + NPCRadius, true),
				EContact::NPC, Index, {});
		}

		const FVector2D Travel = Delta * Fraction;
		Coin.Position += Travel;
		const double Distance = Travel.Size();
		if (Distance > UE_DOUBLE_SMALL_NUMBER)
		{
			const FVector Axis(-Travel.Y / Distance, Travel.X / Distance, 0.0);
			Coin.Rotation = (FQuat(Axis, Distance / CoinRadius) * Coin.Rotation).GetNormalized();
		}
		CrossGates(Coin, Start, Coin.Position);
		if (Contact == EContact::NPC) { return HitNPC(ContactIndex, OutReason) ? ECoinStepResult::Remove : ECoinStepResult::Error; }
		if (Contact == EContact::Drain) { return ECoinStepResult::Remove; }
		if (Contact == EContact::None) { break; }

		// 将接触点沿法线分开极小距离，避免相同接触在零时间重复结算。
		Coin.Position += Normal * ContactSkin;
		Coin.Velocity -= (1.0 + Restitution) * FVector2D::DotProduct(Coin.Velocity, Normal) * Normal;
		Remaining *= 1.0 - Fraction;
	}
	return ECoinStepResult::Active;
}

void UCastlePinballNPCSimulationComponent::CrossGates(FCastlePinballCoin& Coin, const FVector2D& From, const FVector2D& To)
{
	if (From.X == To.X) { return; }
	struct FCrossing { double Fraction; int32 GateIndex; };
	TArray<FCrossing, TInlineAllocator<8>> Crossings;
	for (int32 Index = 0; Index < Config->MultiplierGates.Num(); ++Index)
	{
		if (Coin.TriggeredGates.Contains(Index)) { continue; }
		const FCastlePinballGate& Gate = Config->MultiplierGates[Index];
		const double Fraction = (Gate.Center.X - From.X) / (To.X - From.X);
		if (Fraction > 0.0 && Fraction <= 1.0
			&& FMath::Abs(FMath::Lerp(From.Y, To.Y, Fraction) - Gate.Center.Y) <= Gate.Width * 0.5)
		{
			Crossings.Add({Fraction, Index});
		}
	}
	Crossings.Sort([](const FCrossing& A, const FCrossing& B)
	{
		return A.Fraction == B.Fraction ? A.GateIndex < B.GateIndex : A.Fraction < B.Fraction;
	});
	for (const FCrossing& Crossing : Crossings)
	{
		const FCastlePinballGate& Gate = Config->MultiplierGates[Crossing.GateIndex];
		// 达上限时也记作已过门，避免原金币重返本门后重复尝试增殖。
		Coin.TriggeredGates.Add(Crossing.GateIndex);
		const int32 Requested = Gate.Mode == ECastlePinballGateMode::Multiply ? Gate.Value - 1 : Gate.Value;
		const int32 SpawnCount = FMath::Min(Requested, Config->MaxActiveCoins - Coins.Num());
		if (SpawnCount < Requested)
		{
			UE_LOG(LogMoving_Castle, Warning, TEXT("弹珠机配置 %s 达到在场金币上限 %d，门 %d 限制额外金币 %d → %d。"),
				*Config->GetPathName(), Config->MaxActiveCoins, Crossing.GateIndex + 1, Requested, SpawnCount);
		}
		for (int32 ChildIndex = 0; ChildIndex < SpawnCount; ++ChildIndex)
		{
			const double Side = ChildIndex % 2 == 0 ? -1.0 : 1.0;
			const double Spread = Side * (9.0 + 9.0 * (ChildIndex / 2) / FMath::Max(1, SpawnCount / 2));
			FCastlePinballCoin& Child = Coins.AddDefaulted_GetRef();
			Child.Id = NextCoinId++;
			Child.Position = FMath::Lerp(From, To, Crossing.Fraction);
			Child.Velocity = Coin.Velocity.GetRotated(Spread);
			Child.TriggeredGates.Add(Crossing.GateIndex);
		}
	}
}

bool UCastlePinballNPCSimulationComponent::HitNPC(int32 Index, FText& OutReason)
{
	FCastlePinballNPC& NPC = NPCs[Index];
	if (NPC.Hits + 1 == Config->CoinsRequiredPerNPC)
	{
		// 同步结算前先锁住命中资格；导演拒绝时不计这枚金币，保留离岛清理入口。
		NPC.bRecruited = true;
		if (!OnCommitRecruit.IsBound()) { OutReason = INVTEXT("弹珠机缺少导演招募回调。"); }
		if (!OnCommitRecruit.IsBound() || !OnCommitRecruit.Execute(Index, OutReason))
		{
			NPC.bRecruited = false;
			return false;
		}
		++RecruitedCount;
	}
	++NPC.Hits;
	return true;
}
