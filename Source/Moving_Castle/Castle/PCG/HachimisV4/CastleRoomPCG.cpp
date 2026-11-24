#include "CastleHachimisV4PCG.h"

#include "Castle/PCG/CastlePCGSemantics.h"
#include "Castle/PCG/CastleBuildingPCGRule.h"
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
	const FCastleV4WallSlot* FindWall(const UCastleHachimisV4Layout& Layout, const FIntVector& Cell, const FIntVector& Direction);
}

namespace CastleRoomPCG
{
	using namespace CastleV4PCG;
	using FRoomSample = FCastlePCGRoomSample;

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

	bool SameSample(const FRoomSample& A, const FRoomSample& B)
	{
		return A.Layout == B.Layout && A.Origin == B.Origin && A.bTurn == B.bTurn;
	}

	/** 跨格装饰的所有者由棋盘坐标确定。只有同一源样例仍存在且未被上楼遮挡的整件才装配。 */
	bool OwnRoofKey(const FCastleV4Part& Part, const FRoomSample& Sample, const FIntVector& RoomCell,
		const FCastlePCGSemanticField& Field)
	{
		TArray<FIntVector> Covered;
		for (const FIntVector& Local : Part.Cells)
		{
			const FIntVector Cell = ToBoard(Sample, Local);
			const FCastlePCGCellSpec* Data = Field.Cells.Find(Cell);
			if (!Data || !SameSample(Data->RoomFrame, Sample) || Data->Roof != ECastlePCGRoof::Keep) { return false; }
			Covered.Add(Cell);
		}
		Covered.Sort(CastlePCGSemantics::CellLess);
		return !Covered.IsEmpty() && Covered[0] == RoomCell;
	}

	bool AtSharedDeckEdge(const FCastleV4Part& Part, const FRoomSample& Sample,
		const FCastlePCGSemanticField& Field, const FIntVector& Cell)
	{
		const FString Name = Part.Node.ToString();
		if (!Name.StartsWith(TEXT("Deck_EndApron")) && !Name.StartsWith(TEXT("Deck_SideApron"))
			&& !Name.StartsWith(TEXT("Deck_PerimeterCoping")) && !Name.StartsWith(TEXT("Chassis_"))) { return false; }
		const FBox Box = Part.Mesh->GetBoundingBox().TransformBy(Part.Local.ToMatrixWithScale());
		const FVector Relative = Box.GetCenter() - Sample.Layout->CellBottom(Sample.Cell);
		// 外缘件只占作者底盘边缘 40 cm。宽跨边的长条不会被错误标成相邻两侧的边缘件。
		for (const ECastlePCGFace Face : FCastlePCGBoardQuery::Sides)
		{
			if (Field.SideFaces.FindChecked({Cell, Face}).State != ECastlePCGSide::MergedOpen) { continue; }
			const FIntVector Direction = CastlePCGSemantics::FaceOffset(Face);
			if (FVector::DotProduct(Relative, FVector(ToLocalDirection(Sample, Direction))) > Sample.Layout->CellPitch * 0.5 - 40.0) { return true; }
		}
		return false;
	}

	bool AddDeck(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FCastlePCGSemanticField& Field,
		const FRoomSample& Deck, const FIntVector& Cell, FCastlePCGApplyPlan& Plan, FString& Error)
	{
		const FTransform Frame = InRoom(Board, Building, Deck);
		for (const FCastleV4Part& Part : Deck.Layout->Parts)
		{
			if (Part.Group != TEXT("Foundation") || Part.Cells.Num() != 1 || Part.Cells[0] != Deck.Cell) { continue; }
			if (!Part.Mesh) { return Reject(Building, Error, Part.Node.ToString() + TEXT(" 的 Room 底盘网格缺失")); }
			if (!AtSharedDeckEdge(Part, Deck, Field, Cell) && !AddPart(Building, Part, Frame, Plan, Error)) { return false; }
		}
		for (const FCastleV4Part& Slice : Deck.Layout->FoundationSlices)
		{
			if (Slice.Cells.Num() != 1) { return Reject(Building, Error, Slice.Node.ToString() + TEXT(" 的 Room 底盘切片未指定唯一格")); }
			if (Slice.Cells[0] != Deck.Cell) { continue; }
			if (!Slice.Mesh) { return Reject(Building, Error, Slice.Node.ToString() + TEXT(" 的 Room 底盘切片缺失")); }
			if (!AtSharedDeckEdge(Slice, Deck, Field, Cell) && !AddPart(Building, Slice, Frame, Plan, Error)) { return false; }
		}
		return true;
	}

