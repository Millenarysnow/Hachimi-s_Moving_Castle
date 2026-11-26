#include "CastleSupportTowerSubsystem.h"

#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleSupportFlightActors.h"
#include "CastleSupportTowerData.h"
#include "CastleTowerAttackDataAsset.h"
#include "Components/Building/Effects/Attack/CastleCannonTowerAttackEffectComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Moving_Castle.h"

namespace
{
	constexpr double SlowSeconds = 5.0;
	constexpr double BuffSeconds = 10.0;

	FLinearColor FlightTint(const ECastleTileColor Color)
	{
		switch (Color)
		{
		case ECastleTileColor::Red: return FLinearColor(1.0f, 0.09f, 0.08f);
		case ECastleTileColor::Yellow: return FLinearColor(1.0f, 0.72f, 0.03f);
		case ECastleTileColor::Blue: return FLinearColor(0.05f, 0.35f, 1.0f);
		case ECastleTileColor::Green: return FLinearColor(0.06f, 0.82f, 0.24f);
		default: checkNoEntry(); return FLinearColor::Black;
		}
	}
}

bool UCastleSupportTowerSubsystem::LaunchBalloon(ACastleBuildingBase& Source, ACastleBoard& Board,
	const UCastleSupportTowerData& Data, const int64 Number)
{
	FBalloon Flight;
	Flight.Source = &Source;
	Flight.Board = &Board;
	Flight.Level = Source.GetBuildingLevel();
	// 落地减速直接使用有效数字百分比，先拒绝会产生零或负速度的配置。
	if (Number < 1 || Number >= 100)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 的有效数字 %lld 会产生无效减速倍率。"), *Source.GetPathName(), Number);
		return false;
	}
	Flight.Number = static_cast<int32>(Number);
	Flight.Color = Source.GetBuildingColor();
	Flight.Health = Data.BalloonHealth;
	const TArray<FIntVector> Cells = Source.GetOccupiedCoordinates();
	Flight.Sum = Flight.Number * Cells.Num();
	// 从最高层字典序最前的占格上方起飞，不从大模型中心的缝隙起飞。
	FIntVector AnchorCell = Cells[0];
	for (const FIntVector& Cell : Cells)
	{
		if (Cell.Z > AnchorCell.Z || (Cell.Z == AnchorCell.Z && (Cell.X < AnchorCell.X
			|| (Cell.X == AnchorCell.X && Cell.Y < AnchorCell.Y)))) { AnchorCell = Cell; }
	}
	const FVector Origin = Board.GetGridWorldLocation(AnchorCell);
	for (const FVector& Point : Data.BalloonPath) { Flight.Path.Add(Origin + Board.GetActorTransform().TransformVectorNoScale(Point)); }
	ACastleSupplyBalloon* Balloon = GetWorld()->SpawnActor<ACastleSupplyBalloon>(Flight.Path[0], Board.GetActorRotation());
	const uint64 Id = NextBalloonId++;
	if (!CastleTileColors::IsPlayable(Flight.Color)
		|| !Balloon || !Balloon->Configure(Id, Data.FlightMaterial, FlightTint(Flight.Color)))
	{
		if (Balloon) { Balloon->Destroy(); }
		UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 起飞失败：颜色或气球表现无效，资产=%s。"), *Source.GetPathName(), *Data.GetPathName());
		return false;
	}
	Flight.Visual = Balloon;
	Balloons.Add(Id, MoveTemp(Flight));
	UE_LOG(LogMoving_Castle, Display, TEXT("空投塔 Lv%d %s：气球 %llu 起飞。"), Source.GetBuildingLevel(), *Source.GetName(), Id);
	return true;
}

