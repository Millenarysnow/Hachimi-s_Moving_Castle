#include "Interaction/CastlePatrolRoute.h"

#include "CastleNavigationSubsystem.h"
#include "CastleTowerDispatchSubsystem.h"
#include "Interaction/CastlePatrolWaypointMarker.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"
#include "Castle/Actors/CastleBoard.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"

namespace
{
	// 硬引用正式蓝图，打包时随巡逻入口和其资产依赖一起 Cook；不依赖手工关卡配置。
	constexpr const TCHAR* WaypointMarkerAsset = TEXT("/Game/MovingCastle/BP/BP_CastlePatrolWaypointMarker");
	// 两个通道独立查询同一顶面，允许世界空间 1 mm 的碰撞精度差；不能用导航投影范围穿透前景。
	constexpr double CoincidentHitTolerance = 0.1;
	// 功能面与所在格的作者站立面齐平；1 cm 容差不允许把家具或斜坡顶面当作楼板。
	constexpr double FloorPlaneTolerance = 1.0;

	FVector2D GetHorizontalGridCell(const ACastleBoard& Board, const FVector& LocalHit)
	{
		// 每格覆盖 [中心 - 半格, 中心 + 半格)，与巡逻平段切格一致。调用方先验证范围，再转整数。
		return FVector2D(FMath::FloorToDouble(LocalHit.X / Board.CellSize + 0.5) + Board.GridOrigin.X,
			FMath::FloorToDouble(LocalHit.Y / Board.CellSize + 0.5) + Board.GridOrigin.Y);
	}

	bool TryResolveBuildingFloor(const ACastleBuildingBase& Building, const FHitResult& Hit, FVector& OutPoint, FText& OutFailureReason)
	{
		const auto Reject = [&](const TCHAR* Reason)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("巡逻放点失败：建筑 %s 的功能碰撞 %s %s。"),
				*Building.GetPathName(), *GetNameSafe(Hit.GetComponent()), Reason));
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
			return false;
		};
		// 碰撞所属建筑只用来找到棋盘。叠放后，下层 PCG 管理的顶板属于上一层行走面。
		const ACastleBoard* Board = ACastleBoard::FindBoardForBuilding(&Building);
		if (!Board || !FMath::IsFinite(Board->CellSize) || Board->CellSize <= 0.0
			|| !FMath::IsFinite(Board->LayerHeight) || Board->LayerHeight <= 2.0 * FloorPlaneTolerance
			|| !FMath::IsFinite(Board->WorldBaseHeight) || !Board->GetActorTransform().IsValid()
			|| Board->GetActorTransform().GetScale3D().GetAbsMin() <= UE_SMALL_NUMBER)
		{
			return Reject(TEXT("没有有效的所属棋盘格距、层高或变换"));
		}

		const FTransform& Transform = Board->GetActorTransform();
		const FVector LocalHit = Transform.InverseTransformPosition(Hit.ImpactPoint);
		const double HeightFromFirstFloor = LocalHit.Z - (Board->WorldBaseHeight - Board->LayerHeight * 0.5);
		const double Layer = FMath::RoundToDouble(HeightFromFirstFloor / Board->LayerHeight);
		if (Hit.bStartPenetrating || !Building.IsComponentRelevantForNavigation(Hit.GetComponent())
			|| FVector::DotProduct(Hit.ImpactNormal, Board->GetActorUpVector()) < 0.99
			|| Layer < 0 || Layer >= Board->GetUnlockedLayerCount())
		{
			// 功能分支也能存放护栏和障碍；首个功能碰撞不是本层楼板时拒绝放点，不继续查下方。
			return Reject(TEXT("不是棋盘该层的功能楼板顶面"));
		}

		const FVector2D XY = GetHorizontalGridCell(*Board, LocalHit);
		FIntPoint Min, Max;
		Board->GetBoardExtent(Min, Max);
		if (XY.X < Min.X - Board->MaxOverhangCells || XY.X > Max.X + Board->MaxOverhangCells
			|| XY.Y < Min.Y - Board->MaxOverhangCells || XY.Y > Max.Y + Board->MaxOverhangCells)
		{
			return Reject(TEXT("不在棋盘允许占用的平面范围内"));
		}
		const FIntVector Cell(static_cast<int32>(XY.X), static_cast<int32>(XY.Y), Board->GridOrigin.Z + static_cast<int32>(Layer));
		const ACastleBuildingBase* FloorOwner = Board->GetBuildingAt(Cell);
		if (!IsValid(FloorOwner)) { return Reject(TEXT("命中楼层的对应格没有登记建筑")); }
		// 下层可拥有楼板美术或碰撞。功能高度以命中层实际登记的楼体为准，盾塔仍使用旧标高。
		const double StandingHeight = FloorOwner->HachimisLayout ? FloorOwner->HachimisLayout->StandingHeight : 0.0;
		if (!FMath::IsNearlyEqual(HeightFromFirstFloor, Layer * Board->LayerHeight + StandingHeight, FloorPlaneTolerance))
		{
			return Reject(TEXT("不是棋盘该层的作者站立面"));
		}

		// 只在刚命中的楼板平面内吸附，保留真实高度。格心是否可通行仍由提交时的 NavMesh 验证。
		OutPoint = Transform.TransformPosition(FVector((XY.X - Board->GridOrigin.X) * Board->CellSize,
			(XY.Y - Board->GridOrigin.Y) * Board->CellSize, LocalHit.Z));
		return true;
	}
}

