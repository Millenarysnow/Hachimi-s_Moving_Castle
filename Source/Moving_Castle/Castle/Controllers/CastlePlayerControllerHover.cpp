#include "CastlePlayerController.h"

#include "Castle/Actors/CastleBuildingBase.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Castle/Items/CastleRoomTraitComponent.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "Castle/Rendering/CastleHoverHighlightSubsystem.h"
#include "Castle/SkyIslands/CastleSkyIslandBase.h"
#include "Castle/SkyIslands/CastleSkyIslandTypes.h"
#include "Castle/SkyIslands/Pinball/CastlePinballNPCSkyIsland.h"
#include "Castle/Support/CastleSupportFlightActors.h"
#include "CastleShopComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"
#include "CastleTowerDispatchSubsystem.h"

void ACastlePlayerController::ClearHoverHighlight()
{
	// 清除只访问当前世界的渲染子系统，世界销毁阶段允许它已经先行注销。
	if (UWorld* World = GetWorld())
	{
		if (UCastleHoverHighlightSubsystem* Highlight = World->GetSubsystem<UCastleHoverHighlightSubsystem>())
		{
			Highlight->ClearHighlight();
		}
	}
}

bool ACastlePlayerController::IsHoverHighlightSuppressed() const
{
	// 模态、拆除菜单和临时 HUD 指针期间，世界准星不能继续声明可用目标。
	if (bModalInputActive || (Shop && Shop->IsShopOpen()) || BuildingDemolishMenu || bHoldingTemporaryHUDPointer)
	{
		return true;
	}

	// 路径编辑会把世界指针用于布点，不能同时显示普通玩法交互的描边。
	if (const auto* Route = Cast<ACastlePatrolRoute>(UGameplayStatics::GetActorOfClass(
		GetWorld(), ACastlePatrolRoute::StaticClass())); Route && Route->IsEditing())
	{
		return true;
	}

	// 弹珠机已接管输入后，投币台再次命中不应表现为可进入交互。
	if (const auto* Pinball = Cast<ACastlePinballNPCSkyIsland>(GetInteractiveSkyIsland());
		Pinball && Pinball->IsAimModeActive())
	{
		return true;
	}
	return false;
}

