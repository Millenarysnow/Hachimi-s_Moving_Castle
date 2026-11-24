#include "CastleBuildingPCGComponent.h"

#include "CastlePCGSemantics.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "BuildingMergeComponent.h"
#include "CastleLadderLinkComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/SecureHash.h"
#include "Moving_Castle.h"

namespace CastlePCGComponents
{
	const FName NavigationBarrierTag(TEXT("CastlePCGNavigationBarrier"));

	// 保留浮点全部有效位，小幅改变挂点或碰撞尺寸也必须改变所属通道的键。
	FString VectorKey(const FVector& Vector)
	{
		return FString::Printf(TEXT("%.17g,%.17g,%.17g"), Vector.X, Vector.Y, Vector.Z);
	}

	FString TransformKey(const FTransform& Transform)
	{
		const FQuat Rotation = Transform.GetRotation();
		return VectorKey(Transform.GetTranslation()) + TEXT("/")
			+ FString::Printf(TEXT("%.17g,%.17g,%.17g,%.17g"), Rotation.X, Rotation.Y, Rotation.Z, Rotation.W)
			+ TEXT("/") + VectorKey(Transform.GetScale3D());
	}

	FString StableKey(TArray<FString>& Entries)
	{
		if (Entries.IsEmpty()) { return {}; }
		Entries.Sort();
		const FTCHARToUTF8 Bytes(*FString::Join(Entries, TEXT("|")));
		FMD5 Hash;
		Hash.Update(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
		uint8 Result[16];
		Hash.Final(Result);
		return BytesToHex(Result, UE_ARRAY_COUNT(Result));
	}
}

void FCastlePCGApplyPlan::FinalizeKeys()
{
	using namespace CastlePCGComponents;
	TArray<FString> Keys;
	for (const UPrimitiveComponent* Floor : Gameplay.DisabledFloors) { Keys.Add(TEXT("Floor/") + Floor->GetName()); }
	for (const FCastlePCGFunctionalBox& Box : Gameplay.FunctionalBoxes)
	{
		Keys.Add((Box.bIsNavigationBarrier ? TEXT("Barrier/") : TEXT("Surface/")) + TransformKey(Box.Relative) + TEXT("/") + VectorKey(Box.Extent));
	}
	for (const FCastlePCGLadderPlan& Link : Gameplay.LadderFrames)
	{
		Keys.Add(TEXT("Ladder/") + Link.LowerCell.ToString() + TEXT("/") + TransformKey(Link.Relative)
			+ TEXT("/") + VectorKey(Link.BottomStand) + TEXT("/") + VectorKey(Link.TopStand)
			+ TEXT("/") + VectorKey(Link.LadderBottom) + TEXT("/") + VectorKey(Link.LadderTop));
	}
	Gameplay.Key = StableKey(Keys);

	// 视觉签名只描述真正生成的组件。关闭楼板的玩法碰撞不会改变本键。
	Keys.Reset();
	for (const UPrimitiveComponent* Hidden : Visual.Hidden) { Keys.Add(TEXT("Hide/") + Hidden->GetName()); }
	for (const FCastlePCGMeshTemplate& Part : Visual.Visuals)
	{
		FString Key = Part.Mesh->GetPathName() + TEXT("/") + Part.Node.ToString() + TEXT("/") + Part.Group.ToString()
			+ TEXT("/") + TransformKey(Part.Relative) + TEXT("/") + LexToString(Part.bSelectable);
		for (const UMaterialInterface* Material : Part.Materials) { Key += TEXT("/") + GetPathNameSafe(Material); }
		Keys.Add(MoveTemp(Key));
	}
	Visual.Key = StableKey(Keys);
}

bool UCastleBuildingPCGComponent::IsNavigationBarrier(const UPrimitiveComponent* Component)
{
	return Component && Component->ComponentHasTag(CastlePCGComponents::NavigationBarrierTag);
}

bool UCastleBuildingPCGComponent::CacheOriginalParts(const ACastleBoard* Board, FString& Error)
{
	if (bCached) { return true; }
	ACastleBuildingBase* Building = CastChecked<ACastleBuildingBase>(GetOwner());
	TArray<FCastlePCGPartAnnotation> Parts;
	if (!CastlePCGSemantics::Extract(Building, Board, Parts, Error)) { return false; }
	if (Building->HachimisLayout)
	{
		for (const FCastlePCGPartAnnotation& Part : Parts)
		{
			// V4 根之外的原件只能用于拾取和渲染，纯视觉切换不得碰导航几何。
			if (Part.Component->CanEverAffectNavigation() || Part.Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Ignore)
			{
				Error = Part.Component->GetPathName() + TEXT(" 的 V4 视觉原件含 Pawn 碰撞或导航资格。");
				OriginalVisualCollision.Reset();
				return false;
			}
			OriginalVisualCollision.Add(Part.Component.Get(), Part.Component->GetCollisionEnabled());
		}
	}
	OriginalParts = MoveTemp(Parts);
	bCached = true;
	return true;
}

void UCastleBuildingPCGComponent::DestroyDynamicComponents()
{
	for (USceneComponent* Component : DynamicComponents) { Component->DestroyComponent(); }
	DynamicComponents.Reset();
	ActiveLadders.Reset();
}

bool UCastleBuildingPCGComponent::ApplyGameplay(const FCastlePCGGameplayPlan& Plan, FString& Error)
{
	ACastleBuildingBase* Building = CastChecked<ACastleBuildingBase>(GetOwner());
	check(!BeforeGameplay.IsSet());
	FBeforeGameplay& Previous = BeforeGameplay.Emplace();
	Previous.Components = MoveTemp(DynamicComponents);
	Previous.Ladders = MoveTemp(ActiveLadders);
	Previous.Suppressed = MoveTemp(SuppressedCollision);
	Previous.PendingKey = Plan.Key;

	// 旧 NavLink 保留至提交点，回滚时无需重派 ID。旧功能盒暂停碰撞，避免混入新几何。
	auto SetCollision = [&](UPrimitiveComponent* Component, ECollisionEnabled::Type Collision)
	{
		Previous.ChangedPrimitives.FindOrAdd(Component, Component->GetCollisionEnabled());
		Component->SetCollisionEnabled(Collision);
	};
	for (USceneComponent* Component : Previous.Components)
	{
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
		{
			SetCollision(Primitive, ECollisionEnabled::NoCollision);
		}
	}
	for (const auto& Pair : Previous.Suppressed)
	{
		if (UPrimitiveComponent* Primitive = Pair.Key.Get()) { SetCollision(Primitive, Pair.Value); }
	}
	for (UPrimitiveComponent* Floor : Plan.DisabledFloors)
	{
		SuppressedCollision.Add(Floor, Floor->GetCollisionEnabled());
		SetCollision(Floor, ECollisionEnabled::NoCollision);
	}
	for (const FCastlePCGFunctionalBox& Box : Plan.FunctionalBoxes)
	{
		auto* Collision = NewObject<UBoxComponent>(Building, NAME_None, RF_Transient);
		Collision->SetupAttachment(Building->GameplayRoot);
		Collision->SetMobility(EComponentMobility::Movable);
		Collision->SetBoxExtent(Box.Extent);
		Collision->SetRelativeTransform(Box.Relative);
		// 标记用途供巡逻放点区分墙与楼板；所有功能盒继续参与 Pawn 碰撞和导航。
		if (Box.bIsNavigationBarrier) { Collision->ComponentTags.Add(CastlePCGComponents::NavigationBarrierTag); }
		Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
		Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Collision->SetCanEverAffectNavigation(true);
		Collision->SetGenerateOverlapEvents(false);
		Collision->SetVisibility(false);
		Collision->SetHiddenInGame(true);
		Building->AddInstanceComponent(Collision);
		Collision->RegisterComponent();
		DynamicComponents.Add(Collision);
	}
	for (const auto& Frame : Plan.LadderFrames)
	{
		auto* Link = NewObject<UCastleLadderLinkComponent>(Building, NAME_None, RF_Transient);
		Link->SetupAttachment(Building->GameplayRoot);
		Link->SetMobility(EComponentMobility::Movable);
		Link->SetRelativeTransform(Frame.Relative);
		Link->BottomStandOffset = Frame.BottomStand;
		Link->LadderBottomOffset = Frame.LadderBottom;
		Link->LadderTopOffset = Frame.LadderTop;
		Link->TopStandOffset = Frame.TopStand;
		Building->AddInstanceComponent(Link);
		Link->RegisterComponent();
		DynamicComponents.Add(Link);
		if (!Link->GetId().IsValid())
		{ Error = FString::Printf(TEXT("%s 的动态梯子未获得有效导航ID。"), *Building->GetPathName()); return false; }
		FCastleActiveLadderConnection& Connection = ActiveLadders.AddDefaulted_GetRef();
		Connection.Building = Building;
		Connection.LowerCell = Frame.LowerCell;
		Connection.UpperCell = Frame.LowerCell + FIntVector(0, 0, 1);
		Connection.BottomWorldLocation = Link->GetComponentTransform().TransformPosition(Link->LadderBottomOffset);
		Connection.TopWorldLocation = Link->GetComponentTransform().TransformPosition(Link->LadderTopOffset);
		Connection.WorldFacing = (Link->GetComponentTransform().TransformVectorNoScale((Frame.LadderBottom - Frame.BottomStand).GetSafeNormal2D())).Rotation();
	}
	return true;
}

void UCastleBuildingPCGComponent::FinishGameplay(bool bCommit)
{
	check(BeforeGameplay.IsSet());
	FBeforeGameplay& Previous = BeforeGameplay.GetValue();
	if (bCommit)
	{
		for (USceneComponent* Component : Previous.Components) { Component->DestroyComponent(); }
		AppliedGameplayKey = MoveTemp(Previous.PendingKey);
	}
	else
	{
		DestroyDynamicComponents();
		for (const auto& Pair : Previous.ChangedPrimitives) { Pair.Key->SetCollisionEnabled(Pair.Value); }
		DynamicComponents = MoveTemp(Previous.Components);
		ActiveLadders = MoveTemp(Previous.Ladders);
		SuppressedCollision = MoveTemp(Previous.Suppressed);
	}
	BeforeGameplay.Reset();
}

void UCastleBuildingPCGComponent::ApplyVisual(const ACastleBoard& Board, const FCastlePCGVisualPlan& Plan, const FVector Origin,
	bool bGameplayChanged)
{
	ACastleBuildingBase* Building = CastChecked<ACastleBuildingBase>(GetOwner());
	UBuildingMergeComponent& Merge = *Building->BuildingMerge;
	FString Error;
	AActor* Snapshot = Merge.CreateVisualSnapshot(Error);
	// 快照捕获发生在 Reset 前，以保留尚在飞行的旧件。此后始终写入本轮正式视觉状态。
	Merge.ResetMerge();
	for (USceneComponent* Component : DynamicVisuals) { Component->DestroyComponent(); }
	DynamicVisuals.Reset();
	for (const FCastlePCGPartAnnotation& Part : OriginalParts)
	{
		const bool bHidden = Plan.Hidden.Contains(Part.Component);
		Part.Component->SetVisibility(Part.bOriginalVisible && !bHidden, false);
		if (const ECollisionEnabled::Type* Collision = OriginalVisualCollision.Find(Part.Component.Get()))
		{
			Part.Component->SetCollisionEnabled(bHidden ? ECollisionEnabled::NoCollision : *Collision);
		}
	}
	for (const auto& Visual : Plan.Visuals)
	{
		auto* Mesh = NewObject<UStaticMeshComponent>(Building, NAME_None, RF_Transient);
		Mesh->SetupAttachment(Building->GetRootComponent());
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(Visual.bSelectable ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->ComponentTags.Add(TEXT("CastlePCGDynamic"));
		if (!Visual.Node.IsNone()) { Mesh->ComponentTags.Add(FName(TEXT("CastlePCGNode=") + Visual.Node.ToString())); }
		if (!Visual.Group.IsNone()) { Mesh->ComponentTags.Add(FName(TEXT("CastlePCGGroup=") + Visual.Group.ToString())); }
		Mesh->SetStaticMesh(Visual.Mesh);
		for (int32 M = 0; M < Visual.Materials.Num(); ++M) { Mesh->SetMaterial(M, Visual.Materials[M]); }
		Mesh->SetRelativeTransform(Visual.Relative);
		Building->AddInstanceComponent(Mesh);
		Mesh->RegisterComponent();
		DynamicVisuals.Add(Mesh);
	}
	AppliedVisualKey = Plan.Key;
	if (Snapshot)
	{
		Merge.PartCatalog = Building->GetBuildingDefinition()->PartCatalog;
		Merge.IncomingSourceActor = nullptr;
		Merge.bUseSharedChangeOrigin = true;
		Merge.ChangeOrigin = Origin;
		Merge.ArrivalDirection = Board.GetActorForwardVector();
		// 纯视觉过渡不请求导航重建。随玩法切换的过渡继续沿用合并的导航生命周期。
		const bool bStarted = bGameplayChanged ? Merge.StartChange({Snapshot}) : Merge.StartVisualChange({Snapshot});
		Snapshot->Destroy();
		if (bStarted) { return; }
		Error = Merge.LastError;
	}
	// D6：表现过渡失败仍保留当前终态并推进签名，避免下一轮重复等待同一处故障。
	UE_LOG(LogMoving_Castle, Error, TEXT("%s 的建筑 PCG 视觉过渡失败：%s"), *Building->GetPathName(), *Error);
}

void UCastleBuildingPCGComponent::UpdateAppliedKey()
{
	AppliedKey = AppliedGameplayKey + TEXT("|") + AppliedVisualKey;
}

void UCastleBuildingPCGComponent::GetLadders(TArray<FCastleActiveLadderConnection>& Out) const
{
	Out.Append(ActiveLadders);
}

void UCastleBuildingPCGComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	DestroyDynamicComponents();
	for (USceneComponent* Component : DynamicVisuals) { Component->DestroyComponent(); }
	DynamicVisuals.Reset();
	Super::EndPlay(Reason);
}