ACastlePatrolRoute::ACastlePatrolRoute()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FClassFinder<ACastlePatrolWaypointMarker> Markers(WaypointMarkerAsset);
	WaypointMarkerClass = Markers.Class;
	// 编辑时先消费左键，避免同一次输入继续放置建筑；退出编辑后左键交还原控制器。
	InputPriority = 10;
}

void ACastlePatrolRoute::BeginPlay()
{
	Super::BeginPlay();
	if (MaxPlayerWaypoints < 1 || MaxPlayerWaypoints > ACastlePatrolWaypointMarker::MaxDisplayedNumber)
	{
		const FString Reason = FString::Printf(TEXT("巡逻入口 %s 的 MaxPlayerWaypoints 必须在 1～%d 之间。"),
			*GetPathName(), ACastlePatrolWaypointMarker::MaxDisplayedNumber);
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *Reason);
		ShowMessage(Reason, false);
		SetActorTickEnabled(false);
		return;
	}
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
	const auto* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>();
	// 出兵会话的门由导航求解时插入；会话之外保留玩家闭环供旧导演与调试入口消费。
	const bool bAccepted = Dispatch && Dispatch->IsSessionActive()
		? Navigation->RequestTowerDispatchRoute(Waypoints, OutFailureReason)
		: Navigation->RequestSharedPatrolRoute(Waypoints, OutFailureReason);
	if (!bAccepted) { return false; }
	LocalWaypoints.Reset(Waypoints.Num());
	for (FVector Point : Waypoints) { LocalWaypoints.Add(GetActorTransform().InverseTransformPosition(Point)); }
	return true;
}

bool ACastlePatrolRoute::CanEdit() const
{
	const auto* Controller = Cast<ACastlePlayerController>(GetWorld()->GetFirstPlayerController());
	FText Reason;
	return bOwnsRoute && bAllowPlayerEditing && (!Controller || (!Controller->GetShop()->IsShopOpen() && Controller->IsCastleEditingAllowed(Reason)));
}

bool ACastlePatrolRoute::SetMaxPlayerWaypoints(const int32 NewLimit)
{
	if (NewLimit < 1 || NewLimit > ACastlePatrolWaypointMarker::MaxDisplayedNumber)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("巡逻入口 %s 拒绝路点上限 %d：结果必须在 1～%d 之间。"),
			*GetPathName(), NewLimit, ACastlePatrolWaypointMarker::MaxDisplayedNumber);
		return false;
	}
	MaxPlayerWaypoints = NewLimit;
	return true;
}

bool ACastlePatrolRoute::AddMaxPlayerWaypoints(const int32 Delta)
{
	const int64 Candidate = static_cast<int64>(MaxPlayerWaypoints) + Delta;
	if (Candidate < 1 || Candidate > ACastlePatrolWaypointMarker::MaxDisplayedNumber)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("巡逻入口 %s 拒绝路点上限变更 %d：结果必须在 1～99 之间。"), *GetPathName(), Delta);
		return false;
	}
	return SetMaxPlayerWaypoints(static_cast<int32>(Candidate));
}

void ACastlePatrolRoute::CancelEditingDraft()
{
	SetEditing(false);
	DraftWaypoints.Reset();
}

