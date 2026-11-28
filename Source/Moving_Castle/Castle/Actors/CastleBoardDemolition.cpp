// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBoard.h"
#include "Audio/CastleAudioSubsystem.h"
#include "Algo/MinElement.h"
#include "BuildingMergeComponent.h"
#include "CastleBackpackComponent.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastleNavigationSubsystem.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Moving_Castle.h"

namespace
{
	/** 棋盘拆除的距离平局、拆分起点与外部邻居统一按 X、Y、Z 排序。 */
	bool IsDemolitionCellBefore(const FIntVector& Left, const FIntVector& Right)
	{
		if (Left.X != Right.X) { return Left.X < Right.X; }
		if (Left.Y != Right.Y) { return Left.Y < Right.Y; }
		return Left.Z < Right.Z;
	}

	/** 用最小占格为锚，生成完整矩形盒中的坐标，按字典序输出。 */
	TArray<FIntVector> BuildDemolitionShape(const FIntVector& Anchor, const FIntVector& Dimensions)
	{
		TArray<FIntVector> Cells;
		for (int32 X = 0; X < Dimensions.X; ++X)
		{
			for (int32 Y = 0; Y < Dimensions.Y; ++Y)
			{
				for (int32 Z = 0; Z < Dimensions.Z; ++Z)
				{
					Cells.Add(Anchor + FIntVector(X, Y, Z));
				}
			}
		}
		return Cells;
	}
}

bool ACastleBoard::TryResolveBuildingCell(const ACastleBuildingBase& Building, const FVector& WorldImpactPoint,
	FIntVector& OutCell) const
{
	FText Reason;
	const TArray<FIntVector> Cells = Building.GetOccupiedCoordinates();
	if (!ValidateBoardDimensions(Reason) || WorldImpactPoint.ContainsNaN() || Cells.IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 无法解析建筑 %s 的拆除命中：格尺寸、占格或命中点无效。%s"),
			*GetPathName(), *Building.GetPathName(), *Reason.ToString());
		return false;
	}

	// 完整反变换棋盘旋转与缩放，水平轴用格宽，竖直轴用层高，得到以格为单位的距离。
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldImpactPoint);
	const FVector GridPoint(Local.X / CellSize, Local.Y / CellSize, (Local.Z - WorldBaseHeight) / LayerHeight);
	double BestDistance = TNumericLimits<double>::Max();
	FIntVector BestCell = Cells[0];
	for (const FIntVector& Cell : Cells)
	{
		if (GetBuildingAt(Cell) != &Building)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 上建筑 %s 的占格 %s 已失去归属。"),
				*GetPathName(), *Building.GetPathName(), *Cell.ToString());
			return false;
		}
		const double Distance = FVector::DistSquared(GridPoint, FVector(Cell - GridOrigin));
		// 同一格界经过旋转和反变换可能有末位误差，归一化后仍按字典序选定平局。
		const bool bSameDistance = FMath::IsNearlyEqual(Distance, BestDistance, UE_DOUBLE_SMALL_NUMBER);
		if ((!bSameDistance && Distance < BestDistance) || (bSameDistance && IsDemolitionCellBefore(Cell, BestCell)))
		{
			BestDistance = Distance;
			BestCell = Cell;
		}
	}
	OutCell = BestCell;
	return true;
}

bool ACastleBoard::RemoveBuildingToBackpack(ACastleBuildingBase* Building, UCastleBackpackComponent* Backpack,
	FText& OutFailureReason)
{
	if (!IsValid(Building))
	{
		OutFailureReason = INVTEXT("待回收的建筑已失效。");
		UE_LOG(LogMoving_Castle, Warning, TEXT("棋盘 %s 拒绝回收失效建筑。"), *GetPathName());
		return false;
	}
	const TArray<FIntVector> Cells = Building->GetOccupiedCoordinates();
	if (Cells.Num() != 1)
	{
		OutFailureReason = INVTEXT("只能无坐标回收单格建筑；拆除多格建筑必须指定目标格。");
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 拒绝无坐标回收建筑 %s（占格 %d）：%s"),
			*GetPathName(), *Building->GetPathName(), Cells.Num(), *OutFailureReason.ToString());
		return false;
	}
	return DemolishBuildingCellToBackpack(Building, Cells[0], Backpack, OutFailureReason);
}

