#include "CastleSupportTowerSubsystem.h"

#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleMassFragments.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleSupportFlightActors.h"
#include "CastleSupportTowerData.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Engine/World.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "MassCommonFragments.h"
#include "MassEntityQuery.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Moving_Castle.h"

bool UCastleSupportTowerSubsystem::QueueLaunch(ACastleBuildingBase& Source)
{
	const UCastleBuildingDefinition* Definition = Source.GetBuildingDefinition();
	FText Failure;
	if (!IsInGameThread() || Source.GetWorld() != GetWorld() || !Source.CanReceiveBuildingTrigger()
		|| !IsValid(Definition) || !Definition->ValidateConfiguration(Failure))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("支援塔 %s 请求失败：世界、建造状态或定义 %s 无效。%s"),
			*Source.GetPathName(), *GetPathNameSafe(Definition), *Failure.ToString());
		return false;
	}
	// 同一人员反复进入仍是独立请求；不能为同一座塔做全帧去重。
	PendingLaunches.Add(&Source);
	return true;
}

bool UCastleSupportTowerSubsystem::GatherUnits(FMassEntityManager& Manager, const ECastleMassUnitKind Kind,
	TArray<FCastleSupportUnit>& OutUnits) const
{
	OutUnits.Reset();
	FMassEntityQuery Query(Manager.AsShared());
	if (Kind == ECastleMassUnitKind::Enemy) { Query.AddTagRequirement<FCastleMassEnemyTag>(EMassFragmentPresence::All); }
	else { Query.AddTagRequirement<FCastleMassNPCTag>(EMassFragmentPresence::All); }
	Query.AddRequirement<FCastleMassUnitFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	FMassExecutionContext Context(Manager, 0.0f, false);
	bool bValid = true;
	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk)
	{
		const auto Vitals = Chunk.GetFragmentView<FCastleMassUnitFragment>();
		const auto Transforms = Chunk.GetFragmentView<FTransformFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			if (Vitals.IsEmpty() || Transforms.IsEmpty() || Vitals[It].Kind != Kind || !FMath::IsFinite(Vitals[It].Health)
				|| Transforms[It].GetTransform().GetLocation().ContainsNaN())
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("支援塔世界 %s：Mass 实体 %d 身份、生命或位置无效，取消本批查询。"),
					*GetPathNameSafe(GetWorld()), Chunk.GetEntity(It).Index);
				bValid = false;
				return;
			}
			// 已死亡但未被 Mass 清理的实体不再是可命中、减速或招募的对象。
			if (Vitals[It].Health <= 0.0f) { continue; }
			FCastleSupportUnit& Entry = OutUnits.AddDefaulted_GetRef();
			Entry.Handle.World = GetWorld();
			Entry.Handle.Entity = Chunk.GetEntity(It);
			Entry.Location = Transforms[It].GetTransform().GetLocation();
			Entry.Health = Vitals[It].Health;
		}
	});
	if (!bValid) { OutUnits.Reset(); }
	else { OutUnits.Sort([](const FCastleSupportUnit& A, const FCastleSupportUnit& B) { return A.Handle.Entity.Index < B.Handle.Entity.Index; }); }
	return bValid;
}

void UCastleSupportTowerSubsystem::Launch(ACastleBuildingBase& Source)
{
	ACastleBoard* Board = ACastleBoard::FindBoardForBuilding(&Source);
	const UCastleBuildingDefinition* Definition = Source.GetBuildingDefinition();
	const UCastleSupportTowerData* Data = IsValid(Definition) ? Definition->SupportData.Get() : nullptr;
	FText Reason;
	if (!Board || !IsValid(Data) || !Data->ValidateFor(Source.GetBuildingType(), Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("支援塔 %s 无法出动：棋盘/支援资产 %s 无效。%s"),
			*Source.GetPathName(), *GetPathNameSafe(Data), *Reason.ToString());
		return;
	}
	if (Source.GetBuildingType() == ECastleBuildingType::BalloonTower)
	{
		LaunchBalloon(Source, *Board, *Data);
		return;
	}
	FSwarm Batch;
	Batch.Source = Batch.Home = &Source;
	Batch.Level = Source.GetBuildingLevel();
	const int64 Count = Source.GetEffectiveBuildingNumber() * Source.GetOccupiedCoordinates().Num();
	if (Count > MAX_int32)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("蜂巢塔 %s 的结算蜂数 %lld 超出批次容量。"), *Source.GetPathName(), Count);
		return;
	}
	Batch.Count = static_cast<int32>(Count);
	Batch.Damage = Data->BeeDamage;
	Batch.Speed = Data->BeeFlightSpeed;
	// 巢口在塔上方，以已放置格的最高层定位，避免高层塔从底层模型中心出蜂。
	int32 Top = Source.GetOccupiedCoordinates()[0].Z;
	for (const FIntVector& Cell : Source.GetOccupiedCoordinates()) { Top = FMath::Max(Top, Cell.Z); }
	Batch.HomeLocation = Source.GetActorLocation() + Board->GetActorUpVector() * (Board->LayerHeight * (Top - Source.GetOccupiedCoordinates()[0].Z + 1) + 80.0);
	if (Batch.Level >= 3)
	{
		Batch.Schedule = MakeShared<FBeeSchedule>();
		if (!BuildHiveSchedule(*Board, Source, *Batch.Schedule)) { return; }
	}
	const FVector Start = Batch.HomeLocation;
	LaunchSwarm(MoveTemp(Batch), Start, *Data);
}

