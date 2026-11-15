#include "CastleLadderLinkComponent.h"

#include "Engine/World.h"
#include "Moving_Castle.h"
#include "NavigationSystem.h"
#include "Units/Navigation/CastleNavigationSubsystem.h"

UCastleLadderLinkComponent::UCastleLadderLinkComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
}

bool UCastleLadderLinkComponent::ValidateGeometry(FText& OutFailure) const
{
	OutFailure = FText::GetEmpty();
	const FVector Offsets[] = {BottomStandOffset, LadderBottomOffset, LadderTopOffset, TopStandOffset};
	for (const FVector& Offset : Offsets)
	{
		if (Offset.ContainsNaN())
		{
			OutFailure = FText::FromString(TEXT("梯子接驳偏移必须是有限向量。"));
			return false;
		}
	}
	const double WorldHeight = GetComponentTransform().TransformPosition(LadderTopOffset).Z
		- GetComponentTransform().TransformPosition(LadderBottomOffset).Z;
	if (!FMath::IsFinite(WorldHeight) || WorldHeight <= UE_DOUBLE_SMALL_NUMBER)
	{
		OutFailure = FText::FromString(TEXT("梯子顶端必须高于底端，且上下端高度差必须有限并大于零。"));
		return false;
	}
	return true;
}

bool UCastleLadderLinkComponent::GetTraversalPolyline(const bool bClimbingUp, TArray<FVector>& OutWorldPoints) const
{
	OutWorldPoints.Reset();
	FText Failure;
	if (!ValidateGeometry(Failure))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("梯子导航组件 %s 几何无效：%s"), *GetPathName(), *Failure.ToString());
		return false;
	}
	const FTransform Transform = GetComponentTransform();
	OutWorldPoints = {
		Transform.TransformPosition(BottomStandOffset),
		Transform.TransformPosition(LadderBottomOffset),
		Transform.TransformPosition(LadderTopOffset),
		Transform.TransformPosition(TopStandOffset)
	};
	if (!bClimbingUp) { Algo::Reverse(OutWorldPoints); }
	return true;
}

void UCastleLadderLinkComponent::GetLinkData(FVector& LeftPt, FVector& RightPt, ENavLinkDirection::Type& Direction) const
{
	const AActor* Owner = GetOwner();
	const FTransform OwnerTransform = Owner ? Owner->GetActorTransform() : FTransform::Identity;
	const FTransform ComponentTransform = GetComponentTransform();
	LeftPt = OwnerTransform.InverseTransformPosition(ComponentTransform.TransformPosition(LadderBottomOffset));
	RightPt = OwnerTransform.InverseTransformPosition(ComponentTransform.TransformPosition(LadderTopOffset));
	Direction = ENavLinkDirection::BothWays;
}

void UCastleLadderLinkComponent::GetSupportedAgents(FNavAgentSelector& OutSupportedAgents) const
{
	OutSupportedAgents = FNavAgentSelector();
}

TSubclassOf<UNavArea> UCastleLadderLinkComponent::GetLinkAreaClass() const
{
	return nullptr;
}

UObject* UCastleLadderLinkComponent::GetLinkOwner() const
{
	return GetOwner();
}

void UCastleLadderLinkComponent::OnRegister()
{
	Super::OnRegister();
	if (IsTemplate() || !bLadderLinkEnabled) { return; }
	FText Failure;
	if (!ValidateGeometry(Failure))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("梯子导航组件 %s 拒绝注册：%s"), *GetPathName(), *Failure.ToString());
		return;
	}
	UNavigationSystemV1* Navigation = GetWorld()
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()) : nullptr;
	if (!Navigation)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("梯子导航组件 %s 所属世界没有 NavigationSystem，无法注册双向连接。"), *GetPathName());
		return;
	}
	Navigation->RegisterCustomLink(*this);
	RegisteredNavigation = Navigation;
	if (!CustomLinkId.IsValid())
	{
		RegisteredNavigation.Reset();
		UE_LOG(LogMoving_Castle, Error, TEXT("梯子导航组件 %s 注册后未获得有效 FNavLinkId。"), *GetPathName());
		return;
	}
	if (UCastleNavigationSubsystem* Subsystem = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>())
	{
		if (!Subsystem->RequestNavigationRefresh(Failure))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("梯子导航组件 %s 已登记但无法请求 NavMesh 刷新：%s"), *GetPathName(), *Failure.ToString());
		}
	}
}

void UCastleLadderLinkComponent::OnUnregister()
{
	if (UNavigationSystemV1* Navigation = RegisteredNavigation.Get())
	{
		Navigation->UnregisterCustomLink(*this);
		RegisteredNavigation.Reset();
		if (GetWorld() && !GetWorld()->bIsTearingDown)
		{
			if (UCastleNavigationSubsystem* Subsystem = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>())
			{
				FText Failure;
				if (!Subsystem->RequestNavigationRefresh(Failure))
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("梯子导航组件 %s 已注销但无法请求 NavMesh 刷新：%s"), *GetPathName(), *Failure.ToString());
				}
			}
		}
	}
	Super::OnUnregister();
}
