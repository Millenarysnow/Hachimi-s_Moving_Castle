#include "CastleBuildingPCGRule.h"
#include "CastlePCGSemantics.h"
#include "CastleDeterministicPCGNoise.h"
#include "Engine/StaticMesh.h"
#include "HachimisV4/CastleHachimisV4Layout.h"

namespace
{
	/** 水平和垂直均遵守同色或同号可通行。 */
	bool CanTraverse(const FCastlePCGBoardCell& A, const FCastlePCGBoardCell& B)
	{
		return A.Color == B.Color || A.Number == B.Number;
	}

	/** 规则不能移除已占格或生成棋盘外的格面和层界，以免装配器读到缺漏语义。 */
	bool ValidateDomains(const FCastlePCGBoardQuery& Query, const FCastlePCGSemanticField& Field, bool bRequireFaces,
		bool bRequireVerticalConsistency, FString& Error)
	{
		if (Field.Cells.Num() != Query.OrderedCells().Num()
			|| (bRequireFaces && Field.SideFaces.Num() != Query.OrderedCells().Num() * UE_ARRAY_COUNT(Query.Sides)))
		{
			Error = TEXT("语义格或四向侧面的数量与正式占格不一致。");
			return false;
		}
		for (const FIntVector& Cell : Query.OrderedCells())
		{
			const FCastlePCGCellSpec* Spec = Field.Cells.Find(Cell);
			if (!Spec || Spec->Roof > ECastlePCGRoof::Override || Spec->Deck > ECastlePCGDeck::Merged)
			{
				Error = FString::Printf(TEXT("格 %s 缺少有效格体语义。"), *Cell.ToString());
				return false;
			}
			if (bRequireFaces)
			{
				for (const ECastlePCGFace Direction : Query.Sides)
				{
					const FCastlePCGSideFaceSpec* Side = Field.SideFaces.Find({Cell, Direction});
					if (!Side || Side->State > ECastlePCGSide::MainTowerDoor)
					{
						Error = FString::Printf(TEXT("格 %s 的 %d 号侧面缺少有效语义。"), *Cell.ToString(), int32(Direction));
						return false;
					}
				}
			}
			if (bRequireVerticalConsistency)
			{
				const FCastlePCGVerticalSpec* Incoming = Field.VerticalInterfaces.Find(Cell - Query.Up);
				const bool bIncomingLadder = Incoming && Incoming->State == ECastlePCGVertical::Ladder;
				if ((Spec->Deck == ECastlePCGDeck::Hatch) != bIncomingLadder)
				{
					Error = FString::Printf(TEXT("格 %s 的底盘梯洞与下方层界不一致。"), *Cell.ToString());
					return false;
				}
			}
		}
		for (const auto& Interface : Field.VerticalInterfaces)
		{
			const FCastlePCGBoardCell* Lower = Query.Find(Interface.Key);
			const FCastlePCGBoardCell* Upper = Query.FindOffset(Interface.Key, Query.Up);
			if (!Lower || !Upper || Lower->BuildingRoot == Upper->BuildingRoot
				|| Interface.Value.State > ECastlePCGVertical::Ladder)
			{
				Error = FString::Printf(TEXT("格 %s 的层界语义无效。"), *Interface.Key.ToString());
				return false;
			}
		}
		return true;
	}

	/** 表现阶段可改屋面和饰件，但不能改功能几何、梯位或墙面通行。 */
	bool DetailKeepsGameplay(const FCastlePCGSemanticField& Before, const FCastlePCGSemanticField& After)
	{
		if (Before.VerticalInterfaces.Num() != After.VerticalInterfaces.Num()) { return false; }
		for (const auto& Entry : Before.VerticalInterfaces)
		{
			const FCastlePCGVerticalSpec* Spec = After.VerticalInterfaces.Find(Entry.Key);
			if (!Spec || Spec->State != Entry.Value.State || Spec->Kit != Entry.Value.Kit || Spec->BoardYaw != Entry.Value.BoardYaw) { return false; }
		}
		for (const auto& Entry : Before.SideFaces)
		{
			const FCastlePCGSideFaceSpec& Old = Entry.Value;
			const FCastlePCGSideFaceSpec& New = After.SideFaces.FindChecked(Entry.Key);
			if (Old.State != New.State || Old.bHasNeighbor != New.bHasNeighbor || Old.bNeighborLegacy != New.bNeighborLegacy
				|| Old.bOwnsWall != New.bOwnsWall) { return false; }
		}
		for (const auto& Entry : Before.Cells)
		{
			const FCastlePCGCellSpec& Old = Entry.Value;
			const FCastlePCGCellSpec& New = After.Cells.FindChecked(Entry.Key);
			const auto SameRoomSample = [](const FCastlePCGRoomSample& A, const FCastlePCGRoomSample& B)
			{
				return A.Layout == B.Layout && A.Cell == B.Cell && A.Origin == B.Origin && A.bTurn == B.bTurn;
			};
			if (Old.Deck != New.Deck || !SameRoomSample(Old.RoomDeck, New.RoomDeck) || !SameRoomSample(Old.RoomFrame, New.RoomFrame))
			{
				return false;
			}
			if ((Old.Roof == ECastlePCGRoof::Override && Old.RoofOverride.bAffectsNavigation)
				|| (New.Roof == ECastlePCGRoof::Override && New.RoofOverride.bAffectsNavigation))
			{
				if (Old.Roof != New.Roof || Old.RoofOverride.bAffectsNavigation != New.RoofOverride.bAffectsNavigation
					|| Old.RoofOverride.NavigationExtent != New.RoofOverride.NavigationExtent
					|| !Old.RoofOverride.AnchorTransform.Equals(New.RoofOverride.AnchorTransform, 0.0)) { return false; }
			}
		}
		for (const auto& Entry : After.GetAttachments())
		{
			if (Entry.Value.bAffectsNavigation && !Before.GetAttachments().Contains(Entry.Key)) { return false; }
		}
		return true;
	}
}

