#include "Interaction/CastlePatrolRoute.h"

#include "CastleNavigationSubsystem.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Moving_Castle.h"
#include "Castle/Actors/CastleBoard.h"
#include "Castle/Actors/CastleBuildingBase.h"

ACastlePatrolRoute::ACastlePatrolRoute()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	// 编辑时先消费左键，避免同一次输入继续放置建筑；退出编辑后左键交还原控制器。
	InputPriority = 10;
}

void ACastlePatrolRoute::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<ACastlePatrolRoute> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->bOwnsRoute)
		{
			ShowMessage(TEXT("同一世界只能有一个 CastlePatrolRoute，重复入口已停用，请删除多余 Actor。"), false);
			SetActorTickEnabled(false);
			return;
		}
	}
	bOwnsRoute = true;
	if (!LocalWaypoints.IsEmpty())
	{
		TArray<FVector> WorldPoints;
		for (const FVector Point : LocalWaypoints) { WorldPoints.Add(GetActorTransform().TransformPosition(Point)); }
		FText Failure;
		if (!SetWorldWaypoints(WorldPoints, Failure)) { ShowMessage(Failure.ToString(), false); }
	}
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (bAllowPlayerEditing && Controller && Controller->IsLocalController())
	{
		EnableInput(Controller);
		InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ACastlePatrolRoute::ToggleEditing);
		InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ACastlePatrolRoute::AddPointAtViewCenter).bConsumeInput = false;
		InputComponent->BindKey(EKeys::BackSpace, IE_Pressed, this, &ACastlePatrolRoute::UndoPoint).bConsumeInput = false;
		InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ACastlePatrolRoute::ApplyDraft).bConsumeInput = false;
		InputComponent->BindKey(EKeys::Delete, IE_Pressed, this, &ACastlePatrolRoute::ClearRoute).bConsumeInput = false;
	}
}

bool ACastlePatrolRoute::SetWorldWaypoints(const TArray<FVector>& Waypoints, FText& OutFailureReason)
{
	if (!bOwnsRoute) { OutFailureReason = FText::FromString(TEXT("此 Actor 没有全局巡逻路线的编辑权。")); return false; }
	UCastleNavigationSubsystem* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	if (!Navigation->RequestSharedPatrolRoute(Waypoints, OutFailureReason)) { return false; }
	LocalWaypoints.Reset(Waypoints.Num());
	for (FVector Point : Waypoints) { LocalWaypoints.Add(GetActorTransform().InverseTransformPosition(Point)); }
	return true;
}

bool ACastlePatrolRoute::CanEdit() const
{
	const auto* Controller = Cast<ACastlePlayerController>(GetWorld()->GetFirstPlayerController());
	FText Reason;
	return bOwnsRoute && bAllowPlayerEditing && (!Controller || (!Controller->GetShop()->IsShopOpen() && Controller->IsDirectorEditingAllowed(Reason)));
}

void ACastlePatrolRoute::CancelEditingDraft()
{
	SetEditing(false);
	DraftWaypoints.Reset();
}

void ACastlePatrolRoute::SetEditing(bool bNewEditing)
{
	bEditing = bNewEditing;
	if (!InputComponent) { return; }
	for (FInputKeyBinding& Binding : InputComponent->KeyBindings)
	{
		if (Binding.Chord.Key != EKeys::P) { Binding.bConsumeInput = bEditing; }
	}
}

void ACastlePatrolRoute::ToggleEditing()
{
	const auto* Controller = Cast<ACastlePlayerController>(GetWorld()->GetFirstPlayerController());
	FText Reason;
	if (Controller && !Controller->IsDirectorEditingAllowed(Reason)) { ShowMessage(Reason.ToString(), false); return; }
	if (!CanEdit()) { return; }
	if (bEditing)
	{
		SetEditing(false);
		ShowMessage(TEXT("已取消路径草稿，继续使用已发布路线。"), true);
		return;
	}
	DraftWaypoints = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->GetRequestedWaypoints();
	SetEditing(true);
	ShowMessage(TEXT("巡逻编辑：瞄准建筑楼顶或地面后左键添加路径点；建筑取本层楼内地板；Backspace 撤回，Enter 提交完整闭环，Delete 清空，P 取消。"), true);
}

