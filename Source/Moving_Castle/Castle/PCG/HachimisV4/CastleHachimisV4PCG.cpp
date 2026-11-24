#include "CastleHachimisV4PCG.h"
#include "Moving_Castle.h"

#include "Castle/PCG/CastlePCGSemantics.h"
#include "Castle/PCG/CastleBuildingPCGRule.h"
#include "Castle/PCG/CastleLegacyPCGAdapter.h"
#include "Castle/PCG/CastlePCGAttachmentKit.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "BuildingMergeComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace CastleV4PCG
{
	constexpr double PositionTolerance = 0.1;
	constexpr double WallHalfDepth = 9.0;
	const FIntVector Up(0, 0, 1);
	const FIntVector Cardinal[] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}};

	bool Reject(const ACastleBuildingBase& Building, FString& Error, const FString& Reason)
	{
		Error = FString::Printf(TEXT("%s (%s)：%s"), *Building.GetPathName(), *Building.GetClass()->GetPathName(), *Reason);
		return false;
	}

	/** 套件须明确声明两种空间归属；组合法性在任何装配之前检查。 */
	bool ValidateLadderKit(const ACastleBuildingBase& Lower, const UCastleV4LadderKit& Kit, FString& Error)
	{
		int32 LadderCount = 0;
		int32 UpperFloorCount = 0;
		for (const FCastleV4Part& Part : Kit.Parts)
		{
			const bool bLadder = Part.Group == TEXT("Ladder");
			const bool bUpperFloor = Part.Group == TEXT("UpperFloor");
			if ((!bLadder && !bUpperFloor) || !IsValid(Part.Mesh) || !Part.Local.IsValid())
			{
				return Reject(Lower, Error, FString::Printf(TEXT("套件 %s 的零件 %s 分组、网格或变换无效。"),
					*Kit.GetPathName(), *Part.Node.ToString()));
			}
			LadderCount += bLadder;
			UpperFloorCount += bUpperFloor;
		}
		if (LadderCount == 0 || UpperFloorCount == 0)
		{
			return Reject(Lower, Error, FString::Printf(TEXT("套件 %s 缺少 Ladder 或 UpperFloor 组。"), *Kit.GetPathName()));
		}
		return true;
	}

	bool MapCells(const ACastleBuildingBase& Building, const ACastleBoard& Board, TMap<FIntVector, FIntVector>& Out, FString& Error)
	{
		const UCastleHachimisV4Layout& Layout = *Building.HachimisLayout;
		const auto& Cells = Building.GetOccupiedCoordinates();
		if (!FMath::IsNearlyEqual(Board.CellSize, Layout.CellPitch, PositionTolerance)
			|| !FMath::IsNearlyEqual(Board.LayerHeight, Layout.StoryRise, PositionTolerance)
			|| Cells.Num() != Layout.Grid.X * Layout.Grid.Y * Layout.Grid.Z
			|| !Layout.Ladder || Layout.Ladder->Family != Layout.Family || Layout.StandingHeight <= 0.0)
		{
			return Reject(Building, Error, Layout.GetPathName() + TEXT(" 的占格、标高或本族梯配置错误"));
		}
		if (!ValidateLadderKit(Building, *Layout.Ladder, Error)) { return false; }
		TSet<FIntVector> Claimed;
		for (int32 X = 0; X < Layout.Grid.X; ++X)
		for (int32 Y = 0; Y < Layout.Grid.Y; ++Y)
		for (int32 Z = 0; Z < Layout.Grid.Z; ++Z)
		{
			const FIntVector Local(X, Y, Z);
			const FVector World = Building.GetActorTransform().TransformPosition(Layout.CellBottom(Local));
			const FIntVector* Match = Cells.FindByPredicate([&](const FIntVector& Cell)
			{
				return World.Equals(CellFrame(Board, Cell).GetLocation(), PositionTolerance);
			});
			if (!Match || Claimed.Contains(*Match)) { return Reject(Building, Error, TEXT("作者格变换没有唯一匹配棋盘格")); }
			Claimed.Add(*Match);
			Out.Add(Local, *Match);
		}
		return true;
	}

	FIntVector BoardDirection(const ACastleBuildingBase& Building, const ACastleBoard& Board, const FIntVector& Local)
	{
		const FVector Direction = Board.GetActorTransform().InverseTransformVectorNoScale(
			Building.GetActorTransform().TransformVectorNoScale(FVector(Local)));
		return FIntVector(FMath::RoundToInt(Direction.X), FMath::RoundToInt(Direction.Y), FMath::RoundToInt(Direction.Z));
	}

	ECastlePCGFace FaceOf(const FIntVector& Direction)
	{
		if (Direction.X > 0) { return ECastlePCGFace::PositiveX; }
		if (Direction.X < 0) { return ECastlePCGFace::NegativeX; }
		if (Direction.Y > 0) { return ECastlePCGFace::PositiveY; }
		return ECastlePCGFace::NegativeY;
	}

	bool Extract(ACastleBuildingBase& Building, const ACastleBoard& Board, TArray<FCastlePCGPartAnnotation>& Out, FString& Error)
	{
		TMap<FIntVector, FIntVector> Cells;
		if (!MapCells(Building, Board, Cells, Error)) { return false; }
		FIntVector Minimum = Building.GetOccupiedCoordinates()[0];
		for (const FIntVector& Cell : Building.GetOccupiedCoordinates()) { Minimum = Minimum.ComponentMin(Cell); }
		const UCastleHachimisV4Layout& Layout = *Building.HachimisLayout;
		TArray<FCastlePCGPartAnnotation> Result;
		Result.SetNum(Layout.Parts.Num());
		TMap<FName, const FCastleV4WallSlot*> Slots;
		for (const FCastleV4WallSlot& Slot : Layout.Walls)
		{
			if (Slots.Contains(Slot.Node) || !Cells.Contains(Slot.Cell) || !Slot.Kit || Slot.Mount.ContainsNaN()
				|| !Slot.Kit->WallKey.ToString().StartsWith(Layout.Family.ToString() + TEXT("_")))
			{
				return Reject(Building, Error, Layout.GetPathName() + TEXT(" 中的墙位非法 ") + Slot.Node.ToString());
			}
			Slots.Add(Slot.Node, &Slot);
		}
		TArray<UStaticMeshComponent*> Components;
		Building.GetComponents(Components);
		for (UStaticMeshComponent* Component : Components)
		{
			// 飞入与合并会复制组件标签。只有 SCS 的作者原件可以声明持久槽位。
			if (Component->CreationMethod != EComponentCreationMethod::SimpleConstructionScript) { continue; }
			if (Component->ComponentTags.ContainsByPredicate([](const FName& Tag){ return Tag.ToString().StartsWith(TEXT("PCGRole")); }))
			{
				return Reject(Building, Error, Component->GetPathName() + TEXT(" 的 V4 原件不允许声明旧 PCGRole 标签"));
			}
			for (const FName& Tag : Component->ComponentTags)
			{
				const FString Text = Tag.ToString();
				if (!Text.StartsWith(TEXT("CastleV4Part="))) { continue; }
				int32 Index = INDEX_NONE;
				if (!LexTryParseString(Index, *Text.Mid(13)) || !Result.IsValidIndex(Index) || Result[Index].Component
					|| !Layout.Parts[Index].Mesh || Component->GetStaticMesh() != Layout.Parts[Index].Mesh
					|| UBuildingGameplayRootComponent::IsInGameplayHierarchy(Component))
				{
					return Reject(Building, Error, Component->GetPathName() + TEXT(" 的源节点索引、网格或语义标签无效"));
				}
				const FCastleV4Part& Design = Layout.Parts[Index];
				FCastlePCGPartAnnotation& Part = Result[Index];
				Part.Component = Component;
				Part.bOriginalVisible = Building.BuildingMerge->GetStableVisibility(Component);
				Part.Role = ECastlePCGPartRole::Structural;
				if (Design.Group == TEXT("Roof") || Design.Group == TEXT("RoofKeys")) { Part.Role = ECastlePCGPartRole::Roof; }
				if (Design.Group == TEXT("UpperFloor")) { Part.Role = ECastlePCGPartRole::TopCeilingSlab; }
				for (const FIntVector& Cell : Design.Cells)
				{
					const FIntVector* Mapped = Cells.Find(Cell);
					if (!Mapped) { return Reject(Building, Error, Design.Node.ToString() + TEXT(" 的源覆盖超出格面")); }
					Part.CoveredCellOffsets.Add(*Mapped - Minimum);
				}
				if (const FCastleV4WallSlot* const* Slot = Slots.Find(Design.Group))
				{
					Part.Role = ECastlePCGPartRole::PerimeterRailing;
					Part.BoardFacing = FaceOf(BoardDirection(Building, Board, (*Slot)->Facing));
					Part.CoveredCellOffsets.Add(Cells.FindChecked((*Slot)->Cell) - Minimum);
				}
				if (!Part.CoveredCellOffsets.IsEmpty()) { Part.LocalCellOffset = Part.CoveredCellOffsets[0]; }
			}
		}
		for (int32 Index = 0; Index < Result.Num(); ++Index)
		{
			if (!Result[Index].Component) { return Reject(Building, Error, Layout.Parts[Index].Node.ToString() + TEXT(" 缺少对应蓝图组件")); }
		}
		Out = MoveTemp(Result);
		return true;
	}

	bool BuildStairs(const ACastleBoard& Board, const FOccupancy& Occupancy, const FCastlePCGSemanticField& Field, FStairs& Out, FString& Error)
	{
		for (const auto& Entry : Field.VerticalInterfaces)
		{
			const FCastlePCGVerticalSpec& Spec = Entry.Value;
			if (Spec.State != ECastlePCGVertical::Ladder) { continue; }
			const FIntVector Cell = Entry.Key;
			const ACastleBuildingBase* Lower = Occupancy.FindRef(Cell);
			const ACastleBuildingBase* Upper = Occupancy.FindRef(Cell + Up);
			if (!Lower || !Upper)
			{
				Error = FString::Printf(TEXT("梯位 %s 缺少已完成的上下层建筑。"), *Cell.ToString());
				return false;
			}
			// 所有接梯的两侧共用同一井口，即使上下都是占位建筑也需要由上层切板。
			FStairInterface& Interface = Out.Add(Cell);
			Interface.WorldFrame = CellFrame(Board, Cell);
			Interface.WorldFrame.SetRotation(Board.GetActorQuat() * FQuat(FVector::UpVector, FMath::DegreesToRadians(Spec.BoardYaw)));
			if (Lower->HachimisLayout)
			{
				if (!IsValid(Spec.Kit) || Spec.Kit->Family != Lower->HachimisLayout->Family)
				{
					return Reject(*Lower, Error, FString::Printf(TEXT("层界 %s 缺少有效本族套件：%s。"),
						*Cell.ToString(), *GetPathNameSafe(Spec.Kit)));
				}
				if (!ValidateLadderKit(*Lower, *Spec.Kit, Error)) { return false; }
				Interface.Kit = Spec.Kit;
				Interface.Hatch = Spec.Kit->Hatch;
			}
			else
			{
				Interface.Hatch = CastleLegacyPCG::LadderHatch(Board.LayerHeight);
			}
			const FCastleV4Hatch& Hatch = Interface.Hatch;
			if (!Hatch.Opening.IsValid || Hatch.Opening.Min.ContainsNaN() || Hatch.Opening.Max.ContainsNaN()
				|| Hatch.BottomStand.ContainsNaN() || Hatch.TopStand.ContainsNaN() || Hatch.LadderBottom.ContainsNaN()
				|| Hatch.LadderTop.ContainsNaN() || !Interface.WorldFrame.IsValid())
			{
				return Reject(*Lower, Error, TEXT("层界套件 ") + GetPathNameSafe(Spec.Kit) + TEXT(" 的梯点、朝向或井口无效。"));
			}
			const double LowerWalk = Lower->HachimisLayout ? Lower->HachimisLayout->StandingHeight : 0.0;
			const double UpperWalk = Upper->HachimisLayout ? Upper->HachimisLayout->StandingHeight : 0.0;
			// 混接的两个行走面高差只改变梯的竖向尺度，井口仍和作者挂点共享 XY。
			const double Scale = (Board.LayerHeight + UpperWalk - LowerWalk) / Board.LayerHeight;
			Interface.WorldFrame.SetScale3D(Interface.WorldFrame.GetScale3D() * FVector(1, 1, Scale));
			Interface.WorldFrame.AddToTranslation(Board.GetActorTransform().TransformVector(FVector(0, 0, LowerWalk * (1.0 - Scale))));
		}
		return true;
	}

	bool AddPart(const ACastleBuildingBase& Building, const FCastleV4Part& Part, const FTransform& RelativeFrame,
		FCastlePCGApplyPlan& Plan, FString& Error)
	{
		if (!Part.Mesh || Part.Local.ContainsNaN() || RelativeFrame.ContainsNaN())
		{
			return Reject(Building, Error, Part.Node.ToString() + TEXT(" 的静态网格或装配变换非法"));
		}
		FCastlePCGMeshTemplate& Visual = Plan.Visual.Visuals.AddDefaulted_GetRef();
		Visual.Mesh = Part.Mesh;
		Visual.Node = Part.Node;
		Visual.Group = Part.Group;
		Visual.Relative = Part.Local * RelativeFrame;
		Visual.bSelectable = true;
		return true;
	}

	void AddBox(FCastlePCGApplyPlan& Plan, const FTransform& Frame, const FVector& Center, const FVector& Extent,
		bool bIsNavigationBarrier, const FQuat& Rotation = FQuat::Identity)
	{
		Plan.Gameplay.FunctionalBoxes.Add({FTransform(Rotation, Center) * Frame, Extent, bIsNavigationBarrier});
	}

	/** 生成墙另一侧的通行约束。柱间外缘始终封住，两侧门洞采用作者最窄净宽。 */
	void AddWallCollision(FCastlePCGApplyPlan& Plan, const FTransform& WallFrame, double FullWidth, double DoorWidth,
		double SideOffset, double Stand, double Height)
	{
		const double HalfWidth = FullWidth * 0.5;
		if (DoorWidth == 0.0)
		{
			AddBox(Plan, WallFrame, FVector(SideOffset, 0, Stand + Height * 0.5), FVector(HalfWidth, WallHalfDepth, Height * 0.5), true);
			return;
		}
		const double DoorHalf = DoorWidth * 0.5;
		const double Left = SideOffset - HalfWidth;
		const double Right = SideOffset + HalfWidth;
		AddBox(Plan, WallFrame, FVector((Left - DoorHalf) * 0.5, 0, Stand + Height * 0.5),
			FVector((-DoorHalf - Left) * 0.5, WallHalfDepth, Height * 0.5), true);
		AddBox(Plan, WallFrame, FVector((Right + DoorHalf) * 0.5, 0, Stand + Height * 0.5),
			FVector((Right - DoorHalf) * 0.5, WallHalfDepth, Height * 0.5), true);
	}

	const FCastleV4WallSlot* FindWall(const UCastleHachimisV4Layout& Layout, const FIntVector& Cell, const FIntVector& Direction)
	{
		return Layout.Walls.FindByPredicate([&](const FCastleV4WallSlot& Slot) { return Slot.Cell == Cell && Slot.Facing == Direction; });
	}

	bool PrepareWall(const ACastleBuildingBase& Building, const ACastleBoard& Board, const UCastlePCGRuleSetDataAsset& Rules,
		const FCastlePCGSideFaceSpec& Face, const FCastleV4WallSlot& Slot,
		TConstArrayView<FCastlePCGPartAnnotation> Original, FCastlePCGApplyPlan& Plan, FString& Error)
	{
		const UCastleHachimisV4Layout& Layout = *Building.HachimisLayout;
		// 门洞状态只来自规则。开口几何由原墙槽的作者套件决定。
		const bool bOpen = Face.IsDoor();
		const bool bRemoveWall = !Face.bOwnsWall || Face.State == ECastlePCGSide::MergedOpen;
		const UCastleV4WallKit& Kit = *Slot.Kit;
		if (!FMath::IsFinite(Kit.Opening) || Kit.Opening < 150.0 || Kit.Open.IsEmpty() || Kit.Closed.IsEmpty())
		{
			return Reject(Building, Error, Kit.GetPathName() + TEXT(" 的墙状态或门宽无效"));
		}
		const FTransform Mount = Slot.Mount;
		if (bOpen || bRemoveWall)
		{
			for (int32 Index = 0; Index < Layout.Parts.Num(); ++Index)
			{
				if (Layout.Parts[Index].Group == Slot.Node) { Plan.Visual.Hidden.Add(Original[Index].Component); }
			}
		}
		// 贯通面和归对侧的共享墙都不在本侧安装网格或阻隔功能盒。
		if (bRemoveWall) { return true; }
		if (bOpen)
		{
			for (const FCastleV4Part& Part : Kit.Open)
			{
				if (!AddPart(Building, Part, Mount, Plan, Error)) { return false; }
			}
		}
		const double Tangent = Mount.InverseTransformPosition(Layout.CellBottom(Slot.Cell)).X;
		AddWallCollision(Plan, Mount, Board.CellSize + 0.2, bOpen ? Kit.Opening : 0.0, Tangent, Layout.StandingHeight, Rules.BarrierHeight);
		return true;
	}

	bool CutDeck(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FIntVector& Cell, double Walk,
		UPrimitiveComponent& Floor, const FStairInterface& Interface, FCastlePCGApplyPlan& Plan, FString& Error)
	{
		const FTransform CellLocal = CellFrame(Board, Cell).GetRelativeTransform(Building.GetActorTransform());
		const FTransform HatchLocal = Interface.WorldFrame.GetRelativeTransform(Building.GetActorTransform());
		const FBox Opening = Interface.Hatch.Opening.TransformBy(HatchLocal.ToMatrixWithScale());
		const FVector GridPosition = CellLocal.GetLocation();
		const double Half = Board.CellSize * 0.5;
		const FVector Low = GridPosition + FVector(-Half, -Half, Walk - 5.0);
		const FVector High = GridPosition + FVector(Half, Half, Walk);
		if (!Opening.IsValid || Opening.Min.X <= Low.X || Opening.Min.Y <= Low.Y || Opening.Max.X >= High.X || Opening.Max.Y >= High.Y)
		{
			return Reject(Building, Error, TEXT("梯井超出应切换的上层占格"));
		}
		Plan.Gameplay.DisabledFloors.Add(&Floor);
		const FBox Rects[] = {
			FBox(Low, FVector(Opening.Min.X, High.Y, High.Z)),
			FBox(FVector(Opening.Max.X, Low.Y, Low.Z), High),
			FBox(FVector(Opening.Min.X, Low.Y, Low.Z), FVector(Opening.Max.X, Opening.Min.Y, High.Z)),
			FBox(FVector(Opening.Min.X, Opening.Max.Y, Low.Z), FVector(Opening.Max.X, High.Y, High.Z))};
		for (const FBox& Rect : Rects) { AddBox(Plan, FTransform::Identity, Rect.GetCenter(), Rect.GetExtent(), false); }
		return true;
	}

	void AddLink(FCastlePCGApplyPlan& Plan, const FIntVector& Lower, const FTransform& Frame, const FCastleV4Hatch& Hatch)
	{
		Plan.Gameplay.LadderFrames.Add({Lower, Frame, Hatch.BottomStand, Hatch.TopStand, Hatch.LadderBottom, Hatch.LadderTop});
	}

	/** V4 原功能板按作者格标签定位，收集唯一命中后再生成上层四片 CutDeck。 */
	bool PrepareIncomingDeck(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FIntVector& Cell,
		const FIntVector& LocalCell, const FStairInterface& Stair, FCastlePCGApplyPlan& Plan, FString& Error)
	{
		TArray<UStaticMeshComponent*> Floors, Matches;
		Building.GetComponents(Floors);
		const FName Tag(*FString::Printf(TEXT("CastleV4Floor=%d,%d,%d"), LocalCell.X, LocalCell.Y, LocalCell.Z));
		for (UStaticMeshComponent* Floor : Floors)
		{
			if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Floor) && Floor->ComponentHasTag(Tag))
			{
				Matches.Add(Floor);
			}
		}
		if (Matches.Num() != 1 || !IsValid(Matches[0]->GetStaticMesh()))
		{
			return Reject(Building, Error, FString::Printf(TEXT("格 %s 必须匹配一块有效 V4 原功能板，实际 %d 块。"),
				*Cell.ToString(), Matches.Num()));
		}
		return CutDeck(Board, Building, Cell, Building.HachimisLayout->StandingHeight, *Matches[0], Stair, Plan, Error);
	}

	bool AddStairGroup(const ACastleBuildingBase& Building, const FStairInterface& Stair, FName Group,
		FCastlePCGApplyPlan& Plan, FString& Error)
	{
		if (!IsValid(Stair.Kit))
		{
			return Reject(Building, Error, TEXT("梯井组 ") + Group.ToString() + TEXT(" 缺少有效 V4 套件。"));
		}
		const FTransform Relative = Stair.WorldFrame.GetRelativeTransform(Building.GetActorTransform());
		for (const FCastleV4Part& Part : Stair.Kit->Parts)
		{
			if (Part.Group == Group && !AddPart(Building, Part, Relative, Plan, Error)) { return false; }
		}
		return true;
	}

	bool AddAttachments(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FCastlePCGSemanticField& Field,
		FCastlePCGApplyPlan& Plan, FString& Error)
	{
		auto AddKit = [&](const FIntVector& Cell, const FCastlePCGAttachmentSpec& Spec, FName VisualGroup = NAME_None)
		{
			// 锚点和功能盒都在所占格底面坐标系中，建筑移动或旋转时自然随其组件。
			const FTransform Anchor = Spec.AnchorTransform * CellFrame(Board, Cell).GetRelativeTransform(Building.GetActorTransform());
			for (const FCastleV4Part& Part : Spec.Kit->Parts)
			{
				if (!AddPart(Building, Part, Anchor, Plan, Error)) { return false; }
				FCastlePCGMeshTemplate& Visual = Plan.Visual.Visuals.Last();
				Visual.bSelectable = Spec.bSelectable;
				if (!VisualGroup.IsNone()) { Visual.Group = VisualGroup; }
			}
			if (Spec.bAffectsNavigation)
			{
				Plan.Gameplay.FunctionalBoxes.Add({Anchor, Spec.NavigationExtent, true});
			}
			return true;
		};
		const auto& Cells = Building.GetOccupiedCoordinates();
		for (const FIntVector& Cell : Cells)
		{
			const FCastlePCGCellSpec& Spec = Field.Cells.FindChecked(Cell);
			if (Spec.Roof == ECastlePCGRoof::Override && !AddKit(Cell, Spec.RoofOverride)) { return false; }
			if (Spec.DeckOverride.IsSet())
			{
				const bool bHatch = Spec.Deck == ECastlePCGDeck::Hatch;
				if (!AddKit(Cell, Spec.DeckOverride.GetValue(), bHatch ? FName(TEXT("UpperFloor")) : NAME_None)) { return false; }
				if (bHatch)
				{
					// D7 已授权的临时替代。功能层仍装配切洞板和双向梯链接。
					UE_LOG(LogMoving_Castle, Warning,
						TEXT("建筑 %s 格 %s：挖洞款宽边地板美术暂缺，视觉暂用无洞宽边 Deck 替代，切洞碰撞与爬梯保持开启"),
						*Building.GetPathName(), *Cell.ToString());
				}
			}
		}
		for (const auto& Entry : Field.GetAttachments())
		{
			if (Cells.Contains(Entry.Key.OwnerCell) && !AddKit(Entry.Key.OwnerCell, Entry.Value)) { return false; }
		}
		return true;
	}

	bool Prepare(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
		const FCastlePCGSemanticField& Field, const FStairs& Stairs,
		TConstArrayView<FCastlePCGPartAnnotation> Original, FCastlePCGApplyPlan& Plan, FString& Error)
	{
		const UCastleHachimisV4Layout& Layout = *Building.HachimisLayout;
		TMap<FIntVector, FIntVector> Cells;
		if (!MapCells(Building, Board, Cells, Error)) { return false; }
		if (Original.Num() != Layout.Parts.Num()) { return Reject(Building, Error, Layout.GetPathName() + TEXT(" 的原始组件数不一致")); }
		const FIntVector FirstCell = Building.GetOccupiedCoordinates()[0];
		if (Field.Cells.FindChecked(FirstCell).RoomDeck.Layout)
		{
			if (!PrepareRoom(Board, Building, Rules, Field, Original, Plan, Error)) { return false; }
		}
		else
		{
			for (const FCastleV4WallSlot& Slot : Layout.Walls)
			{
				const ECastlePCGFace Face = FaceOf(BoardDirection(Building, Board, Slot.Facing));
				const FCastlePCGSideFaceSpec& Side = Field.SideFaces.FindChecked({Cells.FindChecked(Slot.Cell), Face});
				if (!PrepareWall(Building, Board, Rules, Side, Slot, Original, Plan, Error)) { return false; }
			}
			for (int32 Index = 0; Index < Layout.Parts.Num(); ++Index)
			{
				const FCastleV4Part& Part = Layout.Parts[Index];
				if ((Part.Group == TEXT("Roof") || Part.Group == TEXT("RoofKeys")) && Part.Cells.ContainsByPredicate([&](const FIntVector& Cell)
				{
					return Field.Cells.FindChecked(Cells.FindChecked(Cell)).Roof != ECastlePCGRoof::Keep;
				})) { Plan.Visual.Hidden.Add(Original[Index].Component); }
				if (Part.Group != TEXT("Foundation")) { continue; }
				const auto IsDeckVisualReplaced = [&](const FIntVector& Cell)
				{
					const FCastlePCGCellSpec& Spec = Field.Cells.FindChecked(Cells.FindChecked(Cell));
					return Spec.Deck == ECastlePCGDeck::Hatch || Spec.DeckOverride.IsSet();
				};
				if (Part.Cells.ContainsByPredicate(IsDeckVisualReplaced))
				{
					Plan.Visual.Hidden.Add(Original[Index].Component);
					for (const FCastleV4Part& Slice : Layout.FoundationSlices)
					{
						if (Slice.Node != Part.Node) { continue; }
						if (Slice.Cells.Num() != 1 || !Cells.Contains(Slice.Cells[0]))
						{
							return Reject(Building, Error, Slice.Node.ToString() + TEXT(" 的作者底盘切片归属非法。"));
						}
						if (!IsDeckVisualReplaced(Slice.Cells[0])
							&& !AddPart(Building, Slice, FTransform::Identity, Plan, Error)) { return false; }
					}
				}
			}
			if (Layout.Grid.Z == 2)
			{
				const FVector Bottom = Layout.InternalHatch.BottomStand;
				const FIntVector Cell(Bottom.X > 0.0 ? Layout.Grid.X - 1 : 0, Bottom.Y > 0.0 ? Layout.Grid.Y - 1 : 0, 0);
				AddLink(Plan, Cells.FindChecked(Cell), FTransform::Identity, Layout.InternalHatch);
			}
		}
		for (const auto& Pair : Cells)
		{
			const FIntVector Cell = Pair.Value;
			if (const FStairInterface* Incoming = Stairs.Find(Cell - Up))
			{
				if (!PrepareIncomingDeck(Board, Building, Cell, Pair.Key, *Incoming, Plan, Error)) { return false; }
				// V4 上层只接收共享井口的带洞楼板组；混接旧下层的楼板由旧适配器准备。
				if (Incoming->Kit && !Field.Cells.FindChecked(Cell).DeckOverride.IsSet()
					&& !AddStairGroup(Building, *Incoming, TEXT("UpperFloor"), Plan, Error)) { return false; }
			}
			if (const FStairInterface* Outgoing = Stairs.Find(Cell))
			{
				if (!AddStairGroup(Building, *Outgoing, TEXT("Ladder"), Plan, Error)) { return false; }
				const FTransform Relative = Outgoing->WorldFrame.GetRelativeTransform(Building.GetActorTransform());
				AddLink(Plan, Cell, Relative, Outgoing->Hatch);
			}
		}
		return true;
	}
}
