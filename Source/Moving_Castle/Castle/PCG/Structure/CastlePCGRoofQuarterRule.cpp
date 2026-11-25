#include "CastlePCGRoofQuarterRule.h"

#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"
#include "Castle/PCG/Structure/CastlePCGSupportRules.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"

namespace CastlePCGRoofQuarters
{
	TArray<FName> FormKeys()
	{
		TArray<FName> Keys = {TEXT("Room_L2V"), TEXT("Room_L3V"), TEXT("Room_L4")};
		for (const FName Family : CastlePCGSupport::Families())
		{
			for (const TCHAR* Form : {TEXT("L1"), TEXT("L2H"), TEXT("L2V"), TEXT("L3H"), TEXT("L3V"), TEXT("L4")})
			{
				Keys.Add(FName(Family.ToString() + TEXT("_") + Form));
			}
			if (Family != TEXT("Balloon") && Family != TEXT("Hive")) { Keys.Add(FName(Family.ToString() + TEXT("_L5"))); }
		}
		return Keys;
	}

	FName FormKey(const UCastleHachimisV4Layout& Layout)
	{
		return FName(Layout.Family.ToString() + TEXT("_") + Layout.Form.ToString());
	}

	bool RequiresFarmSplit(FName Key)
	{
		return Key == TEXT("Farm_L3H") || Key == TEXT("Farm_L4");
	}

	const TCHAR* StateName(ECastlePCGRoofQuarterState State)
	{
		static const TCHAR* Names[] = {TEXT("Original"), TEXT("FlushX"), TEXT("FlushY"), TEXT("FlushXY")};
		return Names[static_cast<uint8>(State)];
	}
}

bool UCastlePCGRoofQuarterRule::ValidateKits(FString& Error) const
{
	using namespace CastlePCGRoofQuarters;
	const auto Keys = FormKeys();
	for (const FName Key : Keys)
	{
		const FCastlePCGRoofQuarterForm* Form = Forms.Find(Key);
		if (!Form || !IsValid(Form->Layout) || FormKey(*Form->Layout) != Key
			|| Form->Pieces.Num() != (RequiresFarmSplit(Key) ? 6 : 4))
		{
			Error = FString::Printf(TEXT("规则 %s 缺少 %s 的正式布局或屋面片数量无效。"), *GetPathName(), *Key.ToString());
			return false;
		}
		const FIntVector Grid = Form->Layout->Grid;
		TSet<FName> Slots;
		for (const FCastlePCGRoofQuarterPiece& Piece : Form->Pieces)
		{
			// 每片只能由本造型顶格持有。四个状态必须通过显式引用指向各自的有效网格。
			if (Piece.Slot.IsNone() || Slots.Contains(Piece.Slot) || Piece.Owner.Z != Grid.Z - 1
				|| Piece.Owner.X < 0 || Piece.Owner.X >= Grid.X || Piece.Owner.Y < 0 || Piece.Owner.Y >= Grid.Y
				|| FMath::Abs(Piece.Outward.X) != 1 || FMath::Abs(Piece.Outward.Y) != 1
				|| Piece.PivotInOwner.ContainsNaN() || Piece.Variants.Num() != 4)
			{
				Error = FString::Printf(TEXT("规则 %s 的 %s 槽 %s 格归属、方向、枢轴或状态数量无效。"),
					*GetPathName(), *Key.ToString(), *Piece.Slot.ToString());
				return false;
			}
			Slots.Add(Piece.Slot);
			for (uint8 Bits = 0; Bits < 4; ++Bits)
			{
				const auto State = static_cast<ECastlePCGRoofQuarterState>(Bits);
				const auto* Ref = Piece.Variants.Find(State);
				FCastlePCGAttachmentSpec Spec;
				Spec.Kit = Ref ? Ref->Get() : nullptr;
				FString Reason;
				if (Spec.Validate(Reason) && Spec.Kit->Parts.Num() == 1)
				{
					const auto& Part = Spec.Kit->Parts[0];
					const auto& Materials = Part.Mesh->GetStaticMaterials();
					if (Part.Local.Equals(FTransform::Identity) && Part.Mesh->GetNumTriangles(0) > 0 && !Materials.IsEmpty()
						&& !Part.Mesh->bHasNavigationData && Part.Mesh->GetBodySetup()
						&& Part.Mesh->GetBodySetup()->CollisionTraceFlag == CTF_UseComplexAsSimple
						&& !Materials.ContainsByPredicate([](const FStaticMaterial& Slot) { return !IsValid(Slot.MaterialInterface); }))
					{
						continue;
					}
				}
				Error = FString::Printf(TEXT("规则 %s 的 %s 槽 %s 状态 %s（%s）缺少有效屋角网格、材质或选中碰撞。%s"),
					*GetPathName(), *Key.ToString(), *Piece.Slot.ToString(), StateName(State), *GetPathNameSafe(Spec.Kit), *Reason);
				return false;
			}
		}
	}
	if (Forms.Num() != Keys.Num())
	{
		Error = FString::Printf(TEXT("规则 %s 含未声明的四分屋顶造型。"), *GetPathName());
		return false;
	}
	return true;
}

