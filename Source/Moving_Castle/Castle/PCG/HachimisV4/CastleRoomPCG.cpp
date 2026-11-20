#include "CastleHachimisV4PCG.h"

#include "Castle/PCG/CastlePCGSemantics.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "Engine/StaticMesh.h"

namespace CastleV4PCG
{
	bool Reject(const ACastleBuildingBase& Building, FString& Error, const FString& Reason);
	bool AddPart(const ACastleBuildingBase& Building, const FCastleV4Part& Part, const FTransform& RelativeFrame,
		FCastlePCGApplyPlan& Plan, FString& Error);
	void AddWallCollision(FCastlePCGApplyPlan& Plan, const FTransform& WallFrame, double FullWidth, double DoorWidth,
		double SideOffset, double Stand, double Height);
	void AddHeightTransition(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FIntVector& Cell,
		const FIntVector& Side, const UCastlePCGRuleSetDataAsset& Rules, FCastlePCGApplyPlan& Plan);
	const FCastleV4WallSlot* FindWall(const UCastleHachimisV4Layout& Layout, const FIntVector& Cell, const FIntVector& Direction);
}

namespace CastleRoomPCG
{
	using namespace CastleV4PCG;
	const FIntVector Up(0, 0, 1);
	const FIntVector Directions[] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}};

	FIntVector ToBoard(const FRoomSample& Sample, const FIntVector& Local)
	{
		return Sample.Origin + (Sample.bTurn ? FIntVector(-Local.Y, Local.X, Local.Z) : Local);
	}

	FIntVector ToLocalDirection(const FRoomSample& Sample, const FIntVector& Direction)
	{
		return Sample.bTurn ? FIntVector(Direction.Y, -Direction.X, Direction.Z) : Direction;
	}

	FTransform InRoom(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FRoomSample& Sample)
	{
		const FTransform Root = CellFrame(Board, Sample.Origin);
		const FTransform Turn(Sample.bTurn ? FQuat(FVector::UpVector, HALF_PI) : FQuat::Identity);
		const FTransform Origin(-Sample.Layout->CellBottom(FIntVector::ZeroValue));
		return (Origin * Turn * Root).GetRelativeTransform(Building.GetActorTransform());
	}

	bool SameColorRoom(const FOccupancy& Occupancy, const ACastleBuildingBase& Building, const FIntVector& Cell)
	{
		const ACastleBuildingBase* Other = Occupancy.FindRef(Cell);
		return IsRoom(Other) && Other->GetBuildingColor() == Building.GetBuildingColor();
	}

	bool SameSample(const FRoomSample& A, const FRoomSample& B)
	{
		return A.Layout == B.Layout && A.Origin == B.Origin && A.bTurn == B.bTurn;
	}

	/** 按层标记普通房的水平互达连通分量。层界的梯组由上下两个分量共同标识。 */
	void HorizontalIslands(const TArray<FIntVector>& Cells, const FOccupancy& Occupancy, TMap<FIntVector, FIntVector>& Out)
	{
		for (const FIntVector& Start : Cells)
		{
			if (Out.Contains(Start)) { continue; }
			TArray<FIntVector> Queue{Start};
			Out.Add(Start, Start);
			for (int32 Index = 0; Index < Queue.Num(); ++Index)
			{
				const FIntVector Current = Queue[Index];
				for (const FIntVector& Direction : Directions)
				{
					const FIntVector Next = Current + Direction;
					const ACastleBuildingBase* Neighbor = Occupancy.FindRef(Next);
					if (!Out.Contains(Next) && IsRoom(Neighbor) && CanTraverse(*Occupancy.FindChecked(Current), *Neighbor))
					{
						Out.Add(Next, Start);
						Queue.Add(Next);
					}
				}
			}
		}
	}

	/** 大样例先覆盖规整部分。剩余格用同一组面、角、层界规则逐格收口。 */
	void MatchSamples(const TArray<FIntVector>& Cells, const UCastleHachimisV4Layout& Example, bool bTurn, bool bStack,
		const FOccupancy& Occupancy, TSet<FIntVector>& Used, FRooms& Rooms)
	{
		for (const FIntVector& Origin : Cells)
		{
			const ACastleBuildingBase& Anchor = *Occupancy.FindChecked(Origin);
			FRoomSample Sample{&Example, FIntVector::ZeroValue, Origin, bTurn};
			TArray<FIntVector, TInlineAllocator<4>> Block;
			for (int32 X = 0; X < Example.Grid.X; ++X)
			for (int32 Y = 0; Y < Example.Grid.Y; ++Y)
			{
				const FIntVector Cell = ToBoard(Sample, FIntVector(X, Y, 0));
				const ACastleBuildingBase* Lower = Occupancy.FindRef(Cell);
				const ACastleBuildingBase* Upper = Occupancy.FindRef(Cell + Up);
				if (Used.Contains(Cell) || !IsRoom(Lower)
					|| (!bStack && Lower->GetBuildingColor() != Anchor.GetBuildingColor())
					|| (bStack && (!IsRoom(Upper) || !CanTraverse(Anchor, *Lower))))
				{
					continue;
				}
				Block.Add(Cell);
			}
			if (Block.Num() != Example.Grid.X * Example.Grid.Y) { continue; }
			for (int32 X = 0; X < Example.Grid.X; ++X)
			for (int32 Y = 0; Y < Example.Grid.Y; ++Y)
			{
				Sample.Cell = FIntVector(X, Y, 0);
				const FIntVector Cell = ToBoard(Sample, Sample.Cell);
				Used.Add(Cell);
				if (bStack)
				{
					// 三层以上时，同一房有上一界面和下一界面。优先用它自己的上接口作本层框架来源。
					Rooms.FindChecked(Cell).Frame = Sample;
					FRoomSample& Upper = Rooms.FindChecked(Cell + Up).Frame;
					if (!Upper.Layout || Upper.Cell.Z != 0)
					{
						Upper = Sample;
						Upper.Cell.Z = 1;
					}
				}
				else
				{
					Rooms.FindChecked(Cell).Deck = Sample;
				}
			}
		}
	}

	/** 跨格装饰的所有者由棋盘坐标确定。只有同一源样例仍存在且未被上楼遮挡的整件才装配。 */
	bool OwnRoofKey(const FCastleV4Part& Part, const FRoomSample& Sample, const FIntVector& RoomCell,
		const FOccupancy& Occupancy, const FRooms& Rooms)
	{
		TArray<FIntVector> Covered;
		for (const FIntVector& Local : Part.Cells)
		{
			const FIntVector Cell = ToBoard(Sample, Local);
			const FRoomCell* Data = Rooms.Find(Cell);
			if (!Data || !SameSample(Data->Frame, Sample) || Occupancy.Contains(Cell + Up)) { return false; }
			Covered.Add(Cell);
		}
		Covered.Sort(CastlePCGSemantics::CellLess);
		return !Covered.IsEmpty() && Covered[0] == RoomCell;
	}

	bool AtSharedDeckEdge(const FCastleV4Part& Part, const FRoomSample& Sample, const ACastleBuildingBase& Building,
		const FOccupancy& Occupancy, const FIntVector& Cell)
	{
		const FString Name = Part.Node.ToString();
		if (!Name.StartsWith(TEXT("Deck_EndApron")) && !Name.StartsWith(TEXT("Deck_SideApron"))
			&& !Name.StartsWith(TEXT("Deck_PerimeterCoping")) && !Name.StartsWith(TEXT("Chassis_"))) { return false; }
		const FBox Box = Part.Mesh->GetBoundingBox().TransformBy(Part.Local.ToMatrixWithScale());
		const FVector Relative = Box.GetCenter() - Sample.Layout->CellBottom(Sample.Cell);
		// 外缘件只占作者底盘边缘 40 cm。宽跨边的长条不会被错误标成相邻两侧的边缘件。
		for (const FIntVector& Direction : Directions)
		{
			if (!SameColorRoom(Occupancy, Building, Cell + Direction)) { continue; }
			if (FVector::DotProduct(Relative, FVector(ToLocalDirection(Sample, Direction))) > Sample.Layout->CellPitch * 0.5 - 40.0) { return true; }
		}
		return false;
	}

	bool AddDeck(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FOccupancy& Occupancy,
		const FRoomSample& Deck, const FIntVector& Cell, FCastlePCGApplyPlan& Plan, FString& Error)
	{
		const FTransform Frame = InRoom(Board, Building, Deck);
		for (const FCastleV4Part& Part : Deck.Layout->Parts)
		{
			if (Part.Group != TEXT("Foundation") || Part.Cells.Num() != 1 || Part.Cells[0] != Deck.Cell) { continue; }
			if (!Part.Mesh) { return Reject(Building, Error, Part.Node.ToString() + TEXT(" 的 Room 底盘网格缺失")); }
			if (!AtSharedDeckEdge(Part, Deck, Building, Occupancy, Cell) && !AddPart(Building, Part, Frame, Plan, Error)) { return false; }
		}
		for (const FCastleV4Part& Slice : Deck.Layout->FoundationSlices)
		{
			if (Slice.Cells[0] != Deck.Cell) { continue; }
			if (!Slice.Mesh) { return Reject(Building, Error, Slice.Node.ToString() + TEXT(" 的 Room 底盘切片缺失")); }
			if (!AtSharedDeckEdge(Slice, Deck, Building, Occupancy, Cell) && !AddPart(Building, Slice, Frame, Plan, Error)) { return false; }
		}
		return true;
	}

	bool AddFrame(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FOccupancy& Occupancy, const FRooms& Rooms,
		const FRoomSample& Sample, const FIntVector& Cell, bool bIncomingHatch, FCastlePCGApplyPlan& Plan, FString& Error)
	{
		const FTransform Frame = InRoom(Board, Building, Sample);
		const UCastleHachimisV4Layout& Layout = *Sample.Layout;
		const bool bExposed = !Occupancy.Contains(Cell + Up);
		for (const FCastleV4Part& Part : Layout.Parts)
		{
			if (!Part.Mesh) { return Reject(Building, Error, Part.Node.ToString() + TEXT(" 的 Room 木构来源缺失")); }
			const FName Group = Part.Group;
			if (Group == TEXT("Roof"))
			{
				if (bExposed && Part.Cells.Contains(Sample.Cell) && !AddPart(Building, Part, Frame, Plan, Error)) { return false; }
			}
			else if (Group == TEXT("RoofKeys"))
			{
				if (OwnRoofKey(Part, Sample, Cell, Occupancy, Rooms) && !AddPart(Building, Part, Frame, Plan, Error)) { return false; }
			}
			else if (Group == TEXT("Structure") || Group == TEXT("WallJunction") || Group == TEXT("Furniture"))
			{
				// 由模型几何中心选择零件的单一所有者，中心落在格缝时取最小格。榫卯和梁不做粗暴裁切。
				const FVector Center = Part.Local.TransformPosition(Part.Mesh->GetBoundingBox().GetCenter());
				const int32 Layer = Center.Z >= Layout.StoryRise ? 1 : 0;
				FIntVector Owner(0, 0, Layer);
				double Closest = TNumericLimits<double>::Max();
				for (int32 X = 0; X < Layout.Grid.X; ++X)
				for (int32 Y = 0; Y < Layout.Grid.Y; ++Y)
				{
					const FIntVector Candidate(X, Y, Layer);
					const double Distance = FVector::DistSquared2D(Center, Layout.CellBottom(Candidate));
					if (Distance < Closest - 0.1) { Owner = Candidate; Closest = Distance; }
				}
				if (Owner == Sample.Cell)
				{
					// 作者支撑/家具在本层开洞处需让出梯井。楼板梯套件自带洞边压条。
					if (bIncomingHatch && Group == TEXT("Furniture") && Center.X > 49 && Center.X < 195 && Center.Y > 52 && Center.Y < 194) { continue; }
					if (!AddPart(Building, Part, Frame, Plan, Error)) { return false; }
				}
			}
		}
		return true;
	}

	bool AddWall(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
		const FIntVector& Cell, const FIntVector& Side, const FRoomSample& Sample, const ACastleBuildingBase* Other,
		FCastlePCGApplyPlan& Plan, FString& Error)
	{
		const bool bRoomNeighbor = IsRoom(Other);
		const bool bOpen = Other && CanTraverse(Building, *Other);
		if (bRoomNeighbor)
		{
			if (Building.GetBuildingColor() == Other->GetBuildingColor()) { return true; }
			// 两个独立房保留共同门槛，只由坐标较小的一侧持有墙和功能件。
			if (!CastlePCGSemantics::CellLess(Cell, Cell + Side)) { return true; }
		}
		if (!Other && !Sample.Layout) { return true; }
		const UCastleHachimisV4Layout* Model = Sample.Layout;
		const FCastleV4WallSlot* Slot = nullptr;
		FTransform Mount;
		FVector CellCenter;
		if (Model)
		{
			const FIntVector Direction = ToLocalDirection(Sample, Side);
			Slot = FindWall(*Model, Sample.Cell, Direction);
			if (Slot)
			{
				Mount = Slot->Mount * InRoom(Board, Building, Sample);
				CellCenter = InRoom(Board, Building, Sample).TransformPosition(Model->CellBottom(Sample.Cell));
			}
			else
			{
				// 真实相邻房跨样例接合而作者样例该格位于内部时，取此样例同面同跨度的外围套件。
				FIntVector Edge = Sample.Cell;
				if (Direction.X != 0) { Edge.X = Direction.X > 0 ? Model->Grid.X - 1 : 0; }
				else { Edge.Y = Direction.Y > 0 ? Model->Grid.Y - 1 : 0; }
				Slot = FindWall(*Model, Edge, Direction);
				if (!Slot) { return Reject(Building, Error, Model->GetPathName() + TEXT(" 缺少 Room 此朝向的外围墙位")); }
				const FTransform Frame = InRoom(Board, Building, Sample);
				Mount = Slot->Mount;
				Mount.AddToTranslation(Model->CellBottom(Sample.Cell) - Model->CellBottom(Edge));
				Mount *= Frame;
				CellCenter = Frame.TransformPosition(Model->CellBottom(Sample.Cell));
			}
		}
		else
		{
			// 裸底盘与异色房或功能塔接合时，按指南安装 Room L1 Bay。独立底盘不产生这个墙位。
			const TObjectPtr<UCastleHachimisV4Layout>* Example = Rules.RoomExamples.Find(TEXT("L2V"));
			if (!Example || !*Example || !(*Example)->Ladder) { return Reject(Building, Error, TEXT("Room 墙配置未初始化")); }
			if (!Rules.RoomL1Bay || Rules.RoomL1Bay->WallKey != TEXT("Room_L1_Bay"))
			{
				return Reject(Building, Error, TEXT("普通房底盘缺少显式 Room_L1_Bay 配置"));
			}
			const FTransform Frame = CellFrame(Board, Cell).GetRelativeTransform(Building.GetActorTransform());
			const FQuat Rotation(FVector::UpVector, FVector(Side).Rotation().Yaw * UE_PI / 180.0 - HALF_PI);
			Mount = FTransform(Rotation, FVector(Side) * 217.0) * Frame;
			CellCenter = Frame.GetLocation();
		}
		const UCastleV4WallKit* Kit = Slot ? Slot->Kit.Get() : Rules.RoomL1Bay.Get();
		if (!Kit || Kit->Open.IsEmpty() || Kit->Closed.IsEmpty() || Kit->Opening < 150.0)
		{
			return Reject(Building, Error, TEXT("Room 墙位缺少成对作者件"));
		}
		const auto& Parts = bOpen ? Kit->Open : Kit->Closed;
		for (const FCastleV4Part& Part : Parts)
		{
			if (!AddPart(Building, Part, Mount, Plan, Error)) { return false; }
		}
		AddWallCollision(Plan, Mount, Board.CellSize + 0.2, bOpen ? Kit->Opening : 0.0,
			Mount.InverseTransformPosition(CellCenter).X, Building.HachimisLayout->StandingHeight, Rules.BarrierHeight);
		if (bOpen && !Other->HachimisLayout) { AddHeightTransition(Board, Building, Cell, Side, Rules, Plan); }
		return true;
	}
}

