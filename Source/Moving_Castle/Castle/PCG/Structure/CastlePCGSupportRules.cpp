#include "CastlePCGSupportRules.h"

#include "Algo/Find.h"
#include "Castle/PCG/CastlePCGSemantics.h"
#include "Engine/StaticMesh.h"

TConstArrayView<FName> CastlePCGSupport::Families()
{
	static const FName Names[] = {TEXT("Archer"), TEXT("Balloon"), TEXT("Cannon"), TEXT("Farm"), TEXT("Hive"), TEXT("Shield")};
	return MakeArrayView(Names);
}

TConstArrayView<ECastlePCGBraceKind> CastlePCGSupport::BraceKinds()
{
	using enum ECastlePCGBraceKind;
	static constexpr ECastlePCGBraceKind Kinds[] = {Cantilever, CornerFan, ThroughArch, DeepCorbel, OpposedSpan};
	return MakeArrayView(Kinds);
}

FName CastlePCGSupport::BraceName(ECastlePCGBraceKind Kind)
{
	return FName(*StaticEnum<ECastlePCGBraceKind>()->GetNameStringByValue(static_cast<int64>(Kind)));
}

FQuat CastlePCGSupport::OutwardRotation(const FIntVector& Direction)
{
	return FQuat(FVector::UpVector, FMath::DegreesToRadians(FVector(Direction).Rotation().Yaw - 90.0));
}

namespace
{
	const FIntVector Axes[] = {FIntVector(1, 0, 0), FIntVector(0, 1, 0)};
	const FIntVector Directions[] = {FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0)};

	/** D8 同时校验未在本轮棋盘出现的款式，防止坏资产延迟到玩家放置时才暴露。 */
	bool ValidateKit(const UCastleBuildingPCGRule& Rule, FName Family, const UCastlePCGAttachmentKit* Kit,
		TConstArrayView<FName> Groups, FString& Error)
	{
		FCastlePCGAttachmentSpec Spec;
		Spec.Kit = Kit;
		FString Reason;
		if (Spec.Validate(Reason) && Kit->Parts.Num() == Groups.Num())
		{
			for (int32 Index = 0; Index < Groups.Num(); ++Index)
			{
				const FCastleV4Part& Part = Kit->Parts[Index];
				const FString ExpectedMesh = FString::Printf(TEXT("SM_HC515_%s_%s"), *Family.ToString(), *Groups[Index].ToString());
				if (Part.Group != Groups[Index] || Part.Mesh->GetName() != ExpectedMesh
					|| Part.Mesh->GetStaticMaterials().IsEmpty() || Part.Mesh->GetStaticMaterials().ContainsByPredicate(
						[](const FStaticMaterial& Slot) { return !IsValid(Slot.MaterialInterface); }))
				{
					Reason = FString::Printf(TEXT("零件 %s 的网格、分组或材质无效（应为 %s）。"),
						*Part.Node.ToString(), *ExpectedMesh);
					break;
				}
			}
			if (Reason.IsEmpty()) { return true; }
		}
		Error = FString::Printf(TEXT("规则 %s 的 %s 家族 %s 套件（%s）无效。%s"),
			*Rule.GetPathName(), *Family.ToString(), *Groups[0].ToString(), *GetPathNameSafe(Kit), *Reason);
		return false;
	}

	/** 主塔按柱列承托，其他支撑必须来自实际完成放置的侧下格。 */
	bool HasBearing(const FCastlePCGBoardQuery& Query, const FIntVector& Cell, const FIntVector& Wall)
	{
		return Query.IsMainTowerColumn(Cell + Wall) || Query.Find(Cell + Wall - Query.Up) != nullptr;
	}

	bool HasOppositeBearings(const FCastlePCGBoardQuery& Query, const FIntVector& Cell, const FIntVector& Axis)
	{
		return HasBearing(Query, Cell, Axis) && HasBearing(Query, Cell, Axis * -1);
	}
}