void ACastlePatrolRoute::SetEditing(bool bNewEditing)
{
	const bool bChanged = (bEditing != bNewEditing);
	bEditing = bNewEditing;
	if (InputComponent)
	{
		for (FInputKeyBinding& Binding : InputComponent->KeyBindings)
		{
			if (Binding.Chord.Key != EKeys::P) { Binding.bConsumeInput = bEditing; }
		}
	}
	if (bChanged)
	{
		OnEditingStateChanged.Broadcast(bEditing);
	}
}

void ACastlePatrolRoute::UndoDraftPoint()
{
	UndoPoint();
}

void ACastlePatrolRoute::ClearPatrolRoute()
{
	ClearRoute();
}

void ACastlePatrolRoute::ToggleEditing()
{
	const auto* Controller = Cast<ACastlePlayerController>(GetWorld()->GetFirstPlayerController());
	FText Reason;
	if (Controller && !Controller->IsCastleEditingAllowed(Reason)) { ShowMessage(Reason.ToString(), false); return; }
	if (!CanEdit()) { return; }
	if (bEditing)
	{
		CancelEditingDraft();
		ShowMessage(TEXT("已取消路径草稿，继续使用已发布路线。"), true);
		return;
	}
	DraftWaypoints = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>()->GetRequestedWaypoints();
	SetEditing(true);
	ShowMessage(TEXT("巡逻编辑：瞄准建筑楼顶或地面后左键添加路径点；建筑取本层楼内地板，棋盘内地面取格心；Backspace 撤回，Enter 提交路线，Delete 清空，P 取消。"), true);
}

void ACastlePatrolRoute::AddPointAtViewCenter()
{
	if (!bEditing || !CanEdit()) { return; }
	// 路线序号从 1 开始；满 99 点后明确拒绝添加，不循环编号也不跳过显示。
	if (DraftWaypoints.Num() >= ACastlePatrolWaypointMarker::MaxDisplayedNumber)
	{
		const FString Failure = FString::Printf(TEXT("巡逻入口 %s 最多可放置 %d 个带编号的路径点。"),
			*GetPathName(), ACastlePatrolWaypointMarker::MaxDisplayedNumber);
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *Failure);
		ShowMessage(Failure, false);
		return;
	}
	FText CapacityFailure;
	if (!CheckDraftWaypointCapacity(CapacityFailure))
	{
		ShowMessage(CapacityFailure.ToString(), false);
		return;
	}
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
	if (!TryAppendDraftWaypoint(Point, Failure))
	{
		ShowMessage(Failure.ToString(), false);
		return;
	}
	ShowMessage(FString::Printf(TEXT("已放置草稿路径点 %d，Enter 后统一求解全部路段。"), DraftWaypoints.Num()), true);
}

bool ACastlePatrolRoute::CheckDraftWaypointCapacity(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (DraftWaypoints.Num() < MaxPlayerWaypoints) { return true; }
	OutReason = FText::FromString(FString::Printf(TEXT("已达到当前路径点上限 %d。"), MaxPlayerWaypoints));
	UE_LOG(LogMoving_Castle, Warning, TEXT("巡逻入口 %s：%s"), *GetPathName(), *OutReason.ToString());
	return false;
}