bool UCastlePCGRoofQuarterRule::Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const
{
	using namespace CastlePCGRoofQuarters;
	if (!FMath::IsNearlyEqual(Query.GetCellPitch(), UCastleHachimisV4Layout::CellPitch, 0.1)
		|| !FMath::IsNearlyEqual(Query.GetLayerHeight(), UCastleHachimisV4Layout::StoryRise, 0.1))
	{
		Error = FString::Printf(TEXT("规则 %s 的棋盘节距或层高与四分屋顶资产不一致。"), *GetPathName());
		return false;
	}
	TSet<FIntVector> Replaced;
	for (const FIntVector& Cell : Query.OrderedCells())
	{
		const FCastlePCGBoardCell& BoardCell = *Query.Find(Cell);
		const FCastlePCGCellSpec& Spec = Field.Cells.FindChecked(Cell);
		if (Spec.Roof != ECastlePCGRoof::Keep || Spec.bOverrideWholeRoof || !BoardCell.Layout) { continue; }

		const FCastlePCGRoomSample& Sample = Spec.RoomFrame;
		const UCastleHachimisV4Layout* Layout = BoardCell.bRoom ? Sample.Layout : BoardCell.Layout;
		if (!Layout || Layout->bChassisOnly) { continue; } // 单层普通房只提供底盘，不参与四分屋顶。
		const FVector InBuilding = BoardCell.CellInBuilding.GetLocation();
		const FIntVector LocalCell = BoardCell.bRoom ? Sample.Cell : FIntVector(
			FMath::RoundToInt(InBuilding.X / Layout->CellPitch + (Layout->Grid.X - 1) * 0.5),
			FMath::RoundToInt(InBuilding.Y / Layout->CellPitch + (Layout->Grid.Y - 1) * 0.5),
			FMath::RoundToInt(InBuilding.Z / Layout->StoryRise));
		if (!BoardCell.bRoom && (!InBuilding.Equals(Layout->CellBottom(LocalCell), 0.1)
			|| LocalCell.X < 0 || LocalCell.X >= Layout->Grid.X || LocalCell.Y < 0 || LocalCell.Y >= Layout->Grid.Y
			|| LocalCell.Z < 0 || LocalCell.Z >= Layout->Grid.Z))
		{
			Error = FString::Printf(TEXT("%s 格 %s 的格底不匹配作者布局 %s。"), *GetPathName(), *Cell.ToString(), *Layout->GetPathName());
			return false;
		}
		if (LocalCell.Z != Layout->Grid.Z - 1) { continue; }
		const FCastlePCGRoofQuarterForm* Form = Forms.Find(FormKey(*Layout));
		if (!Form || Form->Layout != Layout)
		{
			Error = FString::Printf(TEXT("规则 %s 缺少正式布局 %s 的四分屋顶。"), *GetPathName(), *Layout->GetPathName());
			return false;
		}

		// 先看整栋是否已经由叠建规则接管。仅局部 Roof=Hidden 时继续装配其余顶格。
		const FQuat Turn = BoardCell.bRoom ? FQuat(FVector::UpVector, Sample.bTurn ? HALF_PI : 0.0)
			: BoardCell.CellInBuilding.GetRotation().Inverse();
		const auto GridDirection = [&Turn](const FIntVector& Offset)
		{
			const FVector Rotated = Turn.RotateVector(FVector(Offset));
			return FIntVector(FMath::RoundToInt(Rotated.X), FMath::RoundToInt(Rotated.Y), FMath::RoundToInt(Rotated.Z));
		};
		bool bWholeRoofOverridden = false;
		for (int32 X = 0; X < Layout->Grid.X; ++X)
		for (int32 Y = 0; Y < Layout->Grid.Y; ++Y)
		{
			const FIntVector Other = Cell + GridDirection(FIntVector(X, Y, LocalCell.Z) - LocalCell);
			const FCastlePCGCellSpec* OtherSpec = Field.Cells.Find(Other);
			if (OtherSpec && (OtherSpec->bOverrideWholeRoof || OtherSpec->Roof == ECastlePCGRoof::Override))
			{
				bWholeRoofOverridden = true;
			}
		}
		if (bWholeRoofOverridden) { continue; }

		const auto OccupiedByNeighbor = [&Query, &Field, &BoardCell, &Sample](const FIntVector& Neighbor)
		{
			const FCastlePCGBoardCell* Occupant = Query.Find(Neighbor);
			if (!Occupant || Occupant->BuildingRoot == BoardCell.BuildingRoot) { return false; }
			if (!BoardCell.bRoom) { return true; }
			const auto& Other = Field.Cells.FindChecked(Neighbor).RoomFrame;
			return Other.Layout != Sample.Layout || Other.Origin != Sample.Origin || Other.bTurn != Sample.bTurn;
		};
		const auto ShouldTrim = [&](const FIntVector& LocalDirection)
		{
			const FIntVector Outside = Cell + GridDirection(LocalDirection);
			return OccupiedByNeighbor(Outside) || OccupiedByNeighbor(Outside + Query.Up) || Query.IsMainTowerColumn(Outside);
		};
		for (const FCastlePCGRoofQuarterPiece& Piece : Form->Pieces)
		{
			if (Piece.Owner != LocalCell) { continue; }
			const uint8 Bits = (ShouldTrim(FIntVector(Piece.Outward.X, 0, 0)) ? 1 : 0)
				| (ShouldTrim(FIntVector(0, Piece.Outward.Y, 0)) ? 2 : 0);
			const auto State = static_cast<ECastlePCGRoofQuarterState>(Bits);
			FCastlePCGAttachmentSpec Attachment;
			Attachment.Kit = Piece.Variants.FindChecked(State);
			Attachment.AnchorTransform = FTransform(Turn, Turn.RotateVector(Piece.PivotInOwner));
			Attachment.bSelectable = true;
			Attachment.bAffectsNavigation = false;
			if (!Field.AddAttachment({Cell, TEXT("RoofQuarter"), Piece.Slot}, Attachment, Error)) { return false; }
			Replaced.Add(Cell);
		}
	}
	// 延后写回显隐，保证另一片求值时仍能读到原始拓扑与前置规则的优先级。
	for (const FIntVector& Cell : Replaced)
	{
		auto& Spec = Field.Cells.FindChecked(Cell);
		Spec.Roof = ECastlePCGRoof::Hidden;
		Spec.bOverrideWholeRoof = true;
	}
	return true;
}
