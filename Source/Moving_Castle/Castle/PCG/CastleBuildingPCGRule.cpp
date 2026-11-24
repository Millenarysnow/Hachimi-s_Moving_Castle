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
		Field.Cells.FindChecked(Selected + Query.Up).Deck = ECastlePCGDeck::Hatch;
	}
	return true;
}

FString FCastlePCGTarget::StableKey() const
{
	// 显式排序消除 TMap、棋盘插入顺序和 UObject 地址的影响。
	TArray<FString> Keys;
	for (const auto& Pair : Ceilings) { Keys.Add(FString::Printf(TEXT("Z:%d,%d,%d:%d"), Pair.Key.X, Pair.Key.Y, Pair.Key.Z, Pair.Value)); }
	for (const auto& Face : Faces) { Keys.Add(FString::Printf(TEXT("F:%d,%d,%d:%d:%d"), Face.Cell.X, Face.Cell.Y, Face.Cell.Z, static_cast<int32>(Face.Face), Face.bConnected)); }
	Keys.Sort();
	return FString::Join(Keys, TEXT(";"));
}

bool UCastlePCGVerticalRule::Evaluate(const FCastlePCGRuleContext& Context, FCastlePCGTarget& Out, FString& Error) const
{
	TMap<ACastleBuildingBase*, TArray<FIntVector>> ConnectedPairs;
	for (const auto& Adj : Context.Adjacencies)
	{
		if (Adj.Offset != FIntVector(0, 0, 1)) { continue; }
		Out.Ceilings.Add(Adj.FirstCell, 1);
		if (Adj.bCanTraverse) { ConnectedPairs.FindOrAdd(Adj.SecondBuilding).Add(Adj.FirstCell); }
	}
	for (auto& Pair : ConnectedPairs)
	{
		Pair.Value.Sort(CastlePCGSemantics::CellLess);
		FIntVector Selected = Pair.Value[0];
		if (Context.bUseLadderNoise)
		{
			if (!Context.NoiseProvider || !Context.NoiseProvider->Implements<UCastlePCGNoiseProvider>() || Context.LadderChannel.IsNone())
			{ Error = TEXT("梯子择格缺少有效噪声接口或通道。"); return false; }
			float Best = -1.0f;
			for (const auto& Cell : Pair.Value)
			{
				const float Noise = ICastlePCGNoiseProvider::Execute_SampleNoise(Context.NoiseProvider, Context.Seed, Cell, Context.LadderChannel);
				if (!FMath::IsFinite(Noise) || Noise < 0.0f || Noise >= 1.0f)
				{ Error = TEXT("梯子噪声接口返回了 [0,1) 之外的值。"); return false; }
				if (Noise > Best) { Best = Noise; Selected = Cell; }
			}
		}
		Out.Ceilings[Selected] = 2;
	}
	return true;
}

bool UCastlePCGRuleSetDataAsset::ValidateConfiguration(FString& Error) const
{
	if (!VariantLV1ActorClass || !VariantLV2ActorClass || !FlatFloorSlabMesh)
	{ Error = FString::Printf(TEXT("规则集 %s 缺少 LV1/LV2 变体类或封闭平楼板。"), *GetPathName()); return false; }
	if (LadderSelectionNoiseChannel.IsNone() || !FMath::IsFinite(BridgeWidth) || BridgeWidth <= 70.0 || BridgeWidth > 500.0
		|| !FMath::IsFinite(FloorThickness) || FloorThickness <= 0.0 || !FMath::IsFinite(BarrierHeight) || BarrierHeight < 100.0)
	{ Error = FString::Printf(TEXT("规则集 %s 的通道、桥宽、厚度或阻隔高度非法。"), *GetPathName()); return false; }
	if (Rules.IsEmpty()) { Error = FString::Printf(TEXT("规则集 %s 未配置语义规则。"), *GetPathName()); return false; }
	bool bPlanar = false;
	bool bVertical = false;
	bool bRoomForm = false;
	for (const UCastleBuildingPCGRule* Rule : Rules)
	{
		if (!IsValid(Rule) || Rule->GetStage() > ECastlePCGRuleStage::Detail)
		{
			Error = FString::Printf(TEXT("规则集 %s 含空规则或非法阶段：%s。"), *GetPathName(), *GetPathNameSafe(Rule));
			return false;
		}
		// 平面语义必须先于使用它的垂直接口规则；不按类名暗中改写规则顺序。
		bPlanar |= Rule->IsA<UCastlePCGPlanarRule>();
		bVertical |= bPlanar && Rule->IsA<UCastlePCGVerticalRule>();
		bRoomForm |= Rule->IsA<UCastlePCGRoomFormRule>();
	}
	if (!bPlanar || !bVertical || !bRoomForm)
	{
		Error = FString::Printf(TEXT("规则集 %s 必须配齐 Planar、Vertical、RoomForm，且 Planar 须位于 Vertical 前。"), *GetPathName());
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
	for (const ECastlePCGRuleStage Stage : {ECastlePCGRuleStage::Topology, ECastlePCGRuleStage::Structure, ECastlePCGRuleStage::Detail})
	for (const UCastleBuildingPCGRule* Rule : Rules)
	{
		if (Rule->GetStage() != Stage) { continue; }
		if (!Rule->Evaluate(Query, Field, Error))
		{
			Error = FString::Printf(TEXT("规则 %s：%s"), *Rule->GetPathName(), *Error);
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