	bool AddFrame(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FCastlePCGSemanticField& Field,
		const FRoomSample& Sample, const FIntVector& Cell, FCastlePCGApplyPlan& Plan, FString& Error)
	{
		const FTransform Frame = InRoom(Board, Building, Sample);
		const UCastleHachimisV4Layout& Layout = *Sample.Layout;
		const bool bExposed = Field.Cells.FindChecked(Cell).Roof == ECastlePCGRoof::Keep;
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
				if (OwnRoofKey(Part, Sample, Cell, Field) && !AddPart(Building, Part, Frame, Plan, Error)) { return false; }
			}
			else if ((Group == TEXT("Structure") || Group == TEXT("WallJunction") || Group == TEXT("Furniture"))
				&& CastlePCGSemantics::RoomFramePartOwner(Layout, Part) == Sample.Cell)
			{
				// 梯井属于楼板＋梯套件的装配状态。家具不因任何洞口状态被隐藏或替换。
				if (!AddPart(Building, Part, Frame, Plan, Error)) { return false; }
			}
		}
		return true;
	}

	bool AddWall(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
		const FIntVector& Cell, const FIntVector& Side, const FRoomSample& Sample, const FCastlePCGSideFaceSpec& Face,
		FCastlePCGApplyPlan& Plan, FString& Error)
	{
		// 合并贯通或另一侧拥有共享墙的结果已由 Planar 规则写入。
		if (!Face.bOwnsWall || Face.State == ECastlePCGSide::MergedOpen || (!Face.bHasNeighbor && !Sample.Layout)) { return true; }
		const bool bOpen = Face.IsDoor();
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
		return true;
	}
}

bool CastleV4PCG::PrepareRoom(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
	const FCastlePCGSemanticField& Field, TConstArrayView<FCastlePCGPartAnnotation> Original, FCastlePCGApplyPlan& Plan, FString& Error)
{
	using namespace CastleRoomPCG;
	const FIntVector Cell = Building.GetOccupiedCoordinates()[0];
	const FCastlePCGCellSpec& Room = Field.Cells.FindChecked(Cell);
	const bool bIncomingHatch = Room.Deck == ECastlePCGDeck::Hatch;
	// 普通房独立拥有当前底盘；通梯时由上层接口的 UpperFloor 组接管。
	if (Room.Deck != ECastlePCGDeck::Solid || Room.RoomDeck.Layout != Building.HachimisLayout)
	{
		for (const auto& Part : Original) { Plan.Visual.Hidden.Add(Part.Component); }
		if (!bIncomingHatch && !AddDeck(Board, Building, Field, Room.RoomDeck, Cell, Plan, Error)) { return false; }
	}
	if (Room.RoomFrame.Layout && !AddFrame(Board, Building, Field, Room.RoomFrame, Cell, Plan, Error)) { return false; }
	for (const ECastlePCGFace Direction : FCastlePCGBoardQuery::Sides)
	{
		const FCastlePCGSideFaceSpec& Face = Field.SideFaces.FindChecked({Cell, Direction});
		const FIntVector Side = CastlePCGSemantics::FaceOffset(Direction);
		if (!AddWall(Board, Building, Rules, Cell, Side, Room.RoomFrame, Face, Plan, Error)) { return false; }
	}
	return true;
}