bool ACastleBoard::DemolishBuildingCellToBackpack(ACastleBuildingBase* Building, const FIntVector& TargetCell,
	UCastleBackpackComponent* Backpack, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (!IsValid(Backpack) || !GetWorld())
	{
		OutFailureReason = INVTEXT("拆除建筑需要有效棋盘世界与背包。");
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 无法拆除建筑 %s：%s"),
			*GetPathName(), *GetPathNameSafe(Building), *OutFailureReason.ToString());
		return false;
	}

	const auto* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
	if (Director && !Director->IsEditingAllowed(OutFailureReason))
	{
		UE_LOG(LogMoving_Castle, Warning, TEXT("棋盘 %s 当前阶段拒绝拆除建筑 %s：%s"),
			*GetPathName(), *GetPathNameSafe(Building), *OutFailureReason.ToString());
		return false;
	}
	if (!IsValid(Building) || !Building->CanReceiveBuildingTrigger() || GetBuildingAt(TargetCell) != Building)
	{
		OutFailureReason = INVTEXT("待拆除的建筑已失效、尚未落定或不再占据目标格。");
		UE_LOG(LogMoving_Castle, Warning, TEXT("棋盘 %s 拒绝拆除建筑 %s 的格 %s：%s"),
			*GetPathName(), *GetPathNameSafe(Building), *TargetCell.ToString(), *OutFailureReason.ToString());
		return false;
	}
	// 五级建筑也可能只有一个占格，不能仅凭格数将其作为一级卡回收。
	const int32 OriginalLevel = Building->GetBuildingLevel();
	if (OriginalLevel > 4)
	{
		OutFailureReason = INVTEXT("拆除只支持一级至四级建筑。");
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 拒绝拆除建筑 %s（等级 %d）：%s"),
			*GetPathName(), *Building->GetPathName(), OriginalLevel, *OutFailureReason.ToString());
		return false;
	}
	if (!Backpack->HasFreeSlots())
	{
		OutFailureReason = INVTEXT("建筑栏已满，无法回收拆除。");
		UE_LOG(LogMoving_Castle, Warning, TEXT("棋盘 %s 无法拆除建筑 %s 的格 %s：%s"),
			*GetPathName(), *Building->GetPathName(), *TargetCell.ToString(), *OutFailureReason.ToString());
		return false;
	}

	const FString BuildingPath = Building->GetPathName();
	const FVector ChangeOrigin = GetGridWorldLocation(TargetCell);
	FCastleBuildingDescriptor Recovered = Building->GetBuildingDescriptor();
	Recovered.Level = 1;
	TArray<FIntVector> Remaining = Building->GetOccupiedCoordinates();
	if (Remaining.Num() != ACastleBuildingBase::GetExpectedFootprintCellCount(OriginalLevel)
		|| Remaining.Remove(TargetCell) != 1 || !Recovered.Validate(OutFailureReason))
	{
		if (OutFailureReason.IsEmpty()) { OutFailureReason = INVTEXT("建筑占格与等级或目标格不一致。"); }
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 无法重组建筑 %s：%s"),
			*GetPathName(), *BuildingPath, *OutFailureReason.ToString());
		return false;
	}
	Remaining.Sort(IsDemolitionCellBefore);

	// 暂存、替换和效果注册均处在同一个导航变更范围内，回滚时也不发布未提交布局。
	const FScopedCastleNavigationChange NavigationChange(GetWorld(), this);
	struct FDemolitionReplacement
	{
		ACastleBuildingBase* Actor = nullptr;
		FIntVector MinimumCell;
		TArray<AActor*> VisualSources;
	};
	TArray<FDemolitionReplacement> Replacements;
	TArray<ACastleBuildingBase*> StagedActors;
	TArray<ACastleBuildingBase*> RegisteredActors;
	TArray<ACastleBuildingBase*> UnregisteredActors;
	TArray<ACastleBuildingBase*> RemovedActors = {Building};
	bool bCommitted = false;
	ON_SCOPE_EXIT
	{
		if (!bCommitted)
		{
			// 旧 Actor 在写入一级卡前始终存活。失败时先清理新索引，再恢复原 Actor 及触发注册。
			FText RollbackReason;
			for (ACastleBuildingBase* Actor : RegisteredActors)
			{
				if (!UnregisterBuilding(Actor, RollbackReason))
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("拆除回滚无法注销 %s：%s"), *Actor->GetPathName(), *RollbackReason.ToString());
				}
			}
			for (ACastleBuildingBase* Actor : UnregisteredActors)
			{
				if (!RegisterBuilding(Actor, RollbackReason))
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("拆除回滚无法恢复 %s：%s"), *Actor->GetPathName(), *RollbackReason.ToString());
				}
			}
			for (ACastleBuildingBase* Actor : StagedActors) { Actor->Destroy(); }
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 无法提交建筑 %s 的单格拆除：%s"),
				*GetPathName(), *BuildingPath, *OutFailureReason.ToString());
		}
	};
	const auto StageActor = [&](const int32 Level, const TArray<FIntVector>& Cells)
	{
		FCastleBuildingDescriptor Descriptor = Recovered;
		Descriptor.Level = Level;
		ACastleBuildingBase* Actor = SpawnBuildingActor(Descriptor, Cells, OutFailureReason);
		if (Actor)
		{
			Actor->SetPlacementComplete(true);
			StagedActors.Add(Actor);
		}
		return Actor;
	};

	// 先遍历所有优先形状，同形状内按升序尝试最小占格，然后才降级到下一种形状。
	for (int32 Level : {3, 2})
	{
		for (const FIntVector& Dimensions : GetMergedShapeDimensions(Level))
		{
			for (int32 Index = 0; Index < Remaining.Num();)
			{
				const FIntVector Anchor = Remaining[Index];
				const TArray<FIntVector> Shape = BuildDemolitionShape(Anchor, Dimensions);
				if (Shape.ContainsByPredicate([&](const FIntVector& Cell) { return !Remaining.Contains(Cell); }))
				{
					++Index;
					continue;
				}
				ACastleBuildingBase* Actor = StageActor(Level, Shape);
				if (!Actor) { return false; }
				Replacements.Add({Actor, Anchor, {Building}});
				for (const FIntVector& Cell : Shape) { Remaining.RemoveSingle(Cell); }
			}
		}
	}
	for (const FIntVector& Cell : Remaining)
	{
		ACastleBuildingBase* Actor = StageActor(1, {Cell});
		if (!Actor) { return false; }
		Replacements.Add({Actor, Cell, {Building}});
	}

	Replacements.Sort([](const FDemolitionReplacement& Left, const FDemolitionReplacement& Right)
	{
		return IsDemolitionCellBefore(Left.MinimumCell, Right.MinimumCell);
	});
	for (FDemolitionReplacement& Replacement : Replacements)
	{
		// 复用正常合成的身份和形状判定。只匹配外部已落定邻居，暂不改索引，也从未进入赠牌流程。
		while (Replacement.Actor->CanMergeToNextLevel())
		{
			TArray<ACastleBuildingBase*> Neighbors;
			for (const FIntVector& Cell : Replacement.Actor->GetOccupiedCoordinates())
			{
				TArray<FIntVector> NeighborCells;
				GetFaceNeighborCoordinates(Cell, NeighborCells);
				for (const FIntVector& NeighborCell : NeighborCells)
				{
					ACastleBuildingBase* Neighbor = GetBuildingAt(NeighborCell);
					if (Neighbor && !RemovedActors.Contains(Neighbor) && Neighbor->CanReceiveBuildingTrigger())
					{
						Neighbors.AddUnique(Neighbor);
					}
				}
			}
			Neighbors.Sort([](const ACastleBuildingBase& Left, const ACastleBuildingBase& Right)
			{
				const auto ByCell = IsDemolitionCellBefore;
				const TArray<FIntVector> LeftCells = Left.GetOccupiedCoordinates();
				const TArray<FIntVector> RightCells = Right.GetOccupiedCoordinates();
				return ByCell(*Algo::MinElement(LeftCells, ByCell), *Algo::MinElement(RightCells, ByCell));
			});
			ACastleBuildingBase* Partner = nullptr;
			TArray<FIntVector> Shape;
			for (ACastleBuildingBase* Neighbor : Neighbors)
			{
				if (BuildMergedFootprint(Replacement.Actor, Neighbor, Shape))
				{
					Partner = Neighbor;
					break;
				}
			}
			if (!Partner) { break; }
			ACastleBuildingBase* Merged = StageActor(Replacement.Actor->GetBuildingLevel() + 1, Shape);
			if (!Merged) { return false; }
			RemovedActors.Add(Partner);
			Replacement.VisualSources.Add(Partner);
			Replacement.Actor = Merged;
		}
	}

	// 此处所有生成已成功，原索引直到现在才让出，卡牌写入是提交前最后一步。
	for (ACastleBuildingBase* OldActor : RemovedActors)
	{
		if (!UnregisterBuilding(OldActor, OutFailureReason)) { return false; }
		UnregisteredActors.Add(OldActor);
	}
	for (const FDemolitionReplacement& Replacement : Replacements)
	{
		if (!RegisterBuilding(Replacement.Actor, OutFailureReason)) { return false; }
		RegisteredActors.Add(Replacement.Actor);
	}
	int32 AddedIndex = INDEX_NONE;
	if (!Backpack->TryAddItem(Recovered, AddedIndex, OutFailureReason)) { return false; }
	bCommitted = true;

	// 一个 StartChange 会立即隐藏它的源组件。先为后续结果拍下独立快照，保证每个结果都能读取拆除前的同一座建筑。
	TArray<AActor*> SharedSourceSnapshots;
	SharedSourceSnapshots.SetNumZeroed(Replacements.Num());
	if (GetWorld()->IsGameWorld())
	{
		for (int32 Index = 1; Index < Replacements.Num(); ++Index)
		{
			FString SnapshotError;
			SharedSourceSnapshots[Index] = Building->BuildingMerge->CreateVisualSnapshot(SnapshotError);
			if (!SharedSourceSnapshots[Index])
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("建筑 %s 无法为重组结果 %s 捕获拆除前快照：%s"),
					*BuildingPath, *Replacements[Index].Actor->GetPathName(), *SnapshotError);
			}
		}
	}
	for (int32 Index = 0; Index < Replacements.Num(); ++Index)
	{
		FDemolitionReplacement& Replacement = Replacements[Index];
		// 就地重组使用原建筑与参与的外部邻居做视觉快照，无场外飞入；表现错误保留已提交的占用。
		UBuildingMergeComponent* Transition = Replacement.Actor->BuildingMerge;
		Transition->PartCatalog = Recovered.Definition->PartCatalog;
		Transition->bUseSharedChangeOrigin = true;
		Transition->ChangeOrigin = ChangeOrigin;
		AActor* SharedSnapshot = SharedSourceSnapshots[Index];
		if (SharedSnapshot) { Replacement.VisualSources[0] = SharedSnapshot; }
		if (GetWorld()->IsGameWorld() && (Index == 0 || SharedSnapshot) && !Transition->StartChange(Replacement.VisualSources))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建筑 %s 无法播放拆除重组过渡：%s"),
				*Replacement.Actor->GetPathName(), *Transition->LastError);
		}
		if (SharedSnapshot) { SharedSnapshot->Destroy(); }
	}
	for (ACastleBuildingBase* OldActor : RemovedActors)
	{
		ForgetPlacement(OldActor);
		OldActor->Destroy();
	}
	for (ACastleBuildingBase* Actor : StagedActors)
	{
		if (!RegisteredActors.Contains(Actor)) { Actor->Destroy(); }
	}

	UE_LOG(LogMoving_Castle, Display, TEXT("棋盘 %s 拆除 %s（Lv%d）的格 %s，返还 1 张一级卡，重组等级：%s。"),
		*GetPathName(), *BuildingPath, OriginalLevel, *TargetCell.ToString(),
		*FString::JoinBy(Replacements, TEXT("、"), [](const FDemolitionReplacement& Replacement)
		{
			return FString::FromInt(Replacement.Actor->GetBuildingLevel());
		}));
	OnLayoutCommitted.Broadcast(this, ChangeOrigin);
	Backpack->OnBackpackChanged.Broadcast();
	// 重组与返还卡牌均已提交，每次合法拆除只反馈一次。
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Demolish);
	return true;
}
