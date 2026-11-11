#include "CastleMassSpawner.h"

#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleNavigationSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Moving_Castle.h"

ACastleMassSpawner::ACastleMassSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ACastleMassSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (bSpawnOnBeginPlay) { SpawnUnits(); }
}

bool ACastleMassSpawner::SpawnUnits()
{
	UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
	if (!Units || !SpawnedUnits.IsEmpty() || Count <= 0 || Columns <= 0 || !FMath::IsFinite(Spacing) || Spacing <= 0.0f) { return false; }
	const auto* UnitTrait = Configuration ? Cast<UCastleMassUnitTrait>(Configuration->FindTrait(UCastleMassUnitTrait::StaticClass())) : nullptr;
	if (bFollowSharedPatrolRoute && (bStartMoving || !UnitTrait || !UnitTrait->IsA<UCastleMassNPCTrait>()))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 共享巡逻生成器需要 NPC 配置，且不能同时勾选 Start Moving。"));
		return false;
	}
	UCastleNavigationSubsystem* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	const auto Path = Navigation->AcquireSharedPath();
	if (bFollowSharedPatrolRoute && (!Path || !Path->bValid || !Path->bClosedLoop))
	{
		if (!bWaitingForSharedRoute)
		{
			bWaitingForSharedRoute = true;
			Navigation->OnSharedPathChanged.AddDynamic(this, &ACastleMassSpawner::HandleSharedRouteReady);
			UE_LOG(LogMoving_Castle, Display, TEXT("Mass 巡逻生成器 %s：等待整条共享闭环就绪后生成 %d 个 NPC。"), *GetName(), Count);
		}
		return true;
	}
	Navigation->OnSharedPathChanged.RemoveDynamic(this, &ACastleMassSpawner::HandleSharedRouteReady);
	bWaitingForSharedRoute = false;
	TArray<FTransform> Transforms;
	Transforms.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FTransform& Transform = Transforms.Add_GetRef(GetActorTransform());
		Transform.SetLocation(GetActorTransform().TransformPosition(FVector(Index % Columns * Spacing, Index / Columns * Spacing, 0.0)));
		if (bFollowSharedPatrolRoute)
		{
			FVector Location;
			FVector Direction;
			Path->SamplePatrol(Path->Length * Index / Count, Location, Direction);
			Transform.SetLocation(Location);
			Direction.Z = 0.0;
			if (!Direction.IsNearlyZero()) { Transform.SetRotation(Direction.Rotation().Quaternion()); }
		}
	}
	FText Failure;
	if (!Units->SpawnUnits(Configuration, Transforms, SpawnedUnits, Failure))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 批量生成失败：%s"), *Failure.ToString());
		return false;
	}
	if (bFollowSharedPatrolRoute)
	{
		// SpawnUnits 已确认处于 Mass 安全边界。直接写入生成时已知的精确进度，不能再以最近点
		// 反推路段：楼梯去返路线可能重叠，最近点会把返程 NPC 错当成去程并破坏均匀分布。
		auto& Manager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
		for (int32 Index = 0; Index < SpawnedUnits.Num(); ++Index)
		{
			auto& Patrol = Manager.GetFragmentDataChecked<FCastleMassPatrolFragment>(SpawnedUnits[Index].Entity);
			Patrol.bEnabled = true;
			Patrol.PathRevision = Path->PathRevision;
			Patrol.RouteRevision = Path->RouteRevision;
			Patrol.Distance = Path->Length * Index / Count;
			Patrol.Leg = Path->FindPatrolLeg(Patrol.Distance);
		}
	}
	if (bStartMoving)
	{
		const auto* Trait = CastChecked<UCastleMassUnitTrait>(Configuration->FindTrait(UCastleMassUnitTrait::StaticClass()));
		Units->SetUnitsVelocity(SpawnedUnits, GetActorRotation().RotateVector(LocalMovementDirection.GetSafeNormal()) * Trait->Movement.MaxSpeed);
	}
	return true;
}

void ACastleMassSpawner::DespawnUnits()
{
	GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->OnSharedPathChanged.RemoveDynamic(this, &ACastleMassSpawner::HandleSharedRouteReady);
	bWaitingForSharedRoute = false;
	if (UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>())
	{
		Units->DespawnUnits(SpawnedUnits);
	}
	SpawnedUnits.Reset();
}

void ACastleMassSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->OnSharedPathChanged.RemoveDynamic(this, &ACastleMassSpawner::HandleSharedRouteReady);
	// 整个世界退出时 Mass 子系统统一释放；流送卸载或主动移除生成器时清理所属实体。
	if (EndPlayReason == EEndPlayReason::Destroyed || EndPlayReason == EEndPlayReason::RemovedFromWorld)
	{
		DespawnUnits();
	}
	Super::EndPlay(EndPlayReason);
}

void ACastleMassSpawner::HandleSharedRouteReady()
{
	if (bWaitingForSharedRoute) { SpawnUnits(); }
}