bool UCastlePCGPlanarRule::Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const
{
	for (const FIntVector& Cell : Query.OrderedCells())
	{
		const FCastlePCGBoardCell& Owner = *Query.Find(Cell);
		for (const ECastlePCGFace Face : Query.Sides)
		{
			const FIntVector Side = CastlePCGSemantics::FaceOffset(Face);
			const FCastlePCGBoardCell* Neighbor = Query.FindOffset(Cell, Side);
			FCastlePCGSideFaceSpec& Spec = Field.SideFaces.Add({Cell, Face});
			if (Query.IsMainTowerDoorFace(Cell, Side))
			{
				Spec.State = ECastlePCGSide::MainTowerDoor;
			}
			else if (Neighbor)
			{
				const bool bTwoRooms = Owner.bRoom && Neighbor->bRoom;
				const bool bSameOwner = Owner.BuildingRoot == Neighbor->BuildingRoot;
				Spec.bHasNeighbor = !bSameOwner;
				Spec.bNeighborLegacy = !Neighbor->Layout;
				if (bSameOwner || (bTwoRooms && Owner.Color == Neighbor->Color))
				{
					// 同一建筑内面不造墙。同色房还凭此语义去掉底盘的双包边。
					Spec.State = ECastlePCGSide::MergedOpen;
					Spec.bOwnsWall = false;
				}
				else
				{
					Spec.State = CanTraverse(Owner, *Neighbor) ? ECastlePCGSide::OpenDoor : ECastlePCGSide::Closed;
					// 异色房共享一面墙。通行状态仍在两侧保留，供连通分量查询。
					Spec.bOwnsWall = !bTwoRooms || CastlePCGSemantics::CellLess(Cell, Cell + Side);
				}
			}
		}
	}
	return true;
}