void UCastleSupportTowerSubsystem::Tick(const float DeltaTime)
{
	UMassEntitySubsystem* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!Mass || Mass->GetEntityManager().IsProcessing() || !FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("支援塔世界 %s：必须在有效的 Mass 阶段外使用有限帧时长推进。"), *GetPathNameSafe(GetWorld()));
		return;
	}
	FMassEntityManager& Manager = Mass->GetMutableEntityManager();
	// 临时路线上的进入也可触发支援塔；下一轮取队列，避免在遍历容器时立即重入生成。
	const auto Launches = MoveTemp(PendingLaunches);
	for (const auto& Request : Launches)
	{
		if (ACastleBuildingBase* Source = Request.Get(); Source && Source->CanReceiveBuildingTrigger()) { Launch(*Source); }
		else { UE_LOG(LogMoving_Castle, Display, TEXT("支援塔 %s 已撤回，本次未发射。"), *GetPathNameSafe(Request.GetEvenIfUnreachable())); }
	}
	if (!Swarms.IsEmpty() || !Balloons.IsEmpty())
	{
		TArray<FCastleSupportUnit> Enemies;
		if (GatherUnits(Manager, ECastleMassUnitKind::Enemy, Enemies))
		{
			TickSwarms(DeltaTime, Enemies);
			TickBalloons(DeltaTime, Manager, Enemies);
		}
	}
	RefreshModifiers(Manager);
	TickJourneys(Manager, DeltaTime);
}

void UCastleSupportTowerSubsystem::RefreshModifiers(FMassEntityManager& Manager)
{
	const double Now = GetWorld()->GetTimeSeconds();
	DamageModifiers.RemoveAllSwap([Now](const FTowerModifier& Entry) { return Entry.Expires <= Now || !Entry.Tower.IsValid(); });
	ArrowModifiers.RemoveAllSwap([Now](const FTowerModifier& Entry) { return Entry.Expires <= Now || !Entry.Tower.IsValid(); });
	FlightModifiers.RemoveAllSwap([Now](const FTimeModifier& Entry) { return Entry.Expires <= Now; });
	MovementModifiers.RemoveAllSwap([&](const FUnitModifier& Entry) { return Entry.Expires <= Now || !Manager.IsEntityActive(Entry.Entity); });
	TMap<FMassEntityHandle, float> Effective;
	for (const FUnitModifier& Entry : MovementModifiers)
	{
		float& Value = Effective.FindOrAdd(Entry.Entity, 1.0f);
		// 敌人只有减速，NPC 只有加速；取仍生效的最强效果，不夹紧叠加结果。
		Value = Entry.Multiplier < 1.0f ? FMath::Min(Value, Entry.Multiplier) : FMath::Max(Value, Entry.Multiplier);
	}
	for (const FMassEntityHandle Entity : ModifiedUnits)
	{
		if (!Effective.Contains(Entity) && Manager.IsEntityActive(Entity))
		{
			Manager.GetFragmentDataChecked<FCastleMassUnitFragment>(Entity).SupportMovementMultiplier = 1.0f;
		}
	}
	ModifiedUnits.Reset();
	for (const auto& Entry : Effective)
	{
		Manager.GetFragmentDataChecked<FCastleMassUnitFragment>(Entry.Key).SupportMovementMultiplier = Entry.Value;
		ModifiedUnits.Add(Entry.Key);
	}
}

float UCastleSupportTowerSubsystem::GetTowerDamageMultiplier(const ACastleBuildingBase& Tower) const
{
	float Result = 1.0f;
	const double Now = GetWorld()->GetTimeSeconds();
	for (const FTowerModifier& Entry : DamageModifiers)
	{
		if (Entry.Tower.Get() == &Tower && Entry.Expires > Now) { Result = FMath::Max(Result, Entry.Multiplier); }
	}
	return Result;
}