void ACastlePlayerController::UpdateHoverHighlight()
{
	if (!IsLocalController())
	{
		ClearHoverHighlight();
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("控制器 %s 无法更新悬停描边：World 无效。"), *GetPathName());
		return;
	}
	UCastleHoverHighlightSubsystem* Highlight = World->GetSubsystem<UCastleHoverHighlightSubsystem>();
	if (!IsValid(Highlight))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("控制器 %s 的世界 %s 缺少悬停描边子系统。"),
			*GetPathName(), *World->GetPathName());
		return;
	}
	if (IsHoverHighlightSuppressed())
	{
		Highlight->ClearHighlight();
		return;
	}

	FVector RayOrigin;
	FVector RayDirection;
	if (!TryGetAimWorldRay(RayOrigin, RayDirection))
	{
		Highlight->ClearHighlight();
		return;
	}

	// 只使用双操作模式共同的 Visibility 首个命中，避免穿透墙体挑中后方目标。
	const FVector RayEnd = RayOrigin + RayDirection * GridCursorTraceDistance;
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleHoverHighlight), false, GetPawn());
	if (!World->LineTraceSingleByChannel(Hit, RayOrigin, RayEnd, ECC_Visibility, Query))
	{
		Highlight->ClearHighlight();
		return;
	}

	TArray<UPrimitiveComponent*> Primitives;
	AActor* HitActor = Hit.GetActor();
	UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (!IsValid(HitActor) || !IsValid(HitComponent))
	{
		Highlight->ClearHighlight();
		return;
	}

	// 房间传送门必须优先于它所在的建筑，只给真正命中的门体反馈。
	if (UCastleRoomTraitComponent* Portal = Cast<UCastleRoomTraitComponent>(HitComponent);
		Portal && Portal->IsPortalAvailable())
	{
		FText Reason;
		const auto* Building = Cast<ACastleBuildingBase>(HitActor);
		if (CanUseBuildingDemolishMenu(Reason) && Building && Building->GetBuildingLevel() <= 4
			&& IsDemolishableBuilding(Building))
		{
			Primitives.Add(Portal);
		}
		Highlight->SetHighlightedPrimitives(Primitives);
		return;
	}

	// 出兵会话期间命中登记主塔时，只描可见模型和存活吊挂，排除侧格纹理面板。
	UCastleTowerDispatchSubsystem* Dispatch = World->GetSubsystem<UCastleTowerDispatchSubsystem>();
	UCastleMainTowerSubsystem* Towers = World->GetSubsystem<UCastleMainTowerSubsystem>();
	UCastleMainTowerComponent* MainTower = Towers ? Towers->GetRegisteredMainTower() : nullptr;
	AActor* MainTowerActor = IsValid(MainTower) ? MainTower->GetOwner() : nullptr;
	if (Dispatch && Dispatch->IsSessionActive() && IsValid(MainTowerActor) && HitActor == MainTowerActor)
	{
		TSet<const UPrimitiveComponent*> SideGridVisuals;
		if (const UCastleTowerSideGridComponent* SideGrid = MainTowerActor->FindComponentByClass<UCastleTowerSideGridComponent>())
		{
			for (const FCastleTowerSideCell& Cell : SideGrid->GetCells())
			{
				if (Cell.Panel) { SideGridVisuals.Add(Cell.Panel.Get()); }
			}
		}

		TArray<UPrimitiveComponent*> Components;
		MainTowerActor->GetComponents<UPrimitiveComponent>(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			if (IsValid(Component) && Component->IsRegistered() && Component->IsVisible()
				&& !SideGridVisuals.Contains(Component))
			{
				Primitives.Add(Component);
			}
		}
		if (!ensureMsgf(!Primitives.IsEmpty(), TEXT("主塔 %s 没有可用于悬停描边的可见外观组件。"),
			*MainTowerActor->GetPathName()))
		{
			Highlight->ClearHighlight();
			return;
		}
		Highlight->SetHighlightedPrimitives(Primitives);
		return;
	}

	// 补给气球只在 ClickSphere 仍能接收 Visibility 命中时高亮其三段可见外观。
	if (ACastleSupplyBalloon* Balloon = Cast<ACastleSupplyBalloon>(HitActor))
	{
		USphereComponent* ClickSphere = Cast<USphereComponent>(Balloon->GetRootComponent());
		if (!ensureMsgf(IsValid(ClickSphere), TEXT("补给气球 %s 缺少 ClickSphere 根组件。"), *Balloon->GetPathName()))
		{
			Highlight->ClearHighlight();
			return;
		}
		if (ClickSphere->GetCollisionEnabled() == ECollisionEnabled::NoCollision
			|| ClickSphere->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block)
		{
			Highlight->ClearHighlight();
			return;
		}

		bool bHasCanopy = false;
		bool bHasTether = false;
		bool bHasCrate = false;
		TArray<UPrimitiveComponent*> Components;
		Balloon->GetComponents<UPrimitiveComponent>(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			if (!IsValid(Component) || !Component->IsRegistered()) { continue; }
			if (Component->GetFName() == TEXT("BalloonCanopy"))
			{
				bHasCanopy = true;
				if (Component->IsVisible()) { Primitives.Add(Component); }
			}
			else if (Component->GetFName() == TEXT("CargoTether"))
			{
				bHasTether = true;
				if (Component->IsVisible()) { Primitives.Add(Component); }
			}
			else if (Component->GetFName() == TEXT("SupplyCrate"))
			{
				bHasCrate = true;
				if (Component->IsVisible()) { Primitives.Add(Component); }
			}
		}
		if (!ensureMsgf(bHasCanopy && bHasTether && bHasCrate,
			TEXT("补给气球 %s 缺少 BalloonCanopy、CargoTether 或 SupplyCrate 组件。"), *Balloon->GetPathName()))
		{
			Highlight->ClearHighlight();
			return;
		}
		Highlight->SetHighlightedPrimitives(Primitives);
		return;
	}

	// 空岛只对已登记当前岛上的交互接口调用纯查询，不触发购买、领取或投币副作用。
	if (ACastleSkyIslandBase* Island = GetInteractiveSkyIsland(); Island && HitActor == Island)
	{
		FCastleSkyIslandInteractContext Context{Island, this, Hit};
		if (ICastleSkyIslandInteractable* ComponentInteractable = Cast<ICastleSkyIslandInteractable>(HitComponent))
		{
			if (ComponentInteractable->CanHighlightInteract(Context) && HitComponent->IsVisible())
			{
				Primitives.Add(HitComponent);
			}
		}
		else if (ICastleSkyIslandInteractable* ActorInteractable = Cast<ICastleSkyIslandInteractable>(HitActor);
			ActorInteractable && ActorInteractable->CanHighlightInteract(Context))
		{
			HitActor->GetComponents<UPrimitiveComponent>(Primitives);
			Primitives.RemoveAll([](const UPrimitiveComponent* Component)
			{
				return !IsValid(Component) || !Component->IsRegistered() || !Component->IsVisible();
			});
		}
		Highlight->SetHighlightedPrimitives(Primitives);
		return;
	}

	// 建筑仅在可拆除编辑期高亮，避免高等级不支持拆除时每帧调用带错误日志的门控。
	if (ACastleBuildingBase* Building = Cast<ACastleBuildingBase>(HitActor))
	{
		FText Reason;
		if (CanUseBuildingDemolishMenu(Reason) && Building->GetBuildingLevel() <= 4
			&& IsDemolishableBuilding(Building))
		{
			TArray<UPrimitiveComponent*> Components;
			Building->GetComponents<UPrimitiveComponent>(Components);
			for (UPrimitiveComponent* Component : Components)
			{
				if (!IsValid(Component) || !Component->IsRegistered() || !Component->IsVisible()
					|| UBuildingGameplayRootComponent::IsInGameplayHierarchy(Component)
					|| Cast<UCastleRoomTraitComponent>(Component)
					|| Component->GetFName() == TEXT("PrototypeLabel")
					|| Component->GetFName() == TEXT("PrototypeLabelPlate"))
				{
					continue;
				}
				Primitives.Add(Component);
			}
			if (!ensureMsgf(!Primitives.IsEmpty(), TEXT("可拆除建筑 %s 没有已注册的可见外观组件。"),
				*Building->GetPathName()))
			{
				Highlight->ClearHighlight();
				return;
			}
		}
		Highlight->SetHighlightedPrimitives(Primitives);
		return;
	}

	// 任何未归入允许目标的首个命中都会清空上一个目标，防止移开后残留白边。
	Highlight->ClearHighlight();
}