bool CastleV4PCG::BuildRooms(const UCastlePCGRuleSetDataAsset& Rules, const FOccupancy& Occupancy,
	FTargets& Targets, FRooms& Rooms, FString& Error)
{
	using namespace CastleRoomPCG;
	TArray<FIntVector> Cells;
	for (const auto& Entry : Occupancy)
	{
		if (IsRoom(Entry.Value)) { Cells.Add(Entry.Key); Rooms.Add(Entry.Key); }
	}
	if (Cells.IsEmpty()) { return true; }
	Cells.Sort(CastlePCGSemantics::CellLess);
	const FName Names[] = {TEXT("L1"), TEXT("L2H"), TEXT("L3H"), TEXT("L2V"), TEXT("L3V"), TEXT("L4")};
	const FIntVector Dimensions[] = {{1,1,1},{2,1,1},{2,2,1},{1,1,2},{2,1,2},{2,2,2}};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
	{
		const TObjectPtr<UCastleHachimisV4Layout>* Model = Rules.RoomExamples.Find(Names[Index]);
		if (!Model || !*Model || (*Model)->Family != TEXT("Room") || (*Model)->Form != Names[Index]
			|| (*Model)->Grid != Dimensions[Index] || (*Model)->Parts.IsEmpty())
		{
			Error = Rules.GetPathName() + TEXT(" 缺少对应 Room 样例 ") + Names[Index].ToString();
			return false;
		}
	}
	TSet<FIntVector> DeckUsed;
	MatchSamples(Cells, *Rules.RoomExamples.FindChecked(TEXT("L3H")), false, false, Occupancy, DeckUsed, Rooms);
	MatchSamples(Cells, *Rules.RoomExamples.FindChecked(TEXT("L2H")), false, false, Occupancy, DeckUsed, Rooms);
	MatchSamples(Cells, *Rules.RoomExamples.FindChecked(TEXT("L2H")), true, false, Occupancy, DeckUsed, Rooms);
	MatchSamples(Cells, *Rules.RoomExamples.FindChecked(TEXT("L1")), false, false, Occupancy, DeckUsed, Rooms);
	TSet<FIntVector> FrameUsed;
	MatchSamples(Cells, *Rules.RoomExamples.FindChecked(TEXT("L4")), false, true, Occupancy, FrameUsed, Rooms);
	MatchSamples(Cells, *Rules.RoomExamples.FindChecked(TEXT("L3V")), false, true, Occupancy, FrameUsed, Rooms);
	MatchSamples(Cells, *Rules.RoomExamples.FindChecked(TEXT("L3V")), true, true, Occupancy, FrameUsed, Rooms);
	MatchSamples(Cells, *Rules.RoomExamples.FindChecked(TEXT("L2V")), false, true, Occupancy, FrameUsed, Rooms);
	TMap<FIntVector, FIntVector> Islands;
	HorizontalIslands(Cells, Occupancy, Islands);
	TSet<FString> StairPairs;
	for (const FIntVector& Cell : Cells)
	{
		ACastleBuildingBase* Lower = Occupancy.FindChecked(Cell);
		ACastleBuildingBase* Upper = Occupancy.FindRef(Cell + Up);
		if (!IsRoom(Upper) || !CanTraverse(*Lower, *Upper)) { continue; }
		const FString Pair = Islands.FindChecked(Cell).ToString() + TEXT("/") + Islands.FindChecked(Cell + Up).ToString();
		Targets.FindChecked(Lower).Ceilings.Add(Cell, StairPairs.Contains(Pair) ? 1 : 2);
		StairPairs.Add(Pair);
	}
	return true;
}

