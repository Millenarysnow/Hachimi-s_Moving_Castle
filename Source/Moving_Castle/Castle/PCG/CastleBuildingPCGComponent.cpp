#include "CastleBuildingPCGComponent.h"
#include "CastlePCGSemantics.h"
#include "HachimisV4/CastleHachimisV4PCG.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "BuildingMergeComponent.h"
#include "CastleLadderLinkComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Engine/StaticMesh.h"
#include "NavigationSystem.h"
#include "NavigationData.h"

namespace CastlePCGComponents
{
	/** 格中心底面坐标始终经棋盘变换，支持旋转棋盘和沿 Y 合成的建筑。 */
	FTransform CellFrame(const ACastleBoard* Board, FIntVector Cell)
	{
		FVector P = Board->GetActorTransform().InverseTransformPosition(Board->GetGridWorldLocation(Cell));
		P.Z -= Board->LayerHeight * 0.5;
		return FTransform(FQuat::Identity, P) * Board->GetActorTransform();
	}
	/** 将网格模板放入建筑局部坐标，动态件不继承模板 Actor 的运行逻辑。 */
	FCastlePCGMeshTemplate PlaceTemplate(const FCastlePCGMeshTemplate& Template, const FTransform& Frame, const AActor* Building)
	{
		FCastlePCGMeshTemplate Result = Template;
		Result.Relative = (Template.Relative * Frame).GetRelativeTransform(Building->GetActorTransform());
		return Result;
	}
}

bool UCastleBuildingPCGComponent::CacheOriginalParts(const ACastleBoard* Board, FString& Error)
{
	if (bCached) { return true; }
	if (!CastlePCGSemantics::Extract(Cast<ACastleBuildingBase>(GetOwner()), Board, OriginalParts, Error)) { return false; }
	bCached = true;
	return true;
}

