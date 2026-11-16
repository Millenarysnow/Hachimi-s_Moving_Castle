// Copyright Epic Games, Inc. All Rights Reserved.
#include "CastleDirectorNodeActor.h"

#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CollisionShape.h"
#include "Components/SphereComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

ACastleDirectorNodeActor::ACastleDirectorNodeActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ACastleDirectorNodeActor::HandleBeginOverlap);
	Trigger->OnComponentEndOverlap.AddDynamic(this, &ACastleDirectorNodeActor::HandleEndOverlap);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Trigger);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(100.0f);
}

void ACastleDirectorNodeActor::Configure(const UCastleDirectorConfigDataAsset& Config, const int32 Step,
	const ECastleDirectorNodeKind Kind)
{
	NodeKind = Kind;
	Trigger->SetSphereRadius(Config.NodeTriggerRadius);
	Mesh->SetStaticMesh(Config.NodeMesh);
	Mesh->SetRelativeScale3D(FVector(Config.NodeMeshScale));
	Label->SetRelativeLocation(FVector(0.0f, 0.0f, Config.NodeTriggerRadius + 160.0f));
	Label->SetText(FText::FromString(FString::Printf(TEXT("%s · 第 %d 关"),
		Kind == ECastleDirectorNodeKind::Elite ? TEXT("精英战斗") : TEXT("普通战斗"), Step)));
	Label->SetTextRenderColor(Kind == ECastleDirectorNodeKind::Elite ? FColor::Red : FColor::White);
	SetActorHiddenInGame(true);
}

void ACastleDirectorNodeActor::ActivateNode()
{
	// 在打开碰撞之前记录初始 Pawn，避免碰撞启用产生的首个重叠被当作玩家选择。
	APawn* Pawn = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
	UPrimitiveComponent* PawnCollision = Pawn ? Cast<UPrimitiveComponent>(Pawn->GetRootComponent()) : nullptr;
	if (PawnCollision && PawnCollision->OverlapComponent(GetActorLocation(), FQuat::Identity, FCollisionShape::MakeSphere(Trigger->GetScaledSphereRadius())))
	{
		BlockedInitialPawn = Pawn;
	}
	bActive = true;
	SetActorHiddenInGame(false);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

void ACastleDirectorNodeActor::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!bActive || !Pawn || !Pawn->IsLocallyControlled() || !Pawn->IsPlayerControlled() || BlockedInitialPawn == Pawn) { return; }
	if (UCastleDirectorSubsystem* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>())
	{
		Director->HandleNodeTouched(this);
	}
}

void ACastleDirectorNodeActor::HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor == BlockedInitialPawn.Get() && !Trigger->IsOverlappingActor(OtherActor)) { BlockedInitialPawn.Reset(); }
}