bool UCastleSupportTowerSubsystem::FindBalloonTarget(const FVector& Origin, uint64& OutId, FVector& OutLocation) const
{
	OutId = 0;
	double Nearest = TNumericLimits<double>::Max();
	for (const auto& Pair : Balloons)
	{
		const FBalloon& Flight = Pair.Value;
		if (Flight.bCargoReleased || !Flight.Source.IsValid() || !Flight.Visual.IsValid()) { continue; }
		const FVector Location = Flight.Visual->GetActorLocation();
		const double Distance = FVector::DistSquared(Origin, Location);
		const float Radius = Flight.Source->GetBuildingDefinition()->SupportData->AttractionRadius;
		// 气球在仇恨范围内优先于盾和主塔；多个气球按距离、再按生成 ID 定序。
		if (Distance > FMath::Square(static_cast<double>(Radius)) || Distance > Nearest
			|| (Distance == Nearest && OutId != 0 && Pair.Key > OutId)) { continue; }
		Nearest = Distance;
		OutId = Pair.Key;
		OutLocation = Location;
	}
	return OutId != 0;
}

bool UCastleSupportTowerSubsystem::GetBalloonTarget(const uint64 Id, FVector& OutLocation) const
{
	const FBalloon* Flight = Balloons.Find(Id);
	if (!Flight || Flight->bCargoReleased || !Flight->Source.IsValid() || !Flight->Visual.IsValid()) { return false; }
	OutLocation = Flight->Visual->GetActorLocation();
	return true;
}

bool UCastleSupportTowerSubsystem::DamageBalloon(const uint64 Id, const float Damage)
{
	FBalloon* Flight = Balloons.Find(Id);
	if (!IsInGameThread() || !Flight || Flight->bCargoReleased || !Flight->Visual.IsValid()
		|| !FMath::IsFinite(Damage) || Damage < 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("气球 %llu 受击失败，世界=%s；气球已失效或伤害数值非法。"), Id, *GetPathNameSafe(GetWorld()));
		return false;
	}
	Flight->Health -= Damage;
	return Flight->Health > 0.0f || PopBalloonInternal(Id, false);
}

bool UCastleSupportTowerSubsystem::PopBalloon(const uint64 Id)
{
	return PopBalloonInternal(Id, true);
}

bool UCastleSupportTowerSubsystem::PopBalloonInternal(const uint64 Id, const bool bPlayerPopped)
{
	FBalloon* Flight = Balloons.Find(Id);
	if (!IsInGameThread() || !Flight || Flight->bCargoReleased || !Flight->Visual.IsValid() || !Flight->Source.IsValid())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("气球 %llu 已失效或已破裂，无法再次投放（世界 %s）。"), Id, *GetPathNameSafe(GetWorld()));
		return false;
	}
	const auto* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	const double ExtraCells = bPlayerPopped && Badges ? Badges->GetStringGloveExtraCells() : 0.0;
	if (ExtraCells > 0.0)
	{
		const ACastleBoard* Board = Flight->Board.Get();
		if (!Board || !FMath::IsFinite(Board->CellSize) || Board->CellSize <= 0.0f)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 气球 %llu 无法应用摘绳手套：棋盘 %s 或格宽无效。"),
				*Flight->Source->GetPathName(), Id, *GetPathNameSafe(Board));
			return false;
		}
		Flight->ExtraImpactRadius = ExtraCells * Board->CellSize;
	}
	// 即便由 Mass 近战命中，同步撤销气球身份；落地伤害和人员生成只在世界 Tick 处理。
	Flight->bCargoReleased = true;
	Flight->Visual->ReleaseCargo();
	const FVector Start = Flight->Visual->GetActorLocation();
	const UCastleSupportTowerData* Data = Flight->Source->GetBuildingDefinition()->SupportData;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CastleSupplyLanding), false, Flight->Visual.Get());
	// 楼板阻挡 Pawn，塔壳和视觉外檐只响应 Visibility；货箱应落在真实地表而不是碰到美术屋顶。
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0, 0.0, Data->MaxDropDepth), ECC_Pawn, Params)
		|| Hit.bStartPenetrating || Hit.ImpactNormal.Z <= 0.0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 气球 %llu 破裂：%s 正下方没有有效落地面，不能结算空投。"),
			*Flight->Source->GetPathName(), Id, *Start.ToCompactString());
		// 拒绝整次空投，没有替代落点或越过非法落点继续结算。
		Flight->Visual->Destroy();
		Balloons.Remove(Id);
		return false;
	}
	Flight->Landing = Hit.ImpactPoint;
	UE_LOG(LogMoving_Castle, Display, TEXT("空投塔 %s 气球 %llu 已破裂，货箱下落。"), *Flight->Source->GetName(), Id);
	return true;
}