bool ACastlePatrolRoute::TryAppendDraftWaypoint(const FVector Point, FText& OutReason)
{
	if (!CheckDraftWaypointCapacity(OutReason)) { return false; }
	if (!DraftWaypoints.IsEmpty() && DraftWaypoints.Last().Equals(Point, 1.0))
	{
		OutReason = FText::FromString(TEXT("新路径点与上一点重合，请选择不同位置。"));
		return false;
	}
	DraftWaypoints.Add(Point);
	return true;
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
	FHitResult Hit, PhysicalHit;
	const bool bVisibleHit = World->LineTraceSingleByChannel(Hit, RayStart, RayEnd, ECC_Visibility, Params);
	const bool bPhysicalHit = World->LineTraceSingleByChannel(PhysicalHit, RayStart, RayEnd, ECC_Pawn, Params);
	const bool bPhysicalInFront = bPhysicalHit && (!bVisibleHit || PhysicalHit.Distance < Hit.Distance);
	const auto* PhysicalBuilding = Cast<ACastleBuildingBase>(PhysicalHit.GetActor());
	// 两个通道各只取首个阻挡。上下叠楼的美术平板与实际楼板重合时，优先取实际楼板；
	// 更靠前的屋顶或墙仍遮挡后面的楼板，不按 XY 去搜索别层。
	if (bPhysicalHit && PhysicalBuilding && UBuildingGameplayRootComponent::IsInGameplayHierarchy(PhysicalHit.GetComponent())
		&& (bPhysicalInFront || (bVisibleHit && Hit.ImpactNormal.Z >= 0.5
			&& FVector::DistSquared(PhysicalHit.ImpactPoint, Hit.ImpactPoint) <= FMath::Square(CoincidentHitTolerance))))
	{
		return TryResolveBuildingFloor(*PhysicalBuilding, PhysicalHit, OutPoint, OutFailureReason);
	}
	if (!bVisibleHit)
	{
		OutFailureReason = FText::FromString(TEXT("请瞄准建筑或可行走地面顶面后放置路径点。"));
		return false;
	}
	if (bPhysicalInFront)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("路径点被碰撞 %s 遮挡，请直接瞄准楼板或建筑。"),
			*GetNameSafe(PhysicalHit.GetComponent())));
		return false;
	}
	if (ACastleBuildingBase* Building = Cast<ACastleBuildingBase>(Hit.GetActor()))
	{
		// 选中美术时保留“命中建筑最近占用格”语义，例如楼顶选该建筑的室内楼板，而不是在屋顶放点。
		for (TActorIterator<ACastleBoard> It(World); It; ++It)
		{
			FCastleGridCursorTarget Target;
			if (It->GetPlacedBuildings().Contains(Building) && It->TryResolveGridCursorTarget(RayStart, RayEnd, Target) && Target.Surface == ECastleGridCursorSurface::Building)
			{
				// 选中屋檐等表现件也要吸附到该占格真实的站立面。旧建筑仍取逻辑格底，V4 取作者标高。
				const ACastleBuildingBase* FloorOwner = It->GetBuildingAt(Target.Cell);
				if (!IsValid(FloorOwner))
				{
					OutFailureReason = FText::FromString(FString::Printf(TEXT("建筑 %s 的巡逻命中格 %s 没有登记楼体。"),
						*Building->GetPathName(), *Target.Cell.ToString()));
					UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
					return false;
				}
				const double StandingHeight = FloorOwner->HachimisLayout ? FloorOwner->HachimisLayout->StandingHeight : 0.0;
				OutPoint = It->GetGridWorldLocation(Target.Cell)
					+ It->GetActorTransform().TransformVector(FVector(0, 0, StandingHeight - It->LayerHeight * 0.5));
				return true;
			}
		}
		OutFailureReason = FText::FromString(FString::Printf(TEXT("建筑 %s 无法通过棋盘解析命中占用格，拒绝巡逻放点。"), *Building->GetPathName()));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
		return false;
	}
	// 仍然需要实际顶面命中；逻辑网格不能为没有碰撞的空间凭空生成一个楼板。
	if (Hit.ImpactNormal.Z < 0.5)
	{
		OutFailureReason = FText::FromString(TEXT("请瞄准可行走楼板或地面顶面后放置路径点。"));
		return false;
	}
	// 主塔侧挂模式没有地块 Actor，但棋盘仍拥有逻辑格；用实际命中的 XY 查格，
	// 不调用限定建造面的取格接口，也不把格心下方的障碍物当成另一个可点击面。
	const ACastleBoard* MatchedBoard = nullptr;
	FVector GridCenter = FVector::ZeroVector;
	for (TActorIterator<ACastleBoard> It(World); It; ++It)
	{
		const FTransform& Transform = It->GetActorTransform();
		if (!FMath::IsFinite(It->CellSize) || It->CellSize <= 0.0f || It->GridSize.X <= 0 || It->GridSize.Y <= 0
			|| !Transform.IsValid() || Transform.GetScale3D().GetAbsMin() <= UE_SMALL_NUMBER)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("棋盘 %s 的格距、尺寸或变换无效，无法解析巡逻格心。"), *It->GetPathName()));
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
			return false;
		}
		const FVector2D XY = GetHorizontalGridCell(**It, Transform.InverseTransformPosition(Hit.ImpactPoint));
		FIntPoint Min, Max;
		It->GetBoardExtent(Min, Max);
		if (XY.X < Min.X || XY.X > Max.X || XY.Y < Min.Y || XY.Y > Max.Y) { continue; }
		if (MatchedBoard)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("同一命中属于棋盘 %s 和 %s，无法唯一确定巡逻格心。"),
				*MatchedBoard->GetPathName(), *It->GetPathName()));
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
			return false;
		}
		MatchedBoard = *It;
		GridCenter = It->GetGridWorldLocation(FIntVector(static_cast<int32>(XY.X), static_cast<int32>(XY.Y), It->GridOrigin.Z));
	}
	// 只改变棋盘内落点的水平位置，沿用原命中层的高度。棋盘外顶面没有格子，仍是原有的自由坐标。
	OutPoint = MatchedBoard ? FVector(GridCenter.X, GridCenter.Y, Hit.ImpactPoint.Z) : Hit.ImpactPoint;
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
	if (!bOwnsRoute) { return; }
	if (!bShowRoute)
	{
		DestroyWaypointMarkers();
		LastMarkerPoints.Reset();
		return;
	}
	const auto* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	// 路线预览只更新场景中的路径点编号，避免逐帧输出调试文字覆盖 HUD。
	const TArray<FVector>& DisplayPoints = bEditing ? DraftWaypoints : Navigation->GetRequestedWaypoints();
	RefreshWaypointMarkers(DisplayPoints);
}

