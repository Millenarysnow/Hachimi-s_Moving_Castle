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
	FCastlePCGApplyPlan& Plan, FString& Error) const
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
		if (Ceiling.Value == 2)
		{
			for (const auto& Template : LV2) { Plan.Visuals.Add(CastlePCGComponents::PlaceTemplate(Template, Frame, Building)); }
			Plan.LadderFrames.Add({Ceiling.Key, Frame.GetRelativeTransform(Building->GetActorTransform())});
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
	Building->BuildingMerge->ResetMerge();
	DestroyDynamicComponents();
	for (const auto& Part : OriginalParts) { Part.Component->SetVisibility(Part.bOriginalVisible && !Plan.Hidden.Contains(Part.Component), false); }
	for (const auto& Visual : Plan.Visuals)
	{
		auto* Mesh = NewObject<UStaticMeshComponent>(Building, NAME_None, RF_Transient);
		Mesh->SetupAttachment(Building->GetRootComponent());
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->ComponentTags.Add(TEXT("CastlePCGDynamic"));
		Mesh->SetStaticMesh(Visual.Mesh);
		for (int32 M = 0; M < Visual.Materials.Num(); ++M) { Mesh->SetMaterial(M, Visual.Materials[M]); }
		Mesh->SetRelativeTransform(Visual.Relative);
		Building->AddInstanceComponent(Mesh);
		Mesh->RegisterComponent();
		DynamicComponents.Add(Mesh);
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
		Link->SetRelativeTransform(Frame.Value);
		// 真实 Mesh2 在 X[-66.63,-50],Y[100,205]；由负Y侧接近梯面，上层接入板内。
		Link->BottomStandOffset = FVector(-58.317, 40.0, 0.0);
		Link->LadderBottomOffset = FVector(-58.317, 150.0, 0.0);
		Link->LadderTopOffset = FVector(-58.317, 150.0, Board->LayerHeight);
		Link->TopStandOffset = FVector(40.0, 40.0, Board->LayerHeight);
		Building->AddInstanceComponent(Link);
		Link->RegisterComponent();
		DynamicComponents.Add(Link);
		if (!Link->GetId().IsValid())
		{ Error = FString::Printf(TEXT("%s 的动态梯子未获得有效导航ID。"), *Building->GetPathName()); Snapshot->Destroy(); return false; }
		FCastleActiveLadderConnection& Connection = ActiveLadders.AddDefaulted_GetRef();
		Connection.Building = Building;
		Connection.LowerCell = Frame.Key;
		Connection.UpperCell = Frame.Key + FIntVector(0, 0, 1);
		Connection.BottomWorldLocation = Link->GetComponentTransform().TransformPosition(Link->LadderBottomOffset);
		Connection.TopWorldLocation = Link->GetComponentTransform().TransformPosition(Link->LadderTopOffset);
		Connection.WorldFacing = (Link->GetComponentTransform().TransformVectorNoScale(FVector::RightVector)).Rotation();
	}
	Building->BuildingMerge->PartCatalog = Building->GetBuildingDefinition()->PartCatalog;
	Building->BuildingMerge->IncomingSourceActor = nullptr;
	Building->BuildingMerge->bUseSharedChangeOrigin = true;
	Building->BuildingMerge->ChangeOrigin = Origin;
	Building->BuildingMerge->ArrivalDirection = Board->GetActorForwardVector();
	const bool bStarted = Building->BuildingMerge->StartChange({Snapshot});
	Snapshot->Destroy();
	if (!bStarted) { Error = Building->BuildingMerge->LastError; return false; }
	AppliedKey = Plan.Key;
	return true;
}

void UCastleBuildingPCGComponent::GetLadders(TArray<FCastleActiveLadderConnection>& Out) const
{
	Out.Append(ActiveLadders);
}

void UCastleBuildingPCGComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	DestroyDynamicComponents();
	Super::EndPlay(Reason);
}
