#include "CastlePCGSupportRules.h"

#include "Algo/Find.h"
#include "Castle/PCG/CastlePCGSemantics.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"

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

TConstArrayView<ECastlePCGQuadRoofKind> CastlePCGSupport::QuadRoofKinds()
{
	using enum ECastlePCGQuadRoofKind;
	static constexpr ECastlePCGQuadRoofKind Kinds[] = {Corner, Row, Diagonal, LastBay};
	return MakeArrayView(Kinds);
}

FName CastlePCGSupport::QuadRoofName(ECastlePCGQuadRoofKind Kind)
{
	return FName(*StaticEnum<ECastlePCGQuadRoofKind>()->GetNameStringByValue(static_cast<int64>(Kind)));
}

FQuat CastlePCGSupport::OutwardRotation(const FIntVector& Direction)
{
	return FQuat(FVector::UpVector, FMath::DegreesToRadians(FVector(Direction).Rotation().Yaw - 90.0));
}

namespace
{
	const FIntVector Axes[] = {FIntVector(1, 0, 0), FIntVector(0, 1, 0)};
	const FIntVector Directions[] = {FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0)};

	/** 位序为 (0,0)、(1,0)、(0,1)、(1,1)。掩码 0、15 由调用方保留原拓扑屋面。 */
	struct FQuadRoofPlacement
	{
		ECastlePCGQuadRoofKind Kind;
		int32 Yaw;
	};

	FQuadRoofPlacement QuadRoofPlacement(uint8 OccupiedMask)
	{
		using enum ECastlePCGQuadRoofKind;
		static constexpr FQuadRoofPlacement Placements[] = {
			{Corner, 270},   // 1：仅左下被占。
			{Corner, 0},     // 2：仅右下被占。
			{Row, 0},        // 3：下排被占。
			{Corner, 180},   // 4：仅左上被占。
			{Row, 270},      // 5：左列被占。
			{Diagonal, 0},   // 6：右下和左上被占。
			{LastBay, 0},    // 7：仅右上留空。
			{Corner, 90},    // 8：仅右上被占。
			{Diagonal, 90},  // 9：左下和右上被占。
			{Row, 90},       // 10：右列被占。
			{LastBay, 90},   // 11：仅左上留空。
			{Row, 180},      // 12：上排被占。
			{LastBay, 270},  // 13：仅右下留空。
			{LastBay, 180},  // 14：仅左下留空。
		};
		check(OccupiedMask > 0 && OccupiedMask <= UE_ARRAY_COUNT(Placements));
		return Placements[OccupiedMask - 1];
	}

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

	/** 主塔按柱列承托，普通房只有楼板，其他承托立面必须来自落定的侧下方功能建筑。 */
	bool HasBearing(const FCastlePCGBoardQuery& Query, const FIntVector& Cell, const FIntVector& Wall)
	{
		const FCastlePCGBoardCell* Bearing = Query.Find(Cell + Wall - Query.Up);
		return Query.IsMainTowerColumn(Cell + Wall) || (Bearing && !Bearing->bRoom);
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

bool UCastlePCGQuadRoofOverlayRule::ValidateKits(FString& Error) const
{
	static const FName Shield(TEXT("Shield"));
	static const FName Archer(TEXT("Archer"));
	for (const FName Family : CastlePCGSupport::Families())
	{
		const FCastlePCGQuadRoofKits* Kits = FamilyKits.Find(Family);
		if (!Kits || Kits->ByKind.Num() != CastlePCGSupport::QuadRoofKinds().Num()
			|| (Kits->bIsPlaceholder && Family != Shield))
		{
			Error = FString::Printf(TEXT("规则 %s 的 %s 家族须配置四款屋面，只有盾塔可声明箭塔占位。"),
				*GetPathName(), *Family.ToString());
			return false;
		}
		// 因家族校验顺序固定，盾塔检查时箭塔四款引用均已验证。
		const FName MeshFamily = Kits->bIsPlaceholder ? Archer : Family;
		for (const ECastlePCGQuadRoofKind Kind : CastlePCGSupport::QuadRoofKinds())
		{
			const FName Group = CastlePCGSupport::QuadRoofName(Kind);
			const FString MeshName = FString::Printf(TEXT("SM_HC515_L4H_%s_%s"), *MeshFamily.ToString(), *Group.ToString());
			const auto* KitRef = Kits->ByKind.Find(Kind);
			const UCastlePCGAttachmentKit* Kit = KitRef ? KitRef->Get() : nullptr;
			FCastlePCGAttachmentSpec Spec;
			Spec.Kit = Kit;
			FString Reason;
			if (Spec.Validate(Reason) && Kit->Parts.Num() == 1)
			{
				const FCastleV4Part& Part = Kit->Parts[0];
				const auto& Materials = Part.Mesh->GetStaticMaterials();
				const bool bPaletteValid = Materials.Num() == 1 && IsValid(Materials[0].MaterialInterface)
					&& Materials[0].MaterialInterface->GetFName() == TEXT("M_HC515_RoofPalette");
				const bool bReferenceValid = !Kits->bIsPlaceholder || Kit == FamilyKits.FindChecked(Archer).ByKind.FindChecked(Kind);
				if (Part.Group == Group && Part.Mesh->GetName() == MeshName && Part.Mesh->GetNumTriangles(0) > 0
					&& bPaletteValid && bReferenceValid)
				{
					continue;
				}
			}
			Error = FString::Printf(TEXT("规则 %s 的 %s 家族 %s 屋面套件 %s 无效，预期网格 %s 与共享调色板。%s"),
				*GetPathName(), *Family.ToString(), *Group.ToString(), *GetPathNameSafe(Kit), *MeshName, *Reason);
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
			// 外露侧同列必须通天，隔一层的悬挑也会遮住坡顶。
			if (!Other || Other->BuildingRoot != Lower.BuildingRoot || Query.Find(Exposed + Query.Up)
				|| !Query.IsSkyExposed(Exposed)) { continue; }
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
			// 梯井沿用下层家族 FloorLadder 的带孔楼板，无梯时才使用独立的实心宽边板。
			FCastlePCGCellSpec& UpperCell = Field.Cells.FindChecked(Upper);
			if (UpperCell.Deck != ECastlePCGDeck::Hatch)
			{
				FCastlePCGAttachmentSpec& Deck = UpperCell.DeckOverride.Emplace();
				Deck.Kit = Kits->Deck;
				Deck.bSelectable = true;
			}
			break;
		}
	}
	return true;
}

bool UCastlePCGQuadRoofOverlayRule::Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const
{
	const FIntVector Quadrants[] = {FIntVector::ZeroValue, FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(1, 1, 0)};
	for (const FIntVector& Root : Query.OrderedCells())
	{
		const FCastlePCGBoardCell& Building = *Query.Find(Root);
		if (Building.BuildingRoot != Root || Building.bRoom || !Building.Layout
			|| !CastlePCGSupport::Families().Contains(Building.Layout->Family))
		{
			continue;
		}
		const FIntVector Grid = Building.Layout->Grid;
		if (Grid.X != 2 || Grid.Y != 2 || (Grid.Z != 1 && Grid.Z != 2) || Building.BuildingCellCount != 4 * Grid.Z)
		{
			continue;
		}
		// 查询的建筑根是最小占格。四格轮廓旋转后仍为正方形，掩码始终按棋盘坐标编码。
		const FIntVector Anchor = Root + Query.Up * (Grid.Z - 1);
		uint8 OccupiedMask = 0;
		bool bExposed = true;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Quadrants); ++Index)
		{
			const FIntVector Top = Anchor + Quadrants[Index];
			const FCastlePCGBoardCell* TopCell = Query.Find(Top);
			if (!TopCell || TopCell->BuildingRoot != Root)
			{
				Error = FString::Printf(TEXT("建筑 %s（%s）缺少四格顶面占格 %s。"),
					*Root.ToString(), *Building.Layout->GetPathName(), *Top.ToString());
				return false;
			}
			if (Query.Find(Top + Query.Up))
			{
				OccupiedMask |= 1 << Index;
			}
			else if (!Query.IsSkyExposed(Top))
			{
				bExposed = false;
			}
		}
		// 全空、全满或空角上方被悬挑遮挡时，保留拓扑阶段已经决定的显隐状态。
		if (!bExposed || OccupiedMask == 0 || OccupiedMask == 15) { continue; }

		const FQuadRoofPlacement Placement = QuadRoofPlacement(OccupiedMask);
		const FName Family = Building.Layout->Family;
		const FCastlePCGQuadRoofKits* Kits = FamilyKits.Find(Family);
		const auto* KitRef = Kits ? Kits->ByKind.Find(Placement.Kind) : nullptr;
		if (!KitRef || !IsValid(KitRef->Get()))
		{
			Error = FString::Printf(TEXT("建筑 %s（%s）缺少 %s 屋面套件。"), *Root.ToString(),
				*Building.Layout->GetPathName(), *CastlePCGSupport::QuadRoofName(Placement.Kind).ToString());
			return false;
		}
		// 整顶统一替换，但只有最小顶格实例化网格。中心锚点是这四格的平面中心和上层格底。
		for (const FIntVector& Offset : Quadrants)
		{
			FCastlePCGCellSpec& Roof = Field.Cells.FindChecked(Anchor + Offset);
			Roof.bOverrideWholeRoof = true;
			Roof.Roof = ECastlePCGRoof::Hidden;
		}
		FCastlePCGCellSpec& Owner = Field.Cells.FindChecked(Anchor);
		const double HalfPitch = Query.GetCellPitch() * 0.5;
		Owner.Roof = ECastlePCGRoof::Override;
		Owner.RoofOverride.Kit = *KitRef;
		Owner.RoofOverride.AnchorTransform = FTransform(FRotator(0.0, Placement.Yaw, 0.0),
			FVector(HalfPitch, HalfPitch, Query.GetLayerHeight()));
		Owner.RoofOverride.bSelectable = true;
		Owner.RoofOverride.bAffectsNavigation = false;
		if (Kits->bIsPlaceholder)
		{
			UE_LOG(LogMoving_Castle, Warning, TEXT("盾塔建筑 %s：专用四格叠层屋面尚未交付，使用箭塔屋面套件 %s 占位。"),
				*Root.ToString(), *(*KitRef)->GetPathName());
		}
	}
	return true;
}