void ACastlePatrolRoute::RefreshWaypointMarkers(const TArray<FVector>& Points)
{
	if (LastMarkerPoints == Points) { return; }
	LastMarkerPoints = Points;
	const auto Reject = [this](const FString& Reason)
	{
		// 不留半套编号误导玩家，也不改动导航子系统的已请求路线。
		DestroyWaypointMarkers();
		const FString Message = FString::Printf(TEXT("巡逻入口 %s 无法显示路径点（%s）：%s"),
			*GetPathName(), WaypointMarkerAsset, *Reason);
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *Message);
		ShowMessage(Message, false);
	};
	if (Points.IsEmpty())
	{
		DestroyWaypointMarkers();
		return;
	}
	if (Points.Num() > ACastlePatrolWaypointMarker::MaxDisplayedNumber)
	{
		Reject(FString::Printf(TEXT("路径有 %d 点，超过标记可显示的 99 个序号。"), Points.Num()));
		return;
	}
	if (!IsValid(WaypointMarkerClass.Get()))
	{
		Reject(TEXT("正式标记蓝图类缺失或未加载。"));
		return;
	}

	while (WaypointMarkers.Num() > Points.Num())
	{
		if (ACastlePatrolWaypointMarker* Marker = WaypointMarkers.Pop(); IsValid(Marker)) { Marker->Destroy(); }
	}
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (WaypointMarkers.IsValidIndex(Index))
		{
			// 已有实例的序号等于数组位置，撤回/取消/提交时只移动变更的实例。
			ACastlePatrolWaypointMarker* Marker = WaypointMarkers[Index];
			if (!IsValid(Marker))
			{
				Reject(FString::Printf(TEXT("第 %d 个标记实例已失效。"), Index + 1));
				return;
			}
			if (Marker->GetActorLocation() != Points[Index] && !Marker->SetActorLocation(Points[Index]))
			{
				Reject(FString::Printf(TEXT("无法移动标记 %s 到路径点 %d。"), *Marker->GetPathName(), Index + 1));
				return;
			}
			continue;
		}
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// 模型枢轴位于地面且悬浮高度已做在网格里。Actor 直接放在导航请求点，
		// 不额外移动导航坐标，也不把模型的可见高度当成 NPC 的站立高度。
		ACastlePatrolWaypointMarker* Marker = GetWorld()->SpawnActor<ACastlePatrolWaypointMarker>(
			WaypointMarkerClass, Points[Index], FRotator::ZeroRotator, SpawnParameters);
		if (!IsValid(Marker))
		{
			Reject(FString::Printf(TEXT("无法生成第 %d 个标记。"), Index + 1));
			return;
		}
		WaypointMarkers.Add(Marker);
		if (!Marker->SetDisplayedNumber(Index + 1))
		{
			Reject(FString::Printf(TEXT("无法设置第 %d 个标记的数字，请检查标记资产日志。"), Index + 1));
			return;
		}
	}
}

void ACastlePatrolRoute::DestroyWaypointMarkers()
{
	// Actor 的 Owner 并不自动销毁子 Actor，关卡切换和 PIE 收尾显式释放自己创建的标记。
	for (ACastlePatrolWaypointMarker* Marker : WaypointMarkers)
	{
		if (IsValid(Marker)) { Marker->Destroy(); }
	}
	WaypointMarkers.Reset();
}

void ACastlePatrolRoute::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyWaypointMarkers();
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