void UCastleSupportTowerSubsystem::TickBalloons(const float DeltaTime, FMassEntityManager& Manager, TArray<FCastleSupportUnit>& Enemies)
{
	TArray<uint64> Ids;
	Balloons.GenerateKeyArray(Ids);
	Ids.Sort();
	for (const uint64 Id : Ids)
	{
		FBalloon& Flight = Balloons.FindChecked(Id);
		ACastleSupplyBalloon* Visual = Flight.Visual.Get();
		if (!Visual || !Flight.Source.IsValid() || !Flight.Board.IsValid())
		{
			if (Visual) { Visual->Destroy(); }
			UE_LOG(LogMoving_Castle, Display, TEXT("气球 %llu 的塔、棋盘或表现离场，本次飞行结束。"), Id);
			Balloons.Remove(Id);
			continue;
		}
		const UCastleSupportTowerData* Data = Flight.Source->GetBuildingDefinition()->SupportData;
		if (Flight.bCargoReleased)
		{
			const FVector End = Flight.Landing + FVector(0.0, 0.0, 20.0);
			const FVector NewPosition = FMath::VInterpConstantTo(Visual->GetActorLocation(), End, DeltaTime, Data->CargoFallSpeed);
			Visual->SetActorLocation(NewPosition);
			if (NewPosition.Equals(End, 0.01))
			{
				if (!ApplyLanding(Flight, Manager, Enemies))
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 气球 %llu 的落地效果未全部成功。"), *Flight.Source->GetPathName(), Id);
				}
				Visual->Destroy();
				Balloons.Remove(Id);
			}
			continue;
		}
		// 帧预算可以经过多个航点；处于末端悬停不触发自动投放。
		double Travel = DeltaTime * Data->BalloonSpeed * GetFlightMultiplier();
		while (Flight.Path.IsValidIndex(Flight.NextPoint) && Travel > 0.0)
		{
			const FVector Location = Visual->GetActorLocation();
			const FVector ToPoint = Flight.Path[Flight.NextPoint] - Location;
			const double Span = ToPoint.Size();
			if (Span > Travel)
			{
				Visual->SetActorLocation(Location + ToPoint * (Travel / Span));
				break;
			}
			Visual->SetActorLocation(Flight.Path[Flight.NextPoint++]);
			Travel -= Span;
		}
	}
}

