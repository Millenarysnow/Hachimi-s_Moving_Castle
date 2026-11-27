#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastlePCGRoofQuarterRule.h"
#include "CastlePCGSupportRules.h"
#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"
#include "Castle/PCG/HachimisV4/HachimisV4MeshValidation.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "StaticMeshAttributes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGRoofQuarters, "MovingCastle.Castle.PCG.Structure.RoofQuarters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePCGRoofQuarters::RunTest(const FString& Parameters)
{
	const auto* Rules = LoadObject<UCastlePCGRuleSetDataAsset>(nullptr,
		TEXT("/Game/MovingCastle/Data/DA_CastlePCGRuleSet.DA_CastlePCGRuleSet"));
	if (!TestNotNull(TEXT("读取持久化规则集"), Rules)) { return false; }
	FString Error;
	if (!Rules->ValidateConfiguration(Error)) { AddError(Error); return false; }
	const UCastlePCGRoofQuarterRule* Rule = nullptr;
	for (const auto& Entry : Rules->Rules)
	{
		if (const auto* Candidate = Cast<UCastlePCGRoofQuarterRule>(Entry)) { Rule = Candidate; }
	}
	if (!TestNotNull(TEXT("已登记四分屋顶规则"), Rule)) { return false; }

	auto AddBounds = [](FBox& Box, const FCastleV4Part& Part, const FTransform& Parent)
	{
		const FMeshDescription* Mesh = Part.Mesh->GetMeshDescription(0);
		const auto Points = FStaticMeshConstAttributes(*Mesh).GetVertexPositions();
		const FTransform Transform = Part.Local * Parent;
		for (const FVertexID Vertex : Mesh->Vertices().GetElementIDs())
		{
			Box += Transform.TransformPosition(FVector(Points[Vertex]));
		}
	};
	const FIntVector Up(0, 0, 1);
	TSet<const UStaticMesh*> Meshes;
	for (const auto& Entry : Rule->Forms)
	{
		const auto& Form = Entry.Value;
		const auto& Layout = *Form.Layout;
		const FString Name = Entry.Key.ToString();
		const bool bRoom = Layout.Family == TEXT("Room");
		FBox Original(ForceInit);
		for (const auto& Part : Layout.Parts)
		{
			if (Part.Group == TEXT("Roof") || Part.Group == TEXT("RoofKeys")) { AddBounds(Original, Part, FTransform::Identity); }
		}
		for (const auto& Piece : Form.Pieces)
		{
			for (const auto& Variant : Piece.Variants)
			{
				auto* Mesh = Variant.Value->Parts[0].Mesh.Get();
				FString Reason;
				const bool bValid = HachimisV4MeshValidation::Verify(*Mesh, Reason);
				TestTrue(Mesh->GetName() + TEXT(" 距离场与渲染校验 ") + Reason, bValid);
				Meshes.Add(Mesh);
			}
		}
		for (const bool bTurn : {false, true})
		{
			const auto Rotate = [bTurn](const FIntVector& V) { return bTurn ? FIntVector(-V.Y, V.X, V.Z) : V; };
			const FQuat Yaw(FVector::UpVector, bTurn ? HALF_PI : 0.0);
			const FIntVector Origin(20, 20, 10);
			TMap<FIntVector, FCastlePCGBoardCell> Cells;
			FCastlePCGSemanticField Base;
			for (int32 X = 0; X < Layout.Grid.X; ++X)
			for (int32 Y = 0; Y < Layout.Grid.Y; ++Y)
			for (int32 Z = 0; Z < Layout.Grid.Z; ++Z)
			{
				const FIntVector Local(X, Y, Z);
				const FIntVector Cell = Origin + Rotate(Local);
				auto& Data = Cells.Add(Cell);
				Data.Layout = bRoom ? Rules->RoomExamples.FindRef(TEXT("L1")).Get() : &Layout;
				Data.BuildingRoot = bRoom ? Cell : Origin;
				Data.bRoom = bRoom;
				Data.CellInBuilding = FTransform(Yaw.Inverse(), Layout.CellBottom(Local));
				auto& Spec = Base.Cells.Add(Cell);
				if (bRoom) { Spec.RoomFrame = {&Layout, Local, Origin, bTurn}; }
			}
			const auto CheckMask = [&](uint8 Mask, bool bAbove, bool bTower)
			{
				auto Occupancy = Cells;
				auto Field = Base;
				TOptional<FIntVector> Tower;
				// 每个被选中的外侧只放一个邻居。较长边的其余顶格即使外侧为空也必须一起收缩。
				const int32 Top = Layout.Grid.Z - 1;
				const FIntVector Outside[] = {
					{Layout.Grid.X, Layout.Grid.Y - 1, Top}, {-1, 0, Top},
					{Layout.Grid.X - 1, Layout.Grid.Y, Top}, {0, -1, Top}
				};
				for (int32 Side = 0; Side < UE_ARRAY_COUNT(Outside); ++Side)
				{
					if (!(Mask & (1 << Side))) { continue; }
					const FIntVector Neighbor = Origin + Rotate(Outside[Side]) + (bAbove ? Up : FIntVector::ZeroValue);
					if (bTower) { Tower = Neighbor; }
					else
					{
						Occupancy.Add(Neighbor).BuildingRoot = Neighbor;
						Field.Cells.Add(Neighbor);
					}
				}
				FCastlePCGBoardQuery Query(MoveTemp(Occupancy), *Rules, nullptr, 0, 0, Tower);
				if (!Rule->Evaluate(Query, Field, Error)) { AddError(Name + TEXT(" ") + Error); return; }
				TestEqual(Name + TEXT(" 四分网格按槽数量"), Field.GetAttachments().Num(), Form.Pieces.Num());
				FBox Assembled(ForceInit);
				for (const auto& Piece : Form.Pieces)
				{
					const FIntVector Cell = Origin + Rotate(Piece.Owner);
					const auto* Attachment = Field.GetAttachments().Find({Cell, TEXT("RoofQuarter"), Piece.Slot});
					if (!TestNotNull(Name + TEXT(" 实际屋角归属查找"), Attachment)) { continue; }
					const uint8 XBit = Piece.Outward.X > 0 ? 1 : 2;
					const uint8 YBit = Piece.Outward.Y > 0 ? 4 : 8;
					const uint8 Bits = ((Mask & XBit) ? 1 : 0) | ((Mask & YBit) ? 2 : 0);
					TestTrue(Name + TEXT(" 旋转后同侧全部屋角仍按作者轴统一选款"),
						Attachment->Kit == Piece.Variants.FindChecked(static_cast<ECastlePCGRoofQuarterState>(Bits)));
					TestTrue(Name + TEXT(" 可选中、无导航、单位缩放"), Attachment->bSelectable && !Attachment->bAffectsNavigation
						&& Attachment->AnchorTransform.GetScale3D() == FVector::OneVector);
					if (Mask == 0)
					{
						AddBounds(Assembled, Attachment->Kit->Parts[0],
							Attachment->AnchorTransform * FTransform(Yaw.RotateVector(Layout.CellBottom(Piece.Owner))));
					}
					if (Bits)
					{
						FBox Local(ForceInit);
						AddBounds(Local, Attachment->Kit->Parts[0], Attachment->AnchorTransform * FTransform(Yaw.Inverse()));
						const double X = Piece.Outward.X > 0 ? Local.Max.X : -Local.Min.X;
						const double Y = Piece.Outward.Y > 0 ? Local.Max.Y : -Local.Min.Y;
						TestTrue(Name + TEXT(" 选中的收檐面不越格"), (!(Bits & 1) || X <= 257.55) && (!(Bits & 2) || Y <= 257.55));
					}
				}
				if (Mask == 0)
				{
					const FBox Expected = Original.TransformBy(FTransform(Yaw).ToMatrixNoScale());
					TestTrue(Name + TEXT(" 常规屋面六面包围盒误差不超过 0.5 cm"), Assembled.IsValid
						&& Assembled.Min.Equals(Expected.Min, 0.5) && Assembled.Max.Equals(Expected.Max, 0.5));
				}
			};
			for (uint8 Mask = 0; Mask < 16; ++Mask) { CheckMask(Mask, false, false); }
			for (const uint8 Side : {1, 2, 4, 8})
			{
				CheckMask(Side, true, false);
				CheckMask(Side, false, true);
			}

			// 部分顶格压顶时仍生成其他格的屋角；前置规则整顶覆写时不再生成屋角。
			const FIntVector Hidden = Origin + Rotate(Form.Pieces[0].Owner);
			for (const bool bOverride : {false, true})
			{
				auto Field = Base;
				auto& Spec = Field.Cells.FindChecked(Hidden);
				Spec.Roof = bOverride ? ECastlePCGRoof::Override : ECastlePCGRoof::Hidden;
				Spec.bOverrideWholeRoof = bOverride;
				FCastlePCGBoardQuery Query(Cells, *Rules, nullptr, 0, 0);
				if (!Rule->Evaluate(Query, Field, Error)) { AddError(Error); return false; }
				const int32 Remaining = Form.Pieces.Num() - Form.Pieces.FilterByPredicate([&](const auto& P)
				{
					return Origin + Rotate(P.Owner) == Hidden;
				}).Num();
				TestEqual(Name + TEXT(" 局部压顶与整顶覆写优先级"), Field.GetAttachments().Num(), bOverride ? 0 : Remaining);
			}
		}
	}
	TestEqual(TEXT("704 个独立屋角网格，含农田二次切片"), Meshes.Num(), 704);
	return true;
}

#endif