bool UCastlePCGVerticalRule::Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const
{
	// 两层同为普通房时按水平分量配对；混接和功能塔按两座独立建筑配对。
	const TMap<FIntVector, FIntVector> Rooms = Query.HorizontalComponents(Field, true);
	TMap<FIntVector, TMap<FIntVector, TArray<FIntVector>>> Pairs;
	for (const FIntVector& Cell : Query.OrderedCells())
	{
		const FCastlePCGBoardCell& Lower = *Query.Find(Cell);
		const FCastlePCGBoardCell* Upper = Query.FindOffset(Cell, Query.Up);
		if (!Upper || Lower.BuildingRoot == Upper->BuildingRoot) { continue; }
		Field.VerticalInterfaces.Add(Cell, {ECastlePCGVertical::Closed});
		Field.Cells.FindChecked(Cell).Roof = ECastlePCGRoof::Hidden;
		if (!CanTraverse(Lower, *Upper)) { continue; }
		const bool bTwoRooms = Lower.bRoom && Upper->bRoom;
		const FIntVector LowerKey = bTwoRooms ? Rooms.FindChecked(Cell) : Lower.BuildingRoot;
		const FIntVector UpperKey = bTwoRooms ? Rooms.FindChecked(Cell + Query.Up) : Upper->BuildingRoot;
		Pairs.FindOrAdd(LowerKey).FindOrAdd(UpperKey).Add(Cell);
	}
	for (const auto& LowerPair : Pairs)
	for (const auto& UpperPair : LowerPair.Value)
	{
		// 候选已按坐标插入；噪声同分仍由字典序决定。
		FIntVector Selected = UpperPair.Value[0];
		if (Query.Config().bUseNoiseForLadderCellSelection)
		{
			float Best = -1.0f;
			for (const FIntVector& Cell : UpperPair.Value)
			{
				float Score;
				if (!Query.SampleNoise(Cell, Query.Config().LadderSelectionNoiseChannel, Score, Error)) { return false; }
				if (Score > Best) { Best = Score; Selected = Cell; }
			}
		}
		const FCastlePCGBoardCell& Lower = *Query.Find(Selected);
		FCastlePCGVerticalSpec& Interface = Field.VerticalInterfaces.FindChecked(Selected);
		Interface.State = ECastlePCGVertical::Ladder;
		Interface.Kit = Lower.Layout ? Lower.Layout->Ladder.Get() : nullptr;
		Interface.BoardYaw = Lower.Layout && !Lower.bRoom ? Lower.BoardYaw : 0.0;
		if (Lower.Layout && Lower.Layout->Grid.Z == 2 && Lower.Layout->InternalHatch.Opening.IsValid)
		{
			const FBox& InternalOpening = Lower.Layout->InternalHatch.Opening;
			// 检查开口坐标后再用其中心判定格位，避免非有限坐标绕开净空条件。
			if (InternalOpening.Min.ContainsNaN() || InternalOpening.Max.ContainsNaN())
			{
				Error = FString::Printf(TEXT("格 %s 的布局 %s 内梯开口坐标无效。"), *Selected.ToString(), *Lower.Layout->GetPathName());
				return false;
			}
			const FVector CellCenter = Lower.CellInBuilding.GetLocation();
			const FVector OpeningCenter = InternalOpening.GetCenter();
			const double HalfPitch = Lower.Layout->CellPitch * 0.5;
			// 内梯洞只约束同一作者 XY 格的出梯口。其他格继续使用建筑本身的朝向。
			if (FMath::Abs(CellCenter.X - OpeningCenter.X) < HalfPitch
				&& FMath::Abs(CellCenter.Y - OpeningCenter.Y) < HalfPitch)
			{
				if (!IsValid(Interface.Kit))
				{
					Error = FString::Printf(TEXT("格 %s 的布局 %s 缺少有效外梯套件。"),
						*Selected.ToString(), *Lower.Layout->GetPathName());
					return false;
				}
				// 以城堡 Recast 的 35 cm AgentRadius 扩出水平净空，确保入口可落在有支撑的楼板上。
				const FBox Clearance = InternalOpening.ExpandBy(FVector(35.0, 35.0, 0.0));
				bool bFoundOrientation = false;
				for (int32 Quarter = 0; Quarter < 4; ++Quarter)
				{
					const double CandidateYaw = Lower.BoardYaw + Quarter * 90.0;
					const FTransform LadderInBuilding = FTransform(FRotator(0, CandidateYaw, 0)) * Lower.CellInBuilding;
					const FCastleV4Hatch& Hatch = Interface.Kit->Hatch;
					if (!Clearance.IsInsideOrOnXY(LadderInBuilding.TransformPosition(Hatch.BottomStand))
						&& !Clearance.IsInsideOrOnXY(LadderInBuilding.TransformPosition(Hatch.LadderBottom)))
					{
						// 上下两侧装配都读取这一朝向，带洞楼板和实体梯会一起转动。
						Interface.BoardYaw = CandidateYaw;
						bFoundOrientation = true;
						break;
					}
				}
				if (!bFoundOrientation)
				{
					Error = FString::Printf(TEXT("格 %s 的布局 %s 与外梯套件 %s 在四个朝向均冲突。"),
						*Selected.ToString(), *Lower.Layout->GetPathName(), *Interface.Kit->GetPathName());
					return false;
				}
			}
		}
		Field.Cells.FindChecked(Selected + Query.Up).Deck = ECastlePCGDeck::Hatch;
	}
	return true;
}