bool UCastleSupportTowerSubsystem::ApplyLanding(const FBalloon& Flight, FMassEntityManager& Manager, TArray<FCastleSupportUnit>& Enemies)
{
	ACastleBuildingBase& Source = *Flight.Source.Get();
	const auto& Data = *Source.GetBuildingDefinition()->SupportData;
	UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
	const double RadiusSquared = FMath::Square(static_cast<double>(Data.LandingRadius));
	const double ImpactRadius = Data.LandingRadius + Flight.ExtraImpactRadius;
	const double ImpactRadiusSquared = FMath::Square(ImpactRadius);
	const double Now = GetWorld()->GetTimeSeconds();
	const float Damage = Flight.Number * GetTowerDamageMultiplier(Source);
	const float SpeedAfterSlow = 1.0f - Flight.Number * 0.01f;
	int32 EnemiesHit = 0;
	for (FCastleSupportUnit& Enemy : Enemies)
	{
		if (Enemy.Health <= 0.0f || FVector::DistSquared(Flight.Landing, Enemy.Location) > ImpactRadiusSquared) { continue; }
		if (!Units->ApplyImmediateDamageToEnemy(Enemy.Handle, Damage)) { return false; }
		Enemy.Health = FMath::Max(0.0f, Enemy.Health - Damage);
		++EnemiesHit;
		if (Enemy.Health > 0.0f) { MovementModifiers.Add({Enemy.Handle.Entity, SpeedAfterSlow, Now + SlowSeconds}); }
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("空投塔 Lv%d %s 落地：命中 %d 个敌人，单体伤害 %.2f、减速 %d%%，数字和 %d。"),
		Flight.Level, *Source.GetName(), EnemiesHit, Damage, Flight.Number, Flight.Sum);
	DrawDebugCircle(GetWorld(), Flight.Landing + FVector(0.0, 0.0, 35.0), ImpactRadius, 40,
		FlightTint(Flight.Color).ToFColor(true), false, 2.0f, 0, 12.0f, FVector::ForwardVector, FVector::RightVector, false);
	if (Flight.Level == 1) { return true; }
	const float PercentMultiplier = 1.0f + Flight.Sum * 0.01f;
	if (Flight.Color == ECastleTileColor::Red || Flight.Color == ECastleTileColor::Yellow)
	{
		int32 Towers = 0;
		for (ACastleBuildingBase* Building : Flight.Board->GetPlacedBuildings())
		{
			if (!IsValid(Building))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 的棋盘包含失效建筑。"), *Source.GetPathName());
				return false;
			}
			const ECastleBuildingType Type = Building->GetBuildingType();
			const bool bDefense = Type != ECastleBuildingType::NormalRoom && Type != ECastleBuildingType::Farm;
			if (!Building->CanReceiveBuildingTrigger() || (Flight.Color == ECastleTileColor::Red ? !bDefense : Type != ECastleBuildingType::ArcherTower)) { continue; }
			const TArray<FIntVector> Cells = Building->GetOccupiedCoordinates();
			if (!Cells.ContainsByPredicate([&](const FIntVector& Cell)
			{
				return FVector::DistSquared(Flight.Board->GetGridWorldLocation(Cell), Flight.Landing) <= RadiusSquared;
			})) { continue; }
			(Flight.Color == ECastleTileColor::Red ? DamageModifiers : ArrowModifiers).Add({Building, PercentMultiplier, Now + BuffSeconds});
			++Towers;
		}
		if (Flight.Color == ECastleTileColor::Yellow) { FlightModifiers.Add({PercentMultiplier, Now + BuffSeconds}); }
		UE_LOG(LogMoving_Castle, Display, TEXT("空投塔 %s：%d 座塔获得 %d%% 的 10 秒增益。"), *Source.GetName(), Towers, Flight.Sum);
	}
	if (Flight.Level >= 3 && !FireAdjacentAttacks(Flight, Enemies)) { return false; }
	TArray<FCastleSupportUnit> NPCs;
	if ((Flight.Color == ECastleTileColor::Green || Flight.Level == 4) && !GatherUnits(Manager, ECastleMassUnitKind::NPC, NPCs)) { return false; }
	if (Flight.Color == ECastleTileColor::Green)
	{
		int32 People = 0;
		for (const FCastleSupportUnit& NPC : NPCs)
		{
			if (FVector::DistSquared(Flight.Landing, NPC.Location) <= RadiusSquared)
			{
				MovementModifiers.Add({NPC.Handle.Entity, PercentMultiplier, Now + BuffSeconds});
				++People;
			}
		}
		UE_LOG(LogMoving_Castle, Display, TEXT("空投塔 %s：%d 名人员获得 %d%% 的 10 秒移速增益。"), *Source.GetName(), People, Flight.Sum);
	}
	// 先观察原本在途的人员再招募，不能把刚落地的新兵算作应离开原路线的一员。
	if (Flight.Level == 4 && !RallyNPCs(Manager, Source, Flight.Landing, NPCs)) { return false; }
	return Flight.Color != ECastleTileColor::Blue || RecruitNPCs(Manager, Source, Flight.Landing, Flight.Sum % 10);
}

