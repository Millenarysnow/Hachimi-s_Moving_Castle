// Copyright Epic Games, Inc. All Rights Reserved.
#include "CastleDirectorNodeActor.h"

#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "CollisionShape.h"
#include "Components/SphereComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Moving_Castle.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

ACastleDirectorNodeActor::ACastleDirectorNodeActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 与飞行 Pawn 的双向查询使用明确的 WorldDynamic / Pawn 对象通道。
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ACastleDirectorNodeActor::HandleBeginOverlap);
	Trigger->OnComponentEndOverlap.AddDynamic(this, &ACastleDirectorNodeActor::HandleEndOverlap);
	Portal = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Portal"));
	Portal->SetupAttachment(Trigger);
	Portal->SetAutoActivate(false);
	Portal->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Portal->SetCanEverAffectNavigation(false);
	// 原资产的圆环在本地 XY 面。转起后令圆环正面朝向节点的 X 轴，即主塔方向。
	Portal->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
	Label = CreateDefaultSubobject<UWidgetComponent>(TEXT("Label"));
	Label->SetupAttachment(Trigger);
	Label->SetWidgetSpace(EWidgetSpace::Screen);
	Label->SetDrawSize(FVector2D(480.0f, 72.0f));
	Label->SetPivot(FVector2D(0.5f, 0.5f));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool ACastleDirectorNodeActor::Configure(const UCastleDirectorConfigDataAsset& Config, const int32 Step,
	const ECastleDirectorNodeKind Kind)
{
	if (!IsValid(Config.NodePortalSystem) || !FMath::IsFinite(Config.NodePortalScale) || Config.NodePortalScale <= 0.0f
		|| !Config.NodeLabelWidgetClass)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("节点配置失败：资产 %s 的 NodePortalSystem=%s、NodePortalScale=%g 或 NodeLabelWidgetClass=%s 无效。"),
			*Config.GetPathName(), *GetPathNameSafe(Config.NodePortalSystem), Config.NodePortalScale,
			*GetPathNameSafe(Config.NodeLabelWidgetClass.Get()));
		return false;
	}
	APlayerController* LocalController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	ULocalPlayer* LocalPlayer = LocalController ? LocalController->GetLocalPlayer() : nullptr;
	if (!IsValid(LocalPlayer))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("节点配置失败：世界 %s 没有可用于屏幕空间标签 %s 的本地玩家。"),
			*GetPathNameSafe(GetWorld()), *GetPathNameSafe(Config.NodeLabelWidgetClass.Get()));
		return false;
	}
	NodeKind = Kind;
	StepNumber = Step;
	LabelWidgetClass = Config.NodeLabelWidgetClass;
	Trigger->SetSphereRadius(Config.NodeTriggerRadius);
	Portal->SetAsset(Config.NodePortalSystem);
	Portal->SetVariableFloat(TEXT("User.Size"), Config.NodePortalScale);
	// 普通战斗保留 Light 门户原始金色，其他三种节点用独立色彩，文字仍给出准确类型。
	switch (Kind)
	{
	case ECastleDirectorNodeKind::Normal: break;
	case ECastleDirectorNodeKind::Elite: Portal->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(1.0f, 0.06f, 0.10f)); break;
	case ECastleDirectorNodeKind::RewardIsland: Portal->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(0.06f, 0.50f, 1.0f)); break;
	case ECastleDirectorNodeKind::RestIsland: Portal->SetVariableLinearColor(TEXT("User.Color"), FLinearColor(0.10f, 1.0f, 0.25f)); break;
	}
	Label->SetRelativeLocation(FVector(0.0f, 0.0f, Config.NodeTriggerRadius + 160.0f));
	Label->SetWidgetClass(LabelWidgetClass);
	Label->SetOwnerPlayer(LocalPlayer);
	SetActorHiddenInGame(true);
	return true;
}

void ACastleDirectorNodeActor::BeginPlay()
{
	Super::BeginPlay();
	InitializeLabelWidget();
}

bool ACastleDirectorNodeActor::InitializeLabelWidget()
{
	UCastleDirectorNodeLabelWidget* LabelWidget = Cast<UCastleDirectorNodeLabelWidget>(Label->GetUserWidgetObject());
	if (!IsValid(LabelWidget) || !LabelWidget->HasRequiredBindings())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("节点 %s 的屏幕空间标签实例或 LabelText 绑定无效，WBP 类 %s。"),
			*GetPathName(), *GetPathNameSafe(LabelWidgetClass.Get()));
		bLabelReady = false;
		return false;
	}
	// 同一步内不同节点均按自身类型上色，文字也提供独立于颜色的识别信息。
	const TCHAR* Caption = TEXT("战斗");
	FLinearColor Color = FLinearColor::White;
	switch (NodeKind)
	{
	case ECastleDirectorNodeKind::Normal: break;
	case ECastleDirectorNodeKind::Elite: Caption = TEXT("精英"); Color = FLinearColor::Red; break;
	case ECastleDirectorNodeKind::RewardIsland: Caption = TEXT("空岛"); Color = FLinearColor(1.0f, 0.72f, 0.12f); break;
	case ECastleDirectorNodeKind::RestIsland: Caption = TEXT("休息"); Color = FLinearColor::Green; break;
	}
	const FText Text = FText::FromString(FString::Printf(TEXT("%s · 第 %d 关"), Caption, StepNumber));
	bLabelReady = LabelWidget->SetNodeLabel(Text, Color);
	return bLabelReady;
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
	Portal->Activate();
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