float UCastleSupportTowerSubsystem::GetArcherRateMultiplier(const ACastleBuildingBase& Tower) const
{
	float Result = 1.0f;
	const double Now = GetWorld()->GetTimeSeconds();
	for (const FTowerModifier& Entry : ArrowModifiers)
	{
		if (Entry.Tower.Get() == &Tower && Entry.Expires > Now) { Result = FMath::Max(Result, Entry.Multiplier); }
	}
	return Result;
}

float UCastleSupportTowerSubsystem::GetTowerFireRateMultiplier(const ACastleBuildingBase& Tower) const
{
	const bool bArcher = Tower.GetBuildingType() == ECastleBuildingType::ArcherTower;
	float Rate = bArcher ? GetArcherRateMultiplier(Tower) : 1.0f;
	if (const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld()))
	{
		// 箭塔专属倍率与全攻击塔倍率相乘，炮塔只受全局效果影响。
		const auto* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
		Rate *= Badges->GetGlobalTowerFireRateMultiplier(Director && Director->IsFirstTowerDamageTriggeredInBattle());
		if (bArcher) { Rate *= Badges->GetArcherFireRateMultiplier(); }
	}
	return Rate;
}

float UCastleSupportTowerSubsystem::GetFlightMultiplier() const
{
	float Result = 1.0f;
	const double Now = GetWorld()->GetTimeSeconds();
	for (const FTimeModifier& Entry : FlightModifiers)
	{
		if (Entry.Expires > Now) { Result = FMath::Max(Result, Entry.Multiplier); }
	}
	// 随帧查询首次受击效果，飞行中的蜜蜂与气球无需重新生成。
	if (const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld()))
	{
		const auto* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
		Result *= Badges->GetSummonSpeedMultiplier(Director && Director->IsFirstTowerDamageTriggeredInBattle());
	}
	return Result;
}

void UCastleSupportTowerSubsystem::ClearFlightActors()
{
	for (const FSwarm& Swarm : Swarms) { if (auto* Visual = Swarm.Visual.Get()) { Visual->Destroy(); } }
	for (const auto& Pair : Balloons) { if (auto* Visual = Pair.Value.Visual.Get()) { Visual->Destroy(); } }
	Swarms.Reset();
	Balloons.Reset();
	PendingLaunches.Reset();
}

bool UCastleSupportTowerSubsystem::EndBattleSupport()
{
	UMassEntitySubsystem* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
	if (!Mass || !Units || Mass->GetEntityManager().IsProcessing())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("支援塔世界 %s：本场支援回收必须在 Mass 阶段外执行。"), *GetPathNameSafe(GetWorld()));
		return false;
	}
	FMassEntityManager& Manager = Mass->GetMutableEntityManager();
	RecruitedNPCs.RemoveAll([&](const auto& Item) { return !Manager.IsEntityActive(Item.Entity); });
	if (!RecruitedNPCs.IsEmpty() && !Units->DespawnUnits(RecruitedNPCs)) { return false; }
	RecruitedNPCs.Reset();
	for (const auto& Pair : Journeys)
	{
		if (Manager.IsEntityActive(Pair.Key))
		{
			auto& Patrol = Manager.GetFragmentDataChecked<FCastleMassPatrolFragment>(Pair.Key);
			Patrol.bSupportJourney = false;
			// 导演的正式人员马上回收；未被本场拥有的调试单位下一轮重新绑定，不能沿旧进度瞬移。
			Patrol.PathRevision = INDEX_NONE;
		}
	}
	Journeys.Reset();
	ClearFlightActors();
	MovementModifiers.Reset();
	DamageModifiers.Reset();
	ArrowModifiers.Reset();
	FlightModifiers.Reset();
	RefreshModifiers(Manager);
	return true;
}

TStatId UCastleSupportTowerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCastleSupportTowerSubsystem, STATGROUP_Tickables);
}

bool UCastleSupportTowerSubsystem::IsTickable() const
{
	return Super::IsTickable() && (!PendingLaunches.IsEmpty() || !Swarms.IsEmpty() || !Balloons.IsEmpty()
		|| !MovementModifiers.IsEmpty() || !Journeys.IsEmpty() || !DamageModifiers.IsEmpty()
		|| !ArrowModifiers.IsEmpty() || !FlightModifiers.IsEmpty());
}

bool UCastleSupportTowerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCastleSupportTowerSubsystem::Deinitialize()
{
	// World teardown 自身负责销毁 Mass；这里只释放本系统拥有的表现对象和临时值状态。
	ClearFlightActors();
	Journeys.Reset();
	RecruitedNPCs.Reset();
	Super::Deinitialize();
}