bool UCastleSupportTowerSubsystem::FireAdjacentAttacks(const FBalloon& Flight, const TArray<FCastleSupportUnit>& Enemies) const
{
	ACastleBuildingBase* Source = Flight.Source.Get();
	TArray<ACastleBuildingBase*> Neighbors;
	if (!Flight.Board->GetFaceAdjacentBuildings(Source, false, Neighbors)) { return false; }
	TMap<ACastleBuildingBase*, ACastleBuildingBase*> CannonByAmmo;
	for (ACastleBuildingBase* Building : Flight.Board->GetPlacedBuildings())
	{
		if (!IsValid(Building))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 联动查询遇到失效建筑。"), *Source->GetPathName());
			return false;
		}
		if (Building->GetBuildingType() != ECastleBuildingType::CannonTower || !Building->CanReceiveBuildingTrigger()) { continue; }
		const auto* Effect = Cast<UCastleCannonTowerAttackEffectComponent>(Building->BuildingEffect);
		TArray<ACastleBuildingBase*> Ammo;
		if (!Effect || !Effect->GetAmmunitionBuildings(Ammo))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 无法读取炮塔 %s 的弹药库。"), *Source->GetPathName(), *Building->GetPathName());
			return false;
		}
		for (ACastleBuildingBase* Storage : Ammo)
		{
			// 同一邻居被多座炮塔收编仍只提供一发；用最近炮塔的配置，同距按对象路径确定。
			ACastleBuildingBase*& Current = CannonByAmmo.FindOrAdd(Storage);
			const double Distance = FVector::DistSquared(Building->GetActorLocation(), Storage->GetActorLocation());
			const double Previous = Current ? FVector::DistSquared(Current->GetActorLocation(), Storage->GetActorLocation()) : TNumericLimits<double>::Max();
			if (Distance < Previous || (Distance == Previous && Current && Building->GetPathName() < Current->GetPathName())) { Current = Building; }
		}
	}
	UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
	const auto* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	const double Firepower = Badges ? Badges->GetFriendlyDamageMultiplier() : 1.0;
	for (ACastleBuildingBase* Neighbor : Neighbors)
	{
		const ECastleBuildingType Type = Neighbor->GetBuildingType();
		ACastleBuildingBase* Weapon = Type == ECastleBuildingType::ArcherTower || Type == ECastleBuildingType::CannonTower
			? Neighbor : CannonByAmmo.FindRef(Neighbor);
		if (!Weapon) { continue; }
		const auto* Data = Weapon->GetBuildingDefinition()->AttackData.Get();
		const FCastleTowerAttackLevelData* Level = Data ? Data->FindLevel(Weapon->GetBuildingLevel()) : nullptr;
		if (!Level || !Data->ProjectileConfiguration)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 联动武器 %s 的攻击资产或等级缺失，资产=%s。"),
				*Source->GetPathName(), *Weapon->GetPathName(), *GetPathNameSafe(Data));
			return false;
		}
		const FCastleSupportUnit* Target = nullptr;
		double Distance = FMath::Square(static_cast<double>(Level->AttackRadius));
		const FVector Muzzle = Flight.Landing + FVector(0.0, 0.0, 40.0);
		for (const FCastleSupportUnit& Enemy : Enemies)
		{
			const double Candidate = FVector::DistSquared(Muzzle, Enemy.Location);
			if (Enemy.Health > 0.0f && (Candidate < Distance || (!Target && Candidate == Distance))) { Distance = Candidate; Target = &Enemy; }
		}
		if (!Target)
		{
			UE_LOG(LogMoving_Castle, Display, TEXT("空投塔 %s 的邻居 %s 射程内无目标，本次联动未发射。"), *Source->GetName(), *Neighbor->GetName());
			continue;
		}
		FCastleMassHomingProjectileSpawn Shot;
		Shot.Target = Target->Handle;
		Shot.Origin = Muzzle;
		// 不走原建筑触发或弹药库联动，避免连锁及总伤害串入；伤害精确取本邻居卡面的一半。
		Shot.Damage = static_cast<float>((Neighbor->GetEffectiveBuildingNumber() / 2) * Firepower);
		TArray<FCastleMassUnitHandle> Projectiles;
		FText Reason;
		if (!Units->SpawnHomingProjectiles(Data->ProjectileConfiguration, {Shot}, Projectiles, Reason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("空投塔 %s 的邻居 %s 发射失败：%s"), *Source->GetPathName(), *Neighbor->GetPathName(), *Reason.ToString());
			return false;
		}
		UE_LOG(LogMoving_Castle, Display, TEXT("空投塔 %s 联动 %s：在落点发射一发 %.0f 点伤害的弹。"),
			*Source->GetName(), *Neighbor->GetName(), Shot.Damage);
	}
	return true;
}