void ACastlePatrolRoute::AddPointAtViewCenter()
{
	if (!bEditing || !CanEdit()) { return; }
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	FVector Origin, Direction;
	if (Width <= 0 || Height <= 0 || !Controller->DeprojectScreenPositionToWorld(Width * 0.5, Height * 0.5, Origin, Direction))
	{
		ShowMessage(TEXT("当前视口无法生成中心射线。"), false);
		return;
	}
	FVector Point;
	FText Failure;
	if (!TryResolveWaypointFromRay(GetWorld(), Origin, Origin + Direction * 100000.0, Point, Failure))
	{
		ShowMessage(Failure.ToString(), false);
		return;
	}
	if (!DraftWaypoints.IsEmpty() && DraftWaypoints.Last().Equals(Point, 1.0))
	{
		ShowMessage(TEXT("新路径点与上一点重合，请选择不同位置。"), false);
		return;
	}
	DraftWaypoints.Add(Point);
	ShowMessage(FString::Printf(TEXT("已放置草稿路径点 %d，Enter 后统一求解全部路段。"), DraftWaypoints.Num()), true);
}

bool ACastlePatrolRoute::TryResolveWaypointFromRay(UWorld* World, const FVector& RayStart, const FVector& RayEnd,
	FVector& OutPoint, FText& OutFailureReason)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CastlePatrolPlacement), true);
	if (!World)
	{
		OutFailureReason = FText::FromString(TEXT("巡逻放点失败：World 无效。"));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
		return false;
	}
	if (APlayerController* Controller = World->GetFirstPlayerController())
	{
		if (Controller->GetPawn()) { Params.AddIgnoredActor(Controller->GetPawn()); }
	}
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, RayStart, RayEnd, ECC_Visibility, Params))
	{
		OutFailureReason = FText::FromString(TEXT("请瞄准建筑或可行走地面顶面后放置路径点。"));
		return false;
	}
	if (ACastleBuildingBase* Building = Cast<ACastleBuildingBase>(Hit.GetActor()))
	{
		// 保留棋盘已有的“命中建筑最近占用格”语义，多层建筑的楼顶命中落到对应上层。
		// 格心减半层高就是建筑底面锚点；几厘米楼板厚度交给原有 NavMesh 投影，不查碰撞补高度。
		for (TActorIterator<ACastleBoard> It(World); It; ++It)
		{
			FCastleGridCursorTarget Target;
			if (It->GetPlacedBuildings().Contains(Building) && It->TryResolveGridCursorTarget(RayStart, RayEnd, Target) && Target.Surface == ECastleGridCursorSurface::Building)
			{
				OutPoint = It->GetGridWorldLocation(Target.Cell) - It->GetActorTransform().TransformVector(FVector(0, 0, It->LayerHeight * 0.5));
				return true;
			}
		}
		OutFailureReason = FText::FromString(FString::Printf(TEXT("建筑 %s 无法通过棋盘解析命中占用格，拒绝巡逻放点。"), *Building->GetPathName()));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
		return false;
	}
	// 地块和其他场景碰撞仍使用原有顶面规则，不把没有建筑的命中改造成虚拟楼板。
	if (Hit.ImpactNormal.Z < 0.5)
	{
		OutFailureReason = FText::FromString(TEXT("请瞄准可行走楼板或地面顶面后放置路径点。"));
		return false;
	}
	OutPoint = Hit.ImpactPoint;
	return true;
}

void ACastlePatrolRoute::UndoPoint()
{
	if (!bEditing || !CanEdit()) { return; }
	if (!DraftWaypoints.IsEmpty()) { DraftWaypoints.Pop(); }
	ShowMessage(FString::Printf(TEXT("草稿剩余 %d 个路径点。"), DraftWaypoints.Num()), true);
}

