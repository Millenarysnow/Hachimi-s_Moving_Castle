#include "CastleHoverHighlightSubsystem.h"

#include "Castle/Rendering/CastleRenderingConstants.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/UObjectGlobals.h"

bool UCastleHoverHighlightSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCastleHoverHighlightSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("悬停高亮子系统无法初始化：World 无效。"));
		return;
	}

	UMaterialInterface* HoverMaterial = LoadObject<UMaterialInterface>(
		nullptr, CastleRenderingConstants::HoverOutlineMaterialPath);
	if (!IsValid(HoverMaterial))
	{
		UE_LOG(LogMoving_Castle, Error,
			TEXT("世界 %s 无法加载悬停描边后处理材质 %s。"),
			*World->GetPathName(), CastleRenderingConstants::HoverOutlineMaterialPath);
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.Name = MakeUniqueObjectName(
		World, APostProcessVolume::StaticClass(), TEXT("CastleHoverOutlinePostProcess"));
	HoverPostProcessVolume = World->SpawnActor<APostProcessVolume>(
		APostProcessVolume::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!IsValid(HoverPostProcessVolume))
	{
		UE_LOG(LogMoving_Castle, Error,
			TEXT("世界 %s 无法创建悬停描边无界后处理体积，材质路径为 %s。"),
			*World->GetPathName(), CastleRenderingConstants::HoverOutlineMaterialPath);
		return;
	}

	// 仅追加一个 WeightedBlendable。FPostProcessSettings 的其它覆盖标志保持关闭，
	// 因此不会改动关卡已有的曝光、调色、抗锯齿或镜头参数。
	HoverPostProcessVolume->bUnbound = true;
	HoverPostProcessVolume->BlendWeight = 1.0f;
	HoverPostProcessVolume->Settings.WeightedBlendables.Array.Add(
		FWeightedBlendable(1.0f, HoverMaterial));
	bReady = true;
}

void UCastleHoverHighlightSubsystem::Deinitialize()
{
	ClearHighlight();
	// 体积由同一世界的关卡持有，随世界清理。此时世界上下文可能已经注销，不能再次主动销毁 Actor。
	HoverPostProcessVolume = nullptr;
	bReady = false;
	Super::Deinitialize();
}

void UCastleHoverHighlightSubsystem::SetHighlightedPrimitives(
	const TArray<UPrimitiveComponent*>& Primitives)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogMoving_Castle, Error,
			TEXT("世界 %s 的悬停高亮只能从游戏线程更新。"), *GetPathNameSafe(GetWorld()));
		return;
	}
	if (!bReady)
	{
		// 初始化时已记录材质或体积错误，此处不在每帧调用中重复输出同一错误。
		return;
	}

	TMap<TWeakObjectPtr<UPrimitiveComponent>, FOriginalCustomDepthState> DesiredStates;
	for (UPrimitiveComponent* Component : Primitives)
	{
		if (!IsValid(Component) || !Component->IsRegistered() || Component->GetWorld() != GetWorld())
		{
			UE_LOG(LogMoving_Castle, Error,
				TEXT("世界 %s 收到无效悬停组件 %s，当前高亮集合保持不变。"),
				*GetPathNameSafe(GetWorld()), *GetPathNameSafe(Component));
			return;
		}

		const TWeakObjectPtr<UPrimitiveComponent> WeakComponent(Component);
		if (DesiredStates.Contains(WeakComponent))
		{
			continue;
		}
		if (const FOriginalCustomDepthState* ExistingState = OriginalComponentStates.Find(WeakComponent))
		{
			DesiredStates.Add(WeakComponent, *ExistingState);
		}
		else
		{
			FOriginalCustomDepthState OriginalState;
			OriginalState.bRenderCustomDepth = Component->bRenderCustomDepth;
			OriginalState.CustomDepthStencilValue = Component->CustomDepthStencilValue;
			DesiredStates.Add(WeakComponent, OriginalState);
		}
	}

	// 清理已销毁组件的弱引用，确保后续集合对比只考虑有效组件。
	for (auto Iterator = OriginalComponentStates.CreateIterator(); Iterator; ++Iterator)
	{
		if (!Iterator.Key().IsValid())
		{
			Iterator.RemoveCurrent();
		}
	}

	bool bSameComponentSet = DesiredStates.Num() == OriginalComponentStates.Num();
	if (bSameComponentSet)
	{
		for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FOriginalCustomDepthState>& Pair : DesiredStates)
		{
			if (!OriginalComponentStates.Contains(Pair.Key))
			{
				bSameComponentSet = false;
				break;
			}
		}
	}
	if (bSameComponentSet)
	{
		return;
	}

	// 目标切换时，先还原离开集合的组件，再为新组件设置悬停位。
	for (auto Iterator = OriginalComponentStates.CreateIterator(); Iterator; ++Iterator)
	{
		if (DesiredStates.Contains(Iterator.Key()))
		{
			continue;
		}
		if (UPrimitiveComponent* Component = Iterator.Key().Get())
		{
			RestoreComponent(*Component, Iterator.Value());
		}
		Iterator.RemoveCurrent();
	}

	// 原有 Stencil 位全部保留，只将 bit 1 合并到当前值中。
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FOriginalCustomDepthState>& Pair : DesiredStates)
	{
		if (OriginalComponentStates.Contains(Pair.Key))
		{
			continue;
		}

		UPrimitiveComponent* Component = Pair.Key.Get();
		if (!IsValid(Component))
		{
			continue;
		}
		OriginalComponentStates.Add(Pair.Key, Pair.Value);

		const int32 CurrentStencil = Component->CustomDepthStencilValue;
		const int32 StencilWithHoverBit = CurrentStencil | CastleRenderingConstants::HoverOutlineStencilBit;
		if (CurrentStencil != StencilWithHoverBit)
		{
			Component->SetCustomDepthStencilValue(StencilWithHoverBit);
		}
		if (!Component->bRenderCustomDepth)
		{
			Component->SetRenderCustomDepth(true);
		}
	}
}

void UCastleHoverHighlightSubsystem::ClearHighlight()
{
	if (!IsInGameThread())
	{
		UE_LOG(LogMoving_Castle, Error,
			TEXT("世界 %s 的悬停高亮只能从游戏线程清空。"), *GetPathNameSafe(GetWorld()));
		return;
	}

	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FOriginalCustomDepthState>& Pair : OriginalComponentStates)
	{
		if (UPrimitiveComponent* Component = Pair.Key.Get())
		{
			RestoreComponent(*Component, Pair.Value);
		}
	}
	OriginalComponentStates.Reset();
}

TArray<UPrimitiveComponent*> UCastleHoverHighlightSubsystem::GetHighlightedPrimitives() const
{
	TArray<UPrimitiveComponent*> Result;
	for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FOriginalCustomDepthState>& Pair : OriginalComponentStates)
	{
		if (UPrimitiveComponent* Component = Pair.Key.Get())
		{
			Result.Add(Component);
		}
	}
	return Result;
}

void UCastleHoverHighlightSubsystem::RestoreComponent(
	UPrimitiveComponent& Component, const FOriginalCustomDepthState& State) const
{
	if (Component.CustomDepthStencilValue != State.CustomDepthStencilValue)
	{
		Component.SetCustomDepthStencilValue(State.CustomDepthStencilValue);
	}
	if (Component.bRenderCustomDepth != State.bRenderCustomDepth)
	{
		Component.SetRenderCustomDepth(State.bRenderCustomDepth);
	}
}