bool CastleV4PCG::PrepareRoom(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
	const FOccupancy& Occupancy, const FRooms& Rooms, const FStairs& Stairs, TConstArrayView<FCastlePCGPartAnnotation> Original,
	FCastlePCGApplyPlan& Plan, FString& Error)
{
	using namespace CastleRoomPCG;
	const FIntVector Cell = Building.GetOccupiedCoordinates()[0];
	const FRoomCell& Room = Rooms.FindChecked(Cell);
	const bool bIncomingHatch = Stairs.Contains(Cell - Up);
	bool bChangeDeck = bIncomingHatch || Room.Deck.Layout != Building.HachimisLayout;
	for (const FIntVector& Direction : Directions) { bChangeDeck |= SameColorRoom(Occupancy, Building, Cell + Direction); }
	if (bChangeDeck)
	{
		for (const auto& Part : Original) { Plan.Hidden.Add(Part.Component); }
		if (!bIncomingHatch && !AddDeck(Board, Building, Occupancy, Room.Deck, Cell, Plan, Error)) { return false; }
	}
	if (Room.Frame.Layout && !AddFrame(Board, Building, Occupancy, Rooms, Room.Frame, Cell, bIncomingHatch, Plan, Error)) { return false; }
	for (const FIntVector& Side : Directions)
	{
		if (!AddWall(Board, Building, Rules, Cell, Side, Room.Frame, Occupancy.FindRef(Cell + Side), Plan, Error)) { return false; }
	}
	return true;
}
