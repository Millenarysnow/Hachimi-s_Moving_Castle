#include "CastleHachimisV4PCG.h"

#include "Castle/PCG/CastlePCGSemantics.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "BuildingMergeComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/SecureHash.h"
#include "NavigationSystem.h"
#include "NavigationData.h"

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

	bool CanTraverse(const ACastleBuildingBase& A, const ACastleBuildingBase& B)
	{
		return A.GetBuildingColor() == B.GetBuildingColor() || A.GetBuildingNumber() == B.GetBuildingNumber();
	}

	bool IsRoom(const ACastleBuildingBase* Building)
	{
		return Building && Building->IsNormalRoom() && Building->HachimisLayout;
	}

	FTransform CellFrame(const ACastleBoard& Board, const FIntVector& Cell)
	{
		const FVector BottomWorld = Board.GetGridWorldLocation(Cell)
			- Board.GetActorTransform().TransformVector(FVector(0, 0, Board.LayerHeight * 0.5));
		return FTransform(Board.GetActorQuat(), BottomWorld, Board.GetActorScale3D());
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
			for (const FName& Tag : Component->ComponentTags)
			{
				const FString Text = Tag.ToString();
				if (!Text.StartsWith(TEXT("CastleV4Part="))) { continue; }
				int32 Index = INDEX_NONE;
				if (!LexTryParseString(Index, *Text.Mid(13)) || !Result.IsValidIndex(Index) || Result[Index].Component
					|| !Layout.Parts[Index].Mesh || Component->GetStaticMesh() != Layout.Parts[Index].Mesh
					|| UBuildingGameplayRootComponent::IsInGameplayHierarchy(Component)
					|| Component->ComponentTags.ContainsByPredicate([](const FName& Name){ return Name.ToString().StartsWith(TEXT("PCGRole")); }))
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

	bool BuildStairs(const ACastleBoard& Board, const FOccupancy& Occupancy, const FTargets& Targets, const FRooms& Rooms, FStairs& Out, FString& Error)
	{
		for (const auto& Entry : Targets)
		{
			const ACastleBuildingBase& Lower = *Entry.Key;
			for (const auto& Top : Entry.Value.Ceilings)
			{
				if (Top.Value != 2) { continue; }
				ACastleBuildingBase* Upper = Occupancy.FindRef(Top.Key + Up);
				if (!Upper) { return Reject(Lower, Error, TEXT("通梯格没有对应的上层建筑")); }
				if (!Lower.HachimisLayout && !Upper->HachimisLayout) { continue; }
				if (!Targets.Contains(Upper))
				{
					return Reject(*Upper, Error, TEXT("上层旧模板没有可切换的楼板语义"));
				}

				FStairInterface& Interface = Out.Add(Top.Key);
				Interface.WorldFrame = CellFrame(Board, Top.Key);
				const double LowerWalk = Lower.HachimisLayout ? Lower.HachimisLayout->StandingHeight : 0.0;
				const double UpperWalk = Upper->HachimisLayout ? Upper->HachimisLayout->StandingHeight : 0.0;
				if (Lower.HachimisLayout)
				{
					Interface.Kit = Lower.HachimisLayout->Ladder;
					if (!Interface.Kit || Interface.Kit->Family != Lower.HachimisLayout->Family || Interface.Kit->Parts.IsEmpty())
					{
						return Reject(Lower, Error, Lower.HachimisLayout->GetPathName() + TEXT(" 缺少本族楼板梯"));
					}
					Interface.Hatch = Interface.Kit->Hatch;
					// 塔梯随下层作者框架转动。普通房造型只由格局决定。
					if (!IsRoom(&Lower)) { Interface.WorldFrame.SetRotation(Lower.GetActorQuat()); }
				}
				else
				{
					// 点位沿用 BP_Variant_LV2，缺角取自 DaoDun/Variants/LV2/Mesh1 的 LOD0 顶面。
					Interface.Hatch.BottomStand = FVector(-58.317, 40.0, 0.0);
					Interface.Hatch.TopStand = FVector(40.0, 40.0, Board.LayerHeight);
					Interface.Hatch.LadderBottom = FVector(-58.317, 150.0, 0.0);
					Interface.Hatch.LadderTop = FVector(-58.317, 150.0, Board.LayerHeight);
					Interface.Hatch.Opening = FBox(FVector(-205.0, 100.0, Board.LayerHeight - 31.55), FVector(-50.0, 205.0, Board.LayerHeight));
				}
				if (!Interface.Hatch.Opening.IsValid || Interface.Hatch.Opening.Min.ContainsNaN() || Interface.Hatch.Opening.Max.ContainsNaN())
				{
					return Reject(Lower, Error, TEXT("下层所属套件的梯井无效"));
				}
				// 只调整梯的垂直尺度，使两个原生站立面精确相接，XY 与作者梯口一致。
				const double Scale = (Board.LayerHeight + UpperWalk - LowerWalk) / Board.LayerHeight;
				Interface.WorldFrame.SetScale3D(Interface.WorldFrame.GetScale3D() * FVector(1, 1, Scale));
				Interface.WorldFrame.AddToTranslation(Board.GetActorTransform().TransformVector(FVector(0, 0, LowerWalk * (1.0 - Scale))));
				// 只调整普通房互接时的套件方向；混接保持下层系列的安装锚，格位选择不变。
				if (IsRoom(&Lower) && IsRoom(Upper) && !OrientRoomStair(Board, Lower, *Upper,
					Rooms.FindChecked(Top.Key).Frame, Rooms.FindChecked(Top.Key + Up).Frame, Interface, Error)) { return false; }
			}
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
		FCastlePCGMeshTemplate& Visual = Plan.Visuals.AddDefaulted_GetRef();
		Visual.Mesh = Part.Mesh;
		Visual.Relative = Part.Local * RelativeFrame;
		Visual.bSelectable = true;
		return true;
	}

	void AddBox(FCastlePCGApplyPlan& Plan, const FTransform& Frame, const FVector& Center, const FVector& Extent,
		const FQuat& Rotation = FQuat::Identity)
	{
		Plan.FunctionalBoxes.Add({FTransform(Rotation, Center) * Frame, Extent});
	}

	/** 生成墙另一侧的通行约束。柱间外缘始终封住，两侧门洞采用作者最窄净宽。 */
	void AddWallCollision(FCastlePCGApplyPlan& Plan, const FTransform& WallFrame, double FullWidth, double DoorWidth,
		double SideOffset, double Stand, double Height)
	{
		const double HalfWidth = FullWidth * 0.5;
		if (DoorWidth == 0.0)
		{
			AddBox(Plan, WallFrame, FVector(SideOffset, 0, Stand + Height * 0.5), FVector(HalfWidth, WallHalfDepth, Height * 0.5));
			return;
		}
		const double DoorHalf = DoorWidth * 0.5;
		const double Left = SideOffset - HalfWidth;
		const double Right = SideOffset + HalfWidth;
		AddBox(Plan, WallFrame, FVector((Left - DoorHalf) * 0.5, 0, Stand + Height * 0.5),
			FVector((-DoorHalf - Left) * 0.5, WallHalfDepth, Height * 0.5));
		AddBox(Plan, WallFrame, FVector((Right + DoorHalf) * 0.5, 0, Stand + Height * 0.5),
			FVector((Right - DoorHalf) * 0.5, WallHalfDepth, Height * 0.5));
	}

	void AddHeightTransition(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FIntVector& Cell,
		const FIntVector& Side, const UCastlePCGRuleSetDataAsset& Rules, FCastlePCGApplyPlan& Plan)
	{
		constexpr double Run = 122.0;
		const double Height = Building.HachimisLayout->StandingHeight;
		const double Tilt = FMath::Atan2(-Height, Run);
		const double Width = 140.0;
		const FQuat Yaw = FVector(Side).ToOrientationQuat();
		const FTransform Frame = CellFrame(Board, Cell).GetRelativeTransform(Building.GetActorTransform());
		const FVector Edge = FVector(Side) * (Board.CellSize * 0.5);
		const FVector SurfaceCenter = Edge + FVector(Side) * (Run * 0.5) + FVector(0, 0, Height * 0.5);
		const FQuat Rotation = Yaw * FQuat(FVector::RightVector, -Tilt);
		AddBox(Plan, Frame, SurfaceCenter - Rotation.GetUpVector() * (Rules.FloorThickness * 0.5),
			FVector(FMath::Sqrt(Run * Run + Height * Height) * 0.5 + 2.0, Width * 0.5, Rules.FloorThickness * 0.5), Rotation);
	}

	const FCastleV4WallSlot* FindWall(const UCastleHachimisV4Layout& Layout, const FIntVector& Cell, const FIntVector& Direction)
	{
		return Layout.Walls.FindByPredicate([&](const FCastleV4WallSlot& Slot) { return Slot.Cell == Cell && Slot.Facing == Direction; });
	}

	bool PrepareWall(const ACastleBuildingBase& Building, const ACastleBoard& Board, const UCastlePCGRuleSetDataAsset& Rules,
		const FOccupancy& Occupancy, const FCastleV4WallSlot& Slot, const FIntVector& Cell, const FIntVector& BoardSide,
		TConstArrayView<FCastlePCGPartAnnotation> Original, FCastlePCGApplyPlan& Plan, FString& Error)
	{
		const UCastleHachimisV4Layout& Layout = *Building.HachimisLayout;
		const ACastleBuildingBase* Neighbor = Occupancy.FindRef(Cell + BoardSide);
		const bool bOpen = Neighbor && CanTraverse(Building, *Neighbor);
		const UCastleV4WallKit& Kit = *Slot.Kit;
		if (!FMath::IsFinite(Kit.Opening) || Kit.Opening < 150.0 || Kit.Open.IsEmpty() || Kit.Closed.IsEmpty())
		{
			return Reject(Building, Error, Kit.GetPathName() + TEXT(" 的墙状态或门宽无效"));
		}
		const FTransform Mount = Slot.Mount;
		if (bOpen)
		{
			for (int32 Index = 0; Index < Layout.Parts.Num(); ++Index)
			{
				if (Layout.Parts[Index].Group == Slot.Node) { Plan.Hidden.Add(Original[Index].Component); }
			}
			for (const FCastleV4Part& Part : Kit.Open)
			{
				if (!AddPart(Building, Part, Mount, Plan, Error)) { return false; }
			}
		}
		const double Tangent = Mount.InverseTransformPosition(Layout.CellBottom(Slot.Cell)).X;
		AddWallCollision(Plan, Mount, Board.CellSize + 0.2, bOpen ? Kit.Opening : 0.0, Tangent, Layout.StandingHeight, Rules.BarrierHeight);
		if (bOpen && !Neighbor->HachimisLayout) { AddHeightTransition(Board, Building, Cell, BoardSide, Rules, Plan); }
		return true;
	}

	bool CutDeck(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FIntVector& Cell, const FIntVector& SourceCell,
		const FStairInterface& Interface, FCastlePCGApplyPlan& Plan, FString& Error)
	{
		const FTransform CellLocal = CellFrame(Board, Cell).GetRelativeTransform(Building.GetActorTransform());
		const FTransform HatchLocal = Interface.WorldFrame.GetRelativeTransform(Building.GetActorTransform());
		const FBox Opening = Interface.Hatch.Opening.TransformBy(HatchLocal.ToMatrixWithScale());
		const FVector GridPosition = CellLocal.GetLocation();
		const double Half = Board.CellSize * 0.5;
		const double Walk = Building.HachimisLayout ? Building.HachimisLayout->StandingHeight : 0.0;
		const FVector Low = GridPosition + FVector(-Half, -Half, Walk - 5.0);
		const FVector High = GridPosition + FVector(Half, Half, Walk);
		if (!Opening.IsValid || Opening.Min.X <= Low.X || Opening.Min.Y <= Low.Y || Opening.Max.X >= High.X || Opening.Max.Y >= High.Y)
		{
			return Reject(Building, Error, TEXT("梯井超出应切换的上层占格"));
		}
		TArray<UStaticMeshComponent*> Floors, Matches;
		Building.GetComponents(Floors);
		const FName FloorTag(*FString::Printf(TEXT("CastleV4Floor=%d,%d,%d"), SourceCell.X, SourceCell.Y, SourceCell.Z));
		for (UStaticMeshComponent* Floor : Floors)
		{
			if (!UBuildingGameplayRootComponent::IsInGameplayHierarchy(Floor)) { continue; }
			// 盾塔沿用旧 GameplayFloor：多格地板的原点与 V4 格心相差 3.75 cm。
			// 从实际板面覆盖确定归属，既支持旋转建筑，也不会误关上层内部的楼板。
			bool bLegacyCell = false;
			if (!Building.HachimisLayout && Floor->CanEverAffectNavigation() && Floor->GetStaticMesh()
				&& Floor->GetName().StartsWith(TEXT("GameplayFloor_")))
			{
				const FTransform FloorLocal = Floor->GetComponentTransform().GetRelativeTransform(Building.GetActorTransform());
				const FBox Surface = Floor->GetStaticMesh()->GetBoundingBox().TransformBy(FloorLocal.ToMatrixWithScale());
				bLegacyCell = Surface.IsInsideXY(GridPosition) && FMath::IsNearlyEqual(Surface.Max.Z, GridPosition.Z, PositionTolerance);
			}
			if (Floor->ComponentHasTag(FloorTag) || bLegacyCell) { Matches.Add(Floor); }
		}
		if (Matches.Num() != 1)
		{
			return Reject(Building, Error, FString::Printf(TEXT("梯井(%s)必须匹配一块原功能板，实际 %d 块"), *Cell.ToString(), Matches.Num()));
		}
		Plan.DisabledFloors.Add(Matches[0]);
		const FBox Rects[] = {
			FBox(Low, FVector(Opening.Min.X, High.Y, High.Z)),
			FBox(FVector(Opening.Max.X, Low.Y, Low.Z), High),
			FBox(FVector(Opening.Min.X, Low.Y, Low.Z), FVector(Opening.Max.X, Opening.Min.Y, High.Z)),
			FBox(FVector(Opening.Min.X, Opening.Max.Y, Low.Z), FVector(Opening.Max.X, High.Y, High.Z))};
		for (const FBox& Rect : Rects) { AddBox(Plan, FTransform::Identity, Rect.GetCenter(), Rect.GetExtent()); }
		return true;
	}

	void AddLink(FCastlePCGApplyPlan& Plan, const FIntVector& Lower, const FTransform& Frame, const FCastleV4Hatch& Hatch)
	{
		Plan.LadderFrames.Add({Lower, Frame, Hatch.BottomStand, Hatch.TopStand, Hatch.LadderBottom, Hatch.LadderTop});
	}

	/** 计划签名包含两个界面的几何。相同格局即使重新广播也不会重开局部动画。 */
	void FinalizeKey(const FCastlePCGTarget& Target, FCastlePCGApplyPlan& Plan)
	{
		TArray<FString> Keys;
		for (const UPrimitiveComponent* Hidden : Plan.Hidden) { Keys.Add(TEXT("Hide/") + Hidden->GetName()); }
		for (const FCastlePCGMeshTemplate& Part : Plan.Visuals)
		{
			FString PartKey = Part.Mesh->GetPathName() + Part.Relative.ToString() + LexToString(Part.bSelectable);
			for (const UMaterialInterface* Material : Part.Materials) { PartKey += GetPathNameSafe(Material); }
			Keys.Add(MoveTemp(PartKey));
		}
		Keys.Sort();
		// 空键描述蓝图的原始美术。只接管功能面时不能截断原有的升级动画。
		if (!Keys.IsEmpty()) { Plan.VisualKey = FMD5::HashAnsiString(*FString::Join(Keys, TEXT("|"))); }
		Keys.Reset();
		for (const UPrimitiveComponent* Floor : Plan.DisabledFloors) { Keys.Add(TEXT("Floor/") + Floor->GetName()); }
		for (const auto& Box : Plan.FunctionalBoxes) { Keys.Add(Box.Key.ToString() + Box.Value.ToString()); }
		for (const FCastlePCGLadderPlan& Link : Plan.LadderFrames)
		{
			Keys.Add(Link.LowerCell.ToString() + Link.Relative.ToString() + Link.BottomStand.ToString() + Link.TopStand.ToString()
				+ Link.LadderBottom.ToString() + Link.LadderTop.ToString());
		}
		Keys.Sort();
		Plan.Key = Target.StableKey() + Plan.VisualKey + FMD5::HashAnsiString(*FString::Join(Keys, TEXT("|")));
	}

	bool Prepare(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
		const FOccupancy& Occupancy, const FRooms& Rooms, const FStairs& Stairs, const FCastlePCGTarget& Target,
		TConstArrayView<FCastlePCGPartAnnotation> Original, FCastlePCGApplyPlan& Plan, FString& Error)
	{
		const UCastleHachimisV4Layout& Layout = *Building.HachimisLayout;
		TMap<FIntVector, FIntVector> Cells;
		if (!MapCells(Building, Board, Cells, Error)) { return false; }
		if (Original.Num() != Layout.Parts.Num()) { return Reject(Building, Error, Layout.GetPathName() + TEXT(" 的原始组件数不一致")); }
		if (IsRoom(&Building))
		{
			if (!PrepareRoom(Board, Building, Rules, Occupancy, Rooms, Stairs, Original, Plan, Error)) { return false; }
		}
		else
		{
			for (const FCastleV4WallSlot& Slot : Layout.Walls)
			{
				if (!PrepareWall(Building, Board, Rules, Occupancy, Slot, Cells.FindChecked(Slot.Cell),
					BoardDirection(Building, Board, Slot.Facing), Original, Plan, Error)) { return false; }
			}
			for (int32 Index = 0; Index < Layout.Parts.Num(); ++Index)
			{
				const FCastleV4Part& Part = Layout.Parts[Index];
				if ((Part.Group == TEXT("Roof") || Part.Group == TEXT("RoofKeys")) && Part.Cells.ContainsByPredicate([&](const FIntVector& Cell)
				{
					return Target.Ceilings.Contains(Cells.FindChecked(Cell));
				})) { Plan.Hidden.Add(Original[Index].Component); }
				if (Part.Group != TEXT("Foundation")) { continue; }
				if (Part.Cells.ContainsByPredicate([&](const FIntVector& Cell){ return Stairs.Contains(Cells.FindChecked(Cell) - Up); }))
				{
					Plan.Hidden.Add(Original[Index].Component);
					for (const FCastleV4Part& Slice : Layout.FoundationSlices)
					{
						if (Slice.Node == Part.Node && !Stairs.Contains(Cells.FindChecked(Slice.Cells[0]) - Up)
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
				if (!CutDeck(Board, Building, Cell, Pair.Key, *Incoming, Plan, Error)) { return false; }
			}
			if (const FStairInterface* Outgoing = Stairs.Find(Cell))
			{
				if (!Outgoing->Kit || Outgoing->Kit->Parts.IsEmpty()) { return Reject(Building, Error, TEXT("缺少本族楼板梯资产")); }
				const FTransform Relative = Outgoing->WorldFrame.GetRelativeTransform(Building.GetActorTransform());
				for (const FCastleV4Part& Part : Outgoing->Kit->Parts)
				{
					if (!AddPart(Building, Part, Relative, Plan, Error)) { return false; }
				}
				AddLink(Plan, Cell, Relative, Outgoing->Hatch);
			}
		}
		if (!Plan.LadderFrames.IsEmpty())
		{
			UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Building.GetWorld());
			const ANavigationData* Data = Navigation ? Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
			if (!Data || Data->GetRuntimeGenerationMode() != ERuntimeGenerationType::Dynamic)
			{
				return Reject(Building, Error, TEXT("楼层梯连接要求动态 Recast 数据"));
			}
		}
		FinalizeKey(Target, Plan);
		return true;
	}
}
