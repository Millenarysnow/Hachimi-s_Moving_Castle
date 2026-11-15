#include "CastleLadderLinkComponent.h"

#include "Algo/Reverse.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "AI/NavigationSystemHelpers.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "NavAreas/NavArea_Default.h"

#include "Engine/World.h"
#include "Moving_Castle.h"
#include "NavigationSystem.h"
#include "Units/Navigation/CastleNavigationSubsystem.h"

UCastleLadderLinkComponent::UCastleLadderLinkComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
	bCanEverAffectNavigation = true;
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
	// 导出接口要求真实所属 Actor；缺失时明确报错，不用单位变换替代配置。
	if (!ensureMsgf(Owner, TEXT("梯子导航组件 %s 没有所属 Actor。"), *GetPathName()))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("梯子导航组件 %s 缺少所属 Actor，无法导出接驳点。"), *GetPathName());
		return;
	}
	const FTransform OwnerTransform = Owner->GetActorTransform();
	const FTransform ComponentTransform = GetComponentTransform();
	LeftPt = OwnerTransform.InverseTransformPosition(ComponentTransform.TransformPosition(BottomStandOffset));
	RightPt = OwnerTransform.InverseTransformPosition(ComponentTransform.TransformPosition(TopStandOffset));
	Direction = ENavLinkDirection::BothWays;
}

void UCastleLadderLinkComponent::GetSupportedAgents(FNavAgentSelector& OutSupportedAgents) const
{
	OutSupportedAgents = FNavAgentSelector();
}

TSubclassOf<UNavArea> UCastleLadderLinkComponent::GetLinkAreaClass() const
{
	return UNavArea_Default::StaticClass();
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
	// 动态连接由组件先生成唯一 ID，再向系统登记；RegisterCustomLink 不会补齐无效 ID。
	CustomLinkId = FNavLinkId::GenerateUniqueId();
	Navigation->RegisterCustomLink(*this);
	RegisteredNavigation = Navigation;
	FNavigationSystem::OnComponentRegistered(*this);
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
	FNavigationSystem::OnComponentUnregistered(*this);
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

bool UCastleLadderLinkComponent::IsNavigationRelevant() const
{
	return bLadderLinkEnabled && CustomLinkId.IsValid() && RegisteredNavigation.IsValid()
		&& UBuildingGameplayRootComponent::IsInGameplayHierarchy(this);
}

FBox UCastleLadderLinkComponent::GetNavigationBounds() const
{
	FBox LinkBounds(ForceInit);
	LinkBounds += GetComponentTransform().TransformPosition(BottomStandOffset);
	LinkBounds += GetComponentTransform().TransformPosition(TopStandOffset);
	return LinkBounds.ExpandBy(10.0);
}

void UCastleLadderLinkComponent::GetNavigationData(FNavigationRelevantData& Data) const
{
	if (!IsNavigationRelevant()) { return; }
	FNavigationLink Link = INavLinkCustomInterface::GetModifier(this);
	// 接驳点本身由调用方放在楼板上，禁止向下投影到其他楼层，也不扩大吸附范围掩盖错误配置。
	Link.LeftProjectHeight = 0.0f;
	Link.MaxFallDownLength = 0.0f;
	Link.SnapRadius = 10.0f;
	Link.bUseSnapHeight = true;
	Link.SnapHeight = 20.0f;
	const TArray<FNavigationLink> Links = {Link};
	NavigationHelper::ProcessNavLinkAndAppend(&Data.Modifiers, GetOwner(), Links);
}
