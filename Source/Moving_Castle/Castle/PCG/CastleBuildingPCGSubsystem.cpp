#include "CastleBuildingPCGSubsystem.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingPCGRule.h"
#include "CastlePCGSemantics.h"
#include "CastleDeterministicPCGNoise.h"
#include "BuildingMergeComponent.h"
#include "CastleNavigationSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Moving_Castle.h"

namespace CastlePCGSubsystem
{
	/** 实例化模板仅用于读取蓝图构造后的静态网格；不运行 BeginPlay，不保存、不留在世界中。 */
	bool ReadVariant(UWorld* World, UClass* Class, const int32 ExpectedCount, TArray<FCastlePCGMeshTemplate>& Out, FString& Error)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.bDeferConstruction = true;
		AActor* Template = World->SpawnActor<AActor>(Class, FTransform::Identity, Params);
		if (!Template) { Error = FString::Printf(TEXT("PCG 无法生成变体模板 %s。"), *GetNameSafe(Class)); return false; }
		// SCS 注册前就关闭整 Actor 碰撞，防止只读模板短暂成为导航几何。
		Template->SetActorEnableCollision(false);
		Template->FinishSpawning(FTransform::Identity, true);
		TArray<UStaticMeshComponent*> Meshes;
		Template->GetComponents(Meshes);
		for (auto* Mesh : Meshes)
		{
			if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Mesh)) { continue; }
			if (!Mesh->GetStaticMesh()) { Error = FString::Printf(TEXT("PCG 变体 %s.%s 缺少静态网格。"), *Class->GetPathName(), *Mesh->GetName()); Template->Destroy(); return false; }
			FCastlePCGMeshTemplate& Part = Out.AddDefaulted_GetRef();
			Part.Mesh = Mesh->GetStaticMesh();
			Part.Relative = Mesh->GetComponentTransform().GetRelativeTransform(Template->GetActorTransform());
			for (int32 M = 0; M < Mesh->GetNumMaterials(); ++M) { Part.Materials.Add(Mesh->GetMaterial(M)); }
		}
		Template->Destroy();
		if (Out.Num() != ExpectedCount) { Error = FString::Printf(TEXT("PCG 变体 %s 应有 %d 个网格，实际 %d。"), *Class->GetPathName(), ExpectedCount, Out.Num()); return false; }
		int32 Slabs = 0, Ladders = 0, Railings = 0;
		for (const auto& Part : Out)
		{
			const FBox Box = Part.Mesh->GetBoundingBox().TransformBy(Part.Relative.ToMatrixWithScale());
			const FVector Size = Box.GetSize();
			Slabs += FMath::IsNearlyEqual(Box.Max.Z, 560.0, 1.0) && FMath::IsNearlyEqual(Size.Z, 31.55, 1.0)
				&& FMath::IsNearlyEqual(Size.X, 410.0, 1.0) && FMath::IsNearlyEqual(Size.Y, 410.0, 1.0);
			Ladders += FMath::IsNearlyZero(Box.Min.Z, 1.0) && FMath::IsNearlyEqual(Box.Max.Z, 528.45, 1.0)
				&& Size.X < 50.0 && Size.Y > 70.0 && Size.Y < 150.0;
			Railings += FMath::IsNearlyEqual(Size.Z, 162.503, 1.0) && Size.Y < 10.0
				&& FMath::IsNearlyEqual(Size.X, 530.0, 1.0);
		}
		if ((ExpectedCount == 1 && Railings != 1) || (ExpectedCount == 2 && (Slabs != 1 || Ladders != 1)))
		{
			Error = FString::Printf(TEXT("PCG变体 %s 几何语义无效：护栏=%d，顶板=%d，竖梯=%d。"), *Class->GetPathName(), Railings, Slabs, Ladders);
			return false;
		}
		return true;
	}
}