bool UCastlePCGOverhangBraceRule::ValidateKits(FString& Error) const
{
	for (const FName Family : CastlePCGSupport::Families())
	{
		const FCastlePCGBraceKits* Kits = FamilyKits.Find(Family);
		for (const ECastlePCGBraceKind Kind : CastlePCGSupport::BraceKinds())
		{
			const FName Group = CastlePCGSupport::BraceName(Kind);
			const auto* Kit = Kits ? Kits->ByKind.Find(Kind) : nullptr;
			if (!ValidateKit(*this, Family, Kit ? Kit->Get() : nullptr, MakeArrayView(&Group, 1), Error)) { return false; }
		}
	}
	return true;
}

bool UCastlePCGHorizontalLevel2StepRule::ValidateKits(FString& Error) const
{
	const FName SlopeGroups[] = {TEXT("Slope"), TEXT("WallApron")};
	const FName DeckGroups[] = {TEXT("Deck")};
	for (const FName Family : CastlePCGSupport::Families())
	{
		const FCastlePCGStepKits* Kits = FamilyKits.Find(Family);
		if (!ValidateKit(*this, Family, Kits ? Kits->Slope.Get() : nullptr, MakeArrayView(SlopeGroups), Error)
			|| !ValidateKit(*this, Family, Kits ? Kits->Deck.Get() : nullptr, MakeArrayView(DeckGroups), Error))
		{
			return false;
		}
	}
	return true;
}

bool UCastlePCGOverhangBraceRule::Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const
{
	using enum ECastlePCGBraceKind;
	TSet<FIntVector> Available;
	for (const FIntVector& Cell : Query.OrderedCells())
	{
		const FCastlePCGBoardCell& Data = *Query.Find(Cell);
		if (!Data.bRoom && Data.Layout && CastlePCGSupport::Families().Contains(Data.Layout->Family)
			&& Cell.Z > Query.GetGroundLayer() && Query.IsOverhanging(Cell))
		{
			Available.Add(Cell);
		}
	}
	const auto Place = [&](const FIntVector& Cell, const FIntVector& Outward, ECastlePCGBraceKind Kind)
	{
		const FName Family = Query.Find(Cell)->Layout->Family;
		const FCastlePCGBraceKits* Kits = FamilyKits.Find(Family);
		const auto* Kit = Kits ? Kits->ByKind.Find(Kind) : nullptr;
		FCastlePCGAttachmentSpec Spec;
		Spec.Kit = Kit ? Kit->Get() : nullptr;
		// 局部 -Y 指向主承托墙，挂点也从格底中心沿此方向移到墙沿中点。
		Spec.AnchorTransform = FTransform(CastlePCGSupport::OutwardRotation(Outward), -FVector(Outward) * Query.GetCellPitch() * 0.5);
		return Field.AddAttachment({Cell, TEXT("CellBottom"), TEXT("OverhangBrace")}, Spec, Error);
	};

	// 优先保留有真实双端承托的跨度。仅沿正轴搜索，所有者固定在负端首格。
	for (const FIntVector& Near : Query.OrderedCells())
	{
		if (!Available.Contains(Near)) { continue; }
		for (const FIntVector& Axis : Axes)
		{
			const FIntVector Far = Near + Axis;
			const FIntVector Transverse(-Axis.Y, Axis.X, 0);
			if (!Available.Contains(Far) || !HasBearing(Query, Near, Axis * -1) || !HasBearing(Query, Far, Axis)
				|| HasOppositeBearings(Query, Near, Transverse) || HasOppositeBearings(Query, Far, Transverse))
			{
				continue;
			}
			if (!Place(Near, Axis, OpposedSpan)) { return false; }
			Available.Remove(Near);
			Available.Remove(Far);
			break;
		}
	}
	// 单端深挑覆盖两个悬空格。外格需要横向双支撑时，保留该格的单格选型。
	for (const FIntVector& Near : Query.OrderedCells())
	{
		if (!Available.Contains(Near)) { continue; }
		for (const FIntVector& Direction : Directions)
		{
			const FIntVector Far = Near + Direction;
			const FIntVector Transverse(-Direction.Y, Direction.X, 0);
			if (!Available.Contains(Far) || !HasBearing(Query, Near, Direction * -1) || HasBearing(Query, Far, Direction)
				|| HasOppositeBearings(Query, Far, Transverse))
			{
				continue;
			}
			if (!Place(Near, Direction, DeepCorbel)) { return false; }
			Available.Remove(Near);
			Available.Remove(Far);
			break;
		}
	}
	for (const FIntVector& Cell : Query.OrderedCells())
	{
		if (!Available.Contains(Cell)) { continue; }
		// 三面或四面均先取对穿拱，对向轴固定按 +X、+Y 比较。
		if (const FIntVector* Axis = Algo::FindByPredicate(Axes, [&](const FIntVector& D) { return HasOppositeBearings(Query, Cell, D); }))
		{
			if (!Place(Cell, *Axis, ThroughArch)) { return false; }
			continue;
		}
		TArray<FIntVector, TInlineAllocator<4>> Bearings;
		for (const FIntVector& Direction : Directions)
		{
			if (HasBearing(Query, Cell, Direction)) { Bearings.Add(Direction); }
		}
		if (Bearings.Num() == 2)
		{
			// 局部 -Y、-X 依次对齐主副承托墙，手性由叉积固定，四种组合只用旋转。
			const int32 Primary = FVector::CrossProduct(FVector(Bearings[0]), FVector(Bearings[1])).Z < 0.0 ? 0 : 1;
			if (!Place(Cell, Bearings[Primary] * -1, CornerFan)) { return false; }
		}
		else if (Bearings.Num() == 1 && !Place(Cell, Bearings[0] * -1, Cantilever)) { return false; }
	}
	return true;
}