void ACastlePatrolRoute::ApplyDraft()
{
	if (!bEditing || !CanEdit()) { return; }
	FText Failure;
	if (!SetWorldWaypoints(DraftWaypoints, Failure)) { ShowMessage(Failure.ToString(), false); return; }
	SetEditing(false);
	const UCastleNavigationSubsystem* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	ShowMessage(Navigation->LastFailure.IsEmpty()
		? FString::Printf(TEXT("已提交 %d 个路径点的完整共享巡逻路线。"), DraftWaypoints.Num())
		: Navigation->LastFailure.ToString(), Navigation->LastFailure.IsEmpty());
}

void ACastlePatrolRoute::ClearRoute()
{
	if (!bEditing || !CanEdit()) { return; }
	GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->ClearSharedRoute();
	LocalWaypoints.Reset();
	DraftWaypoints.Reset();
	ShowMessage(TEXT("共享巡逻已清空，NPC 停止。可以重新放点并按 Enter 提交。"), true);
}

void ACastlePatrolRoute::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 打开商店时退出草稿模式并释放左键，保证购买/放置输入不会被巡逻编辑截获。
	if (bEditing && !CanEdit()) { CancelEditingDraft(); }
	if (!bShowRoute || !bOwnsRoute) { return; }
	const auto* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	const auto Path = Navigation->AcquireSharedPath();
	if (Path && Path->bValid && Path->bClosedLoop)
	{
		for (int32 Index = 1; Index < Path->PatrolPoints.Num(); ++Index)
		{
			DrawDebugLine(GetWorld(), Path->PatrolPoints[Index - 1] + FVector(0, 0, 8), Path->PatrolPoints[Index] + FVector(0, 0, 8), FColor::Green, false, -1.0f, 0, 3.0f);
		}
	}
	const TArray<FVector>& DisplayPoints = bEditing ? DraftWaypoints : Navigation->GetRequestedWaypoints();
	for (int32 Index = 0; Index < DisplayPoints.Num(); ++Index)
	{
		const FVector Position = DisplayPoints[Index] + FVector(0, 0, 20);
		DrawDebugSphere(GetWorld(), Position, 14.0f, 12, bEditing ? FColor::Orange : FColor::Cyan);
		DrawDebugString(GetWorld(), Position + FVector(0, 0, 25), FString::FromInt(Index + 1), nullptr, FColor::White, 0.0f, false, 1.3f);
	}
	if (GEngine)
	{
		const FString State = !Navigation->LastFailure.IsEmpty() ? Navigation->LastFailure.ToString()
			: (Path && Path->bValid && Path->bClosedLoop ? TEXT("Ready") : TEXT("Waiting for route / NavMesh"));
		GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this), 0.1f, Navigation->LastFailure.IsEmpty() ? FColor::Cyan : FColor::Red,
			FString::Printf(TEXT("Shared NPC patrol | %s | Waypoints %d | Path queries %d | Refresh %d\n[P] Edit / cancel | LMB: point at view center | Backspace: undo | Enter: apply all | Delete: clear"),
				bEditing ? TEXT("EDITING DRAFT") : *State, DisplayPoints.Num(), Navigation->PathQueryCount, Navigation->RefreshCount));
	}
}

void ACastlePatrolRoute::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bOwnsRoute && (EndPlayReason == EEndPlayReason::Destroyed || EndPlayReason == EEndPlayReason::RemovedFromWorld))
	{
		GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->ClearSharedRoute();
	}
	Super::EndPlay(EndPlayReason);
}

void ACastlePatrolRoute::ShowMessage(const FString& Message, bool bSuccess) const
{
	UE_LOG(LogMoving_Castle, Log, TEXT("NPC 路径编辑：%s"), *Message);
	if (GEngine) { GEngine->AddOnScreenDebugMessage(2026111102, 8.0f, bSuccess ? FColor::Cyan : FColor::Yellow, Message); }
}