bool UCastleBuildingPCGSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCastleBuildingPCGSubsystem::ObserveBoard(ACastleBoard* Board)
{
	if (ObservedBoards.Contains(Board)) { return; }
	ObservedBoards.Add(Board);
	Board->OnLayoutCommitted.AddUObject(this, &UCastleBuildingPCGSubsystem::HandleLayoutCommitted);
}

void UCastleBuildingPCGSubsystem::HandleLayoutCommitted(ACastleBoard* Board, const FVector Origin)
{
	FString Error;
	if (!RecomputeInternal(Board, Origin, Error, true)) { UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 建筑PCG拒绝本轮更新：%s"), *GetPathNameSafe(Board), *Error); }
}

bool UCastleBuildingPCGSubsystem::Recompute(ACastleBoard* Board, const FVector Origin, FString& Error)
{
	return RecomputeInternal(Board, Origin, Error, false);
}

bool UCastleBuildingPCGSubsystem::RecomputeInternal(ACastleBoard* Board, const FVector Origin, FString& Error, const bool bAffectedOnly)
{
	if (!Board || !Board->PCGRuleSet) { Error = FString::Printf(TEXT("棋盘 %s 未配置 PCGRuleSet。"), *GetPathNameSafe(Board)); return false; }
	if (!Board->PCGRuleSet->ValidateConfiguration(Error)) { return false; }
	UCastlePCGRuleSetDataAsset* Rules = Board->PCGRuleSet;
	TArray<FCastlePCGMeshTemplate> LV1, LV2;
	if (!CastlePCGSubsystem::ReadVariant(GetWorld(), Rules->VariantLV1ActorClass, 1, LV1, Error)
		|| !CastlePCGSubsystem::ReadVariant(GetWorld(), Rules->VariantLV2ActorClass, 2, LV2, Error)) { return false; }
	TMap<FIntVector, ACastleBuildingBase*> Occupancy;
	TArray<ACastleBuildingBase*> Buildings;
	for (auto* Building : Board->GetPlacedBuildings())
	{
		// 不完整飞入不参与语义场，这是明确的业务谓词，不能读取动画中的半装配模型。
		if (!Building->bHasCompletedPlacement) { continue; }
		Buildings.Add(Building);
		for (const auto& Cell : Building->GetOccupiedCoordinates()) { Occupancy.Add(Cell, Building); }
	}
	TArray<TPair<UCastleBuildingPCGComponent*, FCastlePCGApplyPlan>> Changes;
	UCastleDeterministicPCGNoise* Noise = NewObject<UCastleDeterministicPCGNoise>(this);
	const FIntVector Directions[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
	TMap<FIntVector, TWeakObjectPtr<ACastleBuildingBase>> CurrentOccupancy;
	for (const auto& Pair : Occupancy) { CurrentOccupancy.Add(Pair.Key, Pair.Value); }
	TSet<ACastleBuildingBase*> Affected;
	const auto* Previous = PreviousOccupancy.Find(Board);
	auto MarkCellAndNeighbors = [&](const FIntVector& Cell)
	{
		if (auto* const* Building = Occupancy.Find(Cell)) { Affected.Add(*Building); }
		for (const auto& Direction : Directions)
		{
			if (auto* const* Building = Occupancy.Find(Cell + Direction)) { Affected.Add(*Building); }
		}
	};
	for (const auto& Pair : CurrentOccupancy)
	{
		const auto* Old = Previous ? Previous->Find(Pair.Key) : nullptr;
		if (!Old || *Old != Pair.Value) { MarkCellAndNeighbors(Pair.Key); }
	}
	if (Previous)
	{
		for (const auto& Pair : *Previous) { if (!CurrentOccupancy.Contains(Pair.Key)) { MarkCellAndNeighbors(Pair.Key); } }
	}
	for (auto* Building : Buildings)
	{
		if (bAffectedOnly && !Affected.Contains(Building)) { continue; }
		if (!Building->BuildingPCG->CacheOriginalParts(Board, Error)) { return false; }
		FCastlePCGRuleContext Context;
		Context.Building = Building;
		Context.Seed = Board->GroundSeed;
		Context.NoiseProvider = Noise;
		Context.bUseLadderNoise = Rules->bUseNoiseForLadderCellSelection;
		Context.LadderChannel = Rules->LadderSelectionNoiseChannel;
		for (const auto& Cell : Building->GetOccupiedCoordinates())
		{
			for (const auto& Direction : Directions)
			{
				ACastleBuildingBase* const* Neighbor = Occupancy.Find(Cell + Direction);
				if (!Neighbor || *Neighbor == Building) { continue; }
				FCastlePCGCellAdjacency Adj;
				Adj.FirstBuilding = Building;
				Adj.SecondBuilding = *Neighbor;
				Adj.FirstCell = Cell;
				Adj.SecondCell = Cell + Direction;
				Adj.Offset = Direction;
				Adj.bCanTraverse = Building->GetBuildingColor() == (*Neighbor)->GetBuildingColor()
					|| Building->GetBuildingNumber() == (*Neighbor)->GetBuildingNumber();
				Context.Adjacencies.Add(Adj);
			}
		}
		FCastlePCGTarget Target;
		for (UCastleBuildingPCGRule* Rule : Rules->Rules) { if (!Rule->Evaluate(Context, Target, Error)) { return false; } }
		if (Building->BuildingPCG->GetAppliedKey() == Target.StableKey()) { continue; }
		FCastlePCGApplyPlan Plan;
		if (!Building->BuildingPCG->Prepare(Board, Rules, Target, LV1, LV2, Plan, Error)) { return false; }
		Changes.Add({Building->BuildingPCG, MoveTemp(Plan)});
	}
	if (Changes.IsEmpty()) { PreviousOccupancy.Add(Board, MoveTemp(CurrentOccupancy)); return true; }
	// 所有资源和完整目标先验证，再捕获所有旧件。失败不允许留下半轮隐藏模型。
	TArray<AActor*> Snapshots;
	for (auto& Change : Changes)
	{
		auto* Building = CastChecked<ACastleBuildingBase>(Change.Key->GetOwner());
		AActor* Snapshot = Building->BuildingMerge->CreateVisualSnapshot(Error);
		if (!Snapshot) { for (auto* Captured : Snapshots) { Captured->Destroy(); } return false; }
		Snapshots.Add(Snapshot);
	}
	const FScopedCastleNavigationChange NavigationChange(GetWorld(), Board);
	for (int32 Index = 0; Index < Changes.Num(); ++Index)
	{
		if (!Changes[Index].Key->Apply(Board, Changes[Index].Value, Snapshots[Index], Origin, Error))
		{
			for (int32 Remaining = Index + 1; Remaining < Snapshots.Num(); ++Remaining) { Snapshots[Remaining]->Destroy(); }
			return false;
		}
	}
	OnPCGLayoutUpdated.Broadcast(Board);
	PreviousOccupancy.Add(Board, MoveTemp(CurrentOccupancy));
	return true;
}

void UCastleBuildingPCGSubsystem::GetActiveLadderConnections(const ACastleBoard* Board, TArray<FCastleActiveLadderConnection>& OutLadders) const
{
	OutLadders.Reset();
	if (!Board) { return; }
	for (auto* Building : Board->GetPlacedBuildings())
	{
		if (Building->bHasCompletedPlacement) { Building->BuildingPCG->GetLadders(OutLadders); }
	}
}

void UCastleBuildingPCGSubsystem::Deinitialize()
{
	for (const auto& Board : ObservedBoards) { if (Board.IsValid()) { Board->OnLayoutCommitted.RemoveAll(this); } }
	ObservedBoards.Reset();
	PreviousOccupancy.Reset();
	Super::Deinitialize();
}