bool UCastleBuildingPCGComponent::Prepare(const ACastleBoard* Board, const UCastlePCGRuleSetDataAsset* Rules,
	const FCastlePCGTarget& Target, const TArray<FCastlePCGMeshTemplate>& LV1, const TArray<FCastlePCGMeshTemplate>& LV2,
	const CastleV4PCG::FStairs& Stairs, FCastlePCGApplyPlan& Plan, FString& Error) const
{
	ACastleBuildingBase* Building = CastChecked<ACastleBuildingBase>(GetOwner());
	Plan = FCastlePCGApplyPlan();
	Plan.Key = Target.StableKey();
	FIntVector Minimum = Building->GetOccupiedCoordinates()[0];
	for (const auto& Cell : Building->GetOccupiedCoordinates()) { Minimum = Minimum.ComponentMin(Cell); }
	const FTransform BoardInverse = Board->GetActorTransform().Inverse();
	// 这里的功能盒明确表达桥、封闭楼板或阻隔，不是缺少资产时使用的替代美术。
	auto AddBox = [&](FVector Center, FVector Extent, FQuat Rotation = FQuat::Identity)
	{
		const FTransform World = FTransform(Rotation, Center) * Board->GetActorTransform();
		Plan.FunctionalBoxes.Add({World.GetRelativeTransform(Building->GetActorTransform()), Extent});
	};
	for (const auto& Ceiling : Target.Ceilings)
	{
		bool bHasSlab = false;
		for (const auto& Part : OriginalParts)
		{
			if (!Part.CoveredCellOffsets.Contains(Ceiling.Key - Minimum)) { continue; }
			if (Part.Role == ECastlePCGPartRole::Roof) { Plan.Hidden.Add(Part.Component); }
			if (Part.Role == ECastlePCGPartRole::TopCeilingSlab)
			{
				bHasSlab |= Part.bOriginalVisible;
				if (Ceiling.Value == 2) { Plan.Hidden.Add(Part.Component); }
			}
		}
		const FTransform Frame = CastlePCGComponents::CellFrame(Board, Ceiling.Key);
		if (const CastleV4PCG::FStairInterface* Mixed = Stairs.Find(Ceiling.Key))
		{
			// 旧盾塔在下时继续装旧模板，V4 上层按其同一个缺角切换原底盘功能板。
			// 上层已生成切口四边功能面，旧下层不能再铺一整块封死梯井。
			for (const FCastlePCGMeshTemplate& Template : LV2)
			{
				FCastlePCGMeshTemplate Part = CastlePCGComponents::PlaceTemplate(Template, Mixed->WorldFrame, Building);
				Part.bSelectable = true;
				Plan.Visuals.Add(MoveTemp(Part));
			}
			CastleV4PCG::AddLink(Plan, Ceiling.Key, Mixed->WorldFrame.GetRelativeTransform(Building->GetActorTransform()), Mixed->Hatch);
			continue;
		}
		if (Ceiling.Value == 2)
		{
			for (const auto& Template : LV2) { Plan.Visuals.Add(CastlePCGComponents::PlaceTemplate(Template, Frame, Building)); }
			// 旧系列使用已经验收的 LV2 动作点，V4 使用独立测得的作者点。
			Plan.LadderFrames.Add({Ceiling.Key, Frame.GetRelativeTransform(Building->GetActorTransform()),
				FVector(-58.317, 40.0, 0.0), FVector(40.0, 40.0, Board->LayerHeight),
				FVector(-58.317, 150.0, 0.0), FVector(-58.317, 150.0, Board->LayerHeight)});
		}
		else if (!bHasSlab)
		{
			FCastlePCGMeshTemplate Slab;
			Slab.Mesh = Rules->FlatFloorSlabMesh;
			for (const auto& Material : Slab.Mesh->GetStaticMaterials()) { Slab.Materials.Add(Material.MaterialInterface); }
			Plan.Visuals.Add(CastlePCGComponents::PlaceTemplate(Slab, Frame, Building));
		}
		// 上层正式建筑本身带功能地板；封闭顶面补齐与变体同高的下层所属功能面。
		FVector Top = BoardInverse.TransformPosition(Frame.GetLocation());
		Top.Z += Board->LayerHeight - Rules->FloorThickness * 0.5;
		AddBox(Top, FVector(205.0, 205.0, Rules->FloorThickness * 0.5));
	}
	for (const FIntVector& Cell : Building->GetOccupiedCoordinates())
	{
		// V4 在下时，沿用盾塔自己的板面高度和原碰撞身份，只切换重合格的板。
		if (const CastleV4PCG::FStairInterface* Mixed = Stairs.Find(Cell - FIntVector(0, 0, 1)))
		{
			if (!CastleV4PCG::CutDeck(*Board, *Building, Cell, FIntVector::ZeroValue, *Mixed, Plan, Error)) { return false; }
		}
	}
	for (const auto& Part : OriginalParts)
	{
		if (Part.Role != ECastlePCGPartRole::PerimeterRailing || !Part.bOriginalVisible) { continue; }
		int32 ConnectedCount = 0;
		for (const auto& Offset : Part.CoveredCellOffsets)
		{
			ConnectedCount += Target.Faces.ContainsByPredicate([&](const FCastlePCGFaceTarget& Face)
			{ return Face.bConnected && Face.Cell == Minimum + Offset && Face.Face == Part.BoardFacing; }) ? 1 : 0;
		}
		if (ConnectedCount == 0) { continue; }
		Plan.Hidden.Add(Part.Component);
		UStaticMeshComponent* Original = CastChecked<UStaticMeshComponent>(Part.Component);
		const FBox Box = Original->GetStaticMesh()->GetBoundingBox().TransformBy((Original->GetComponentTransform() * BoardInverse).ToMatrixWithScale());
		const bool bThinX = Box.GetSize().X < Box.GetSize().Y;
		TArray<FIntVector> Offsets = Part.CoveredCellOffsets;
		Offsets.Sort(CastlePCGSemantics::CellLess);
		const double SegmentLength = (bThinX ? Box.GetSize().Y : Box.GetSize().X) / Offsets.Num();
		for (int32 Index = 0; Index < Offsets.Num(); ++Index)
		{
			const bool bConnected = Target.Faces.ContainsByPredicate([&](const FCastlePCGFaceTarget& Face)
			{ return Face.bConnected && Face.Cell == Minimum + Offsets[Index] && Face.Face == Part.BoardFacing; });
			FCastlePCGMeshTemplate Template;
			if (bConnected) { Template = LV1[0]; }
			else
			{
				// 指南 D4 指定复用同蓝图的单格护栏；找不到必须报错，不能保留双格或借用其他建筑。
				const FCastlePCGPartAnnotation* Reference = OriginalParts.FindByPredicate([&](const FCastlePCGPartAnnotation& Candidate)
				{
					if (Candidate.Role != ECastlePCGPartRole::PerimeterRailing || Candidate.CoveredCellOffsets.Num() != 1 || !Candidate.bOriginalVisible) { return false; }
					const auto* ReferenceComponent = CastChecked<UStaticMeshComponent>(Candidate.Component);
					const FVector ReferenceSize = ReferenceComponent->GetStaticMesh()->GetBoundingBox().TransformBy(
						(ReferenceComponent->GetComponentTransform() * BoardInverse).ToMatrixWithScale()).GetSize();
					return FMath::IsNearlyEqual(FMath::Max(ReferenceSize.X, ReferenceSize.Y), SegmentLength, 1.0)
						&& FMath::IsNearlyEqual(ReferenceSize.Z, Box.GetSize().Z, 1.0);
				});
				if (!Reference) { Error = FString::Printf(TEXT("%s 跨格护栏 %s 缺少本蓝图单格参考护栏。"), *Building->GetPathName(), *Original->GetName()); return false; }
				UStaticMeshComponent* ReferenceMesh = CastChecked<UStaticMeshComponent>(Reference->Component);
				Template.Mesh = ReferenceMesh->GetStaticMesh();
				for (int32 M = 0; M < ReferenceMesh->GetNumMaterials(); ++M) { Template.Materials.Add(ReferenceMesh->GetMaterial(M)); }
			}
			const FBox MeshBox = Template.Mesh->GetBoundingBox();
			const bool bMeshAlongX = MeshBox.GetSize().X > MeshBox.GetSize().Y;
			const double Length = bMeshAlongX ? MeshBox.GetSize().X : MeshBox.GetSize().Y;
			const FQuat Rotation(FVector::UpVector, (bThinX == bMeshAlongX) ? HALF_PI : 0.0);
			FVector Scale = FVector::OneVector;
			if (bMeshAlongX) { Scale.X = SegmentLength / Length; } else { Scale.Y = SegmentLength / Length; }
			FVector Center = Box.GetCenter();
			if (bThinX) { Center.Y = Box.Min.Y + SegmentLength * (Index + 0.5); }
			else { Center.X = Box.Min.X + SegmentLength * (Index + 0.5); }
			const FTransform Railing(Rotation, Center - Rotation.RotateVector(Scale * MeshBox.GetCenter()), Scale);
			Template.Relative = (Railing * Board->GetActorTransform()).GetRelativeTransform(Building->GetActorTransform());
			Plan.Visuals.Add(Template);
		}
	}
	for (const auto& Face : Target.Faces)
	{
		const FIntVector Offset = CastlePCGSemantics::FaceOffset(Face.Face);
		FVector Center = BoardInverse.TransformPosition(Board->GetGridWorldLocation(Face.Cell));
		Center.Z -= Board->LayerHeight * 0.5;
		Center += FVector(Offset) * (Board->CellSize * 0.5);
		const double HalfLength = (Board->CellSize - Board->GroundTileSize) * 0.5 + 40.0;
		const FQuat Rotation(FVector::UpVector, Offset.Y != 0 ? HALF_PI : 0.0);
		if (Face.bConnected)
		{
			Center.Z -= Rules->FloorThickness * 0.5;
			AddBox(Center, FVector(HalfLength, Rules->BridgeWidth * 0.5, Rules->FloorThickness * 0.5), Rotation);
		}
		else
		{
			Center.Z += Rules->BarrierHeight * 0.5;
			AddBox(Center, FVector(10.0, Board->GroundTileSize * 0.5, Rules->BarrierHeight * 0.5), Rotation);
		}
	}
	if (!Plan.LadderFrames.IsEmpty())
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		ANavigationData* Data = Navigation ? Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
		if (!Data || Data->GetRuntimeGenerationMode() != ERuntimeGenerationType::Dynamic)
		{
			Error = FString::Printf(TEXT("%s 的PCG梯子需要有效的动态Recast导航数据，拒绝提交替换。"), *Building->GetPathName());
			return false;
		}
	}
	CastleV4PCG::FinalizeKey(Target, Plan);
	return true;
}