bool UCastlePCGRuleSetDataAsset::ValidateConfiguration(FString& Error) const
{
	// 旧模板只在旧适配器实际参与本轮时验证。正式规则只要求共享参数和规则配置。
	if (LadderSelectionNoiseChannel.IsNone() || !FMath::IsFinite(BridgeWidth) || BridgeWidth <= 70.0 || BridgeWidth > 500.0
		|| !FMath::IsFinite(FloorThickness) || FloorThickness <= 0.0 || !FMath::IsFinite(BarrierHeight) || BarrierHeight < 100.0)
	{ Error = FString::Printf(TEXT("规则集 %s 的通道、桥宽、厚度或阻隔高度非法。"), *GetPathName()); return false; }
	if (Rules.IsEmpty()) { Error = FString::Printf(TEXT("规则集 %s 未配置语义规则。"), *GetPathName()); return false; }
	int32 PlanarIndex = INDEX_NONE;
	int32 VerticalIndex = INDEX_NONE;
	int32 RoomFormIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Rules.Num(); ++Index)
	{
		const UCastleBuildingPCGRule* Rule = Rules[Index];
		if (!IsValid(Rule) || Rule->GetStage() > ECastlePCGRuleStage::Detail)
		{
			Error = FString::Printf(TEXT("规则集 %s 含空规则或非法阶段：%s。"), *GetPathName(), *GetPathNameSafe(Rule));
			return false;
		}
		// 每种必需规则只执行一次。子类可替换实现，但不能换阶段或重复写入同一个基础域。
		auto Register = [&](int32& RoleIndex, ECastlePCGRuleStage Stage)
		{
			if (RoleIndex != INDEX_NONE || Rule->GetStage() != Stage)
			{
				Error = FString::Printf(TEXT("规则集 %s 的必需规则 %s 重复或阶段错误。"), *GetPathName(), *Rule->GetPathName());
				return false;
			}
			RoleIndex = Index;
			return true;
		};
		if (Rule->IsA<UCastlePCGPlanarRule>() && !Register(PlanarIndex, ECastlePCGRuleStage::Topology)) { return false; }
		if (Rule->IsA<UCastlePCGVerticalRule>() && !Register(VerticalIndex, ECastlePCGRuleStage::Topology)) { return false; }
		if (Rule->IsA<UCastlePCGRoomFormRule>() && !Register(RoomFormIndex, ECastlePCGRuleStage::Structure)) { return false; }
	}
	if (PlanarIndex == INDEX_NONE || VerticalIndex == INDEX_NONE || RoomFormIndex == INDEX_NONE)
	{
		Error = FString::Printf(TEXT("规则集 %s 必须配齐 Planar、Vertical、RoomForm。"), *GetPathName());
		return false;
	}
	// 必需规则保留明确的依赖顺序，扩展规则仍按各自声明的阶段执行。
	if (PlanarIndex >= VerticalIndex || VerticalIndex >= RoomFormIndex)
	{
		Error = FString::Printf(TEXT("规则集 %s 的必需规则顺序无效。Planar 须位于 Vertical 前，Vertical 须位于 RoomForm 前。"), *GetPathName());
		return false;
	}
	return true;
}

bool UCastlePCGRuleSetDataAsset::Solve(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Out, FString& Error) const
{
	if (!ValidateConfiguration(Error)) { return false; }
	// 求解失败时保留调用者原有场；后续阶段只读取这轮已经完成的结果。
	FCastlePCGSemanticField Field;
	for (const FIntVector& Cell : Query.OrderedCells()) { Field.Cells.Add(Cell); }
	bool bHasPlanarOutput = false;
	bool bHasVerticalOutput = false;
	for (const ECastlePCGRuleStage Stage : {ECastlePCGRuleStage::Topology, ECastlePCGRuleStage::Structure, ECastlePCGRuleStage::Detail})
	for (const UCastleBuildingPCGRule* Rule : Rules)
	{
		if (Rule->GetStage() != Stage) { continue; }
		TOptional<FCastlePCGSemanticField> Previous;
		if (Stage == ECastlePCGRuleStage::Detail) { Previous.Emplace(Field); }
		bHasPlanarOutput |= Rule->IsA<UCastlePCGPlanarRule>();
		bHasVerticalOutput |= Rule->IsA<UCastlePCGVerticalRule>();
		if (!Rule->Evaluate(Query, Field, Error)
			|| !ValidateDomains(Query, Field, bHasPlanarOutput, bHasVerticalOutput, Error))
		{
			Error = FString::Printf(TEXT("规则 %s：%s"), *Rule->GetPathName(), *Error);
			return false;
		}
		if (Previous.IsSet() && !DetailKeepsGameplay(Previous.GetValue(), Field))
		{
			Error = FString::Printf(TEXT("Detail 规则 %s 试图改变玩法语义。"), *Rule->GetPathName());
			return false;
		}
		for (const auto& Entry : Field.Cells)
		{
			if (Entry.Value.Roof == ECastlePCGRoof::Override && !Entry.Value.RoofOverride.Validate(Error))
			{
				Error = FString::Printf(TEXT("规则 %s 的格 %s：%s"), *Rule->GetPathName(), *Entry.Key.ToString(), *Error);
				return false;
			}
		}
	}
	Out = MoveTemp(Field);
	return true;
}
