#include "CastleMassSpawner.h"

#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "MassEntityConfigAsset.h"
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
	TArray<FTransform> Transforms;
	Transforms.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FTransform& Transform = Transforms.Add_GetRef(GetActorTransform());
		Transform.SetLocation(GetActorTransform().TransformPosition(FVector(Index % Columns * Spacing, Index / Columns * Spacing, 0.0)));
	}
	FText Failure;
	if (!Units->SpawnUnits(Configuration, Transforms, SpawnedUnits, Failure))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("Mass 批量生成失败：%s"), *Failure.ToString());
		return false;
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
	if (UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>())
	{
		Units->DespawnUnits(SpawnedUnits);
	}
	SpawnedUnits.Reset();
}

void ACastleMassSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 整个世界退出时 Mass 子系统统一释放；流送卸载或主动移除生成器时清理所属实体。
	if (EndPlayReason == EEndPlayReason::Destroyed || EndPlayReason == EEndPlayReason::RemovedFromWorld)
	{
		DespawnUnits();
	}
	Super::EndPlay(EndPlayReason);
}