void UCastleBuildingPCGComponent::DestroyDynamicComponents()
{
	for (USceneComponent* Component : DynamicComponents) { if (IsValid(Component)) { Component->DestroyComponent(); } }
	DynamicComponents.Reset();
	ActiveLadders.Reset();
}

bool UCastleBuildingPCGComponent::Apply(const ACastleBoard* Board, const FCastlePCGApplyPlan& Plan, AActor* Snapshot,
	const FVector Origin, FString& Error)
{
	ACastleBuildingBase* Building = CastChecked<ACastleBuildingBase>(GetOwner());
	// 必须先结束旧动画，再写新目标；ResetMerge 会恢复旧目标显隐，不可在写目标之后调用。
	check(!BeforeApply.IsSet());
	FBeforeApply& Previous = BeforeApply.Emplace();
	Previous.bVisualChange = NeedsVisualChange(Plan);
	Previous.VisualKey = AppliedVisualKey;
	if (Previous.bVisualChange)
	{
		check(Snapshot);
		Building->BuildingMerge->ResetMerge();
		Previous.Visuals = MoveTemp(DynamicVisuals);
	}
	Previous.Components = MoveTemp(DynamicComponents);
	Previous.Ladders = MoveTemp(ActiveLadders);
	Previous.Suppressed = MoveTemp(SuppressedCollision);
	Previous.Key = AppliedKey;
	auto Remember = [&](UPrimitiveComponent* Primitive)
	{
		Previous.ChangedPrimitives.FindOrAdd(Primitive, {Primitive->IsVisible(), Primitive->GetCollisionEnabled()});
	};
	const auto DisablePrevious = [&](const TArray<TObjectPtr<USceneComponent>>& Components)
	{
		for (USceneComponent* Component : Components)
		{
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			{
				Remember(Primitive);
				Primitive->SetVisibility(false, false);
				Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	};
	DisablePrevious(Previous.Components);
	DisablePrevious(Previous.Visuals);
	for (const auto& Pair : Previous.Suppressed)
	{
		if (UPrimitiveComponent* Primitive = Pair.Key.Get())
		{
			Remember(Primitive);
			Primitive->SetCollisionEnabled(Pair.Value);
		}
	}
	auto Suppress = [&](UPrimitiveComponent* Primitive)
	{
		Remember(Primitive);
		SuppressedCollision.Add(Primitive, Primitive->GetCollisionEnabled());
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	};
	for (const auto& Part : OriginalParts)
	{
		const bool bHidden = Plan.Hidden.Contains(Part.Component);
		Remember(Part.Component);
		if (Previous.bVisualChange) { Part.Component->SetVisibility(Part.bOriginalVisible && !bHidden, false); }
		if (bHidden && Building->HachimisLayout) { Suppress(Part.Component); }
	}
	for (UPrimitiveComponent* Floor : Plan.DisabledFloors) { Suppress(Floor); }
	if (Previous.bVisualChange)
	{
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
			Mesh->SetStaticMesh(Visual.Mesh);
			for (int32 M = 0; M < Visual.Materials.Num(); ++M) { Mesh->SetMaterial(M, Visual.Materials[M]); }
			Mesh->SetRelativeTransform(Visual.Relative);
			Building->AddInstanceComponent(Mesh);
			Mesh->RegisterComponent();
			DynamicVisuals.Add(Mesh);
		}
	}
	for (const auto& Box : Plan.FunctionalBoxes)
	{
		auto* Collision = NewObject<UBoxComponent>(Building, NAME_None, RF_Transient);
		Collision->SetupAttachment(Building->GameplayRoot);
		Collision->SetMobility(EComponentMobility::Movable);
		Collision->SetBoxExtent(Box.Value);
		Collision->SetRelativeTransform(Box.Key);
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
		{ Error = FString::Printf(TEXT("%s 的动态梯子未获得有效导航ID。"), *Building->GetPathName()); if (Snapshot) { Snapshot->Destroy(); } return false; }
		FCastleActiveLadderConnection& Connection = ActiveLadders.AddDefaulted_GetRef();
		Connection.Building = Building;
		Connection.LowerCell = Frame.LowerCell;
		Connection.UpperCell = Frame.LowerCell + FIntVector(0, 0, 1);
		Connection.BottomWorldLocation = Link->GetComponentTransform().TransformPosition(Link->LadderBottomOffset);
		Connection.TopWorldLocation = Link->GetComponentTransform().TransformPosition(Link->LadderTopOffset);
		Connection.WorldFacing = (Link->GetComponentTransform().TransformVectorNoScale((Frame.LadderBottom - Frame.BottomStand).GetSafeNormal2D())).Rotation();
	}
	if (Previous.bVisualChange)
	{
		Building->BuildingMerge->PartCatalog = Building->GetBuildingDefinition()->PartCatalog;
		Building->BuildingMerge->IncomingSourceActor = nullptr;
		Building->BuildingMerge->bUseSharedChangeOrigin = true;
		Building->BuildingMerge->ChangeOrigin = Origin;
		Building->BuildingMerge->ArrivalDirection = Board->GetActorForwardVector();
		const bool bStarted = Building->BuildingMerge->StartChange({Snapshot});
		Snapshot->Destroy();
		if (!bStarted) { Error = Building->BuildingMerge->LastError; return false; }
	}
	AppliedKey = Plan.Key;
	AppliedVisualKey = Plan.VisualKey;
	return true;
}

void UCastleBuildingPCGComponent::FinishApply(bool bCommit)
{
	check(BeforeApply.IsSet());
	FBeforeApply& Previous = BeforeApply.GetValue();
	if (bCommit)
	{
		// 新目标成功接管后才销毁退场组件，释放上一份 NavLink。
		for (USceneComponent* Component : Previous.Components) { Component->DestroyComponent(); }
		for (USceneComponent* Component : Previous.Visuals) { Component->DestroyComponent(); }
	}
	else
	{
		if (Previous.bVisualChange)
		{
			CastChecked<ACastleBuildingBase>(GetOwner())->BuildingMerge->ResetMerge();
			for (USceneComponent* Component : DynamicVisuals) { Component->DestroyComponent(); }
			DynamicVisuals = MoveTemp(Previous.Visuals);
		}
		DestroyDynamicComponents();
		for (const auto& Pair : Previous.ChangedPrimitives)
		{
			Pair.Key->SetVisibility(Pair.Value.Key, false);
			Pair.Key->SetCollisionEnabled(Pair.Value.Value);
		}
		DynamicComponents = MoveTemp(Previous.Components);
		ActiveLadders = MoveTemp(Previous.Ladders);
		SuppressedCollision = MoveTemp(Previous.Suppressed);
		AppliedKey = MoveTemp(Previous.Key);
		AppliedVisualKey = MoveTemp(Previous.VisualKey);
	}
	BeforeApply.Reset();
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