bool UCastlePCGHorizontalLevel2StepRule::Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const
{
	for (const FIntVector& Covered : Query.OrderedCells())
	{
		const FCastlePCGBoardCell& Lower = *Query.Find(Covered);
		const FIntVector Upper = Covered + Query.Up;
		const FCastlePCGBoardCell* Higher = Query.Find(Upper);
		if (!Higher || Higher->BuildingCellCount != 1 || Lower.bRoom || !Lower.Layout || Lower.BuildingCellCount != 2
			|| Lower.Layout->Grid != FIntVector(2, 1, 1) || !CastlePCGSupport::Families().Contains(Lower.Layout->Family))
		{
			continue;
		}
		for (const FIntVector& Direction : Directions)
		{
			const FIntVector Exposed = Covered + Direction;
			const FCastlePCGBoardCell* Other = Query.Find(Exposed);
			if (!Other || Other->BuildingRoot != Lower.BuildingRoot || Query.Find(Exposed + Query.Up)) { continue; }
			const FCastlePCGStepKits* Kits = FamilyKits.Find(Lower.Layout->Family);
			if (!Kits || !Kits->Slope || !Kits->Deck)
			{
				Error = FString::Printf(TEXT("格 %s 的 %s 缺少宽边地板或坡顶套件。"), *Covered.ToString(), *Lower.Layout->GetPathName());
				return false;
			}
			FCastlePCGCellSpec& Roof = Field.Cells.FindChecked(Exposed);
			Roof.Roof = ECastlePCGRoof::Override;
			Roof.bOverrideWholeRoof = true;
			Roof.RoofOverride.Kit = Kits->Slope;
			Roof.RoofOverride.AnchorTransform = FTransform(CastlePCGSupport::OutwardRotation(Direction), FVector(0, 0, Query.GetLayerHeight()));
			Roof.RoofOverride.bSelectable = true;
			// 宽边板归上层格，使用下层家族的款式。保留 Topology 阶段的 Hatch 或 Solid 玩法结论。
			FCastlePCGAttachmentSpec& Deck = Field.Cells.FindChecked(Upper).DeckOverride.Emplace();
			Deck.Kit = Kits->Deck;
			Deck.bSelectable = true;
			break;
		}
	}
	return true;
}
