#include "CastlePCGSemantics.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "BuildingMergeComponent.h"
#include "HachimisV4/CastleHachimisV4PCG.h"

bool CastlePCGSemantics::CellLess(const FIntVector& A, const FIntVector& B)
{
	return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z);
}

FIntVector CastlePCGSemantics::FaceOffset(const ECastlePCGFace Face)
{
	switch (Face)
	{
	case ECastlePCGFace::PositiveX: return FIntVector(1, 0, 0);
	case ECastlePCGFace::NegativeX: return FIntVector(-1, 0, 0);
	case ECastlePCGFace::PositiveY: return FIntVector(0, 1, 0);
	case ECastlePCGFace::NegativeY: return FIntVector(0, -1, 0);
	case ECastlePCGFace::PositiveZ: return FIntVector(0, 0, 1);
	case ECastlePCGFace::NegativeZ: return FIntVector(0, 0, -1);
	default: return FIntVector::ZeroValue;
	}
}

bool CastlePCGSemantics::Extract(ACastleBuildingBase* Building, const ACastleBoard* Board,
	TArray<FCastlePCGPartAnnotation>& OutParts, FString& OutError)
{
	OutParts.Reset();
	OutError.Reset();
	if (!Building || !Board || Building->GetOccupiedCoordinates().IsEmpty())
	{
		OutError = TEXT("语义提取要求有效建筑、棋盘及占格。"); return false;
	}
	if (Building->HachimisLayout) { return CastleV4PCG::Extract(*Building, *Board, OutParts, OutError); }
	const TArray<FIntVector> Cells = Building->GetOccupiedCoordinates();
	FIntVector Minimum = Cells[0];
	for (const FIntVector& Cell : Cells) { Minimum = Minimum.ComponentMin(Cell); }
	const FTransform BoardInverse = Board->GetActorTransform().Inverse();
	TArray<UStaticMeshComponent*> Meshes;
	Building->GetComponents(Meshes);
	TArray<FCastlePCGPartAnnotation> Result;
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		// 功能分支、标签和已生成 PCG 件不属于原始模型；显隐不能用来判断动画中的原件。
		if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Mesh)
			|| Mesh->ComponentHasTag(TEXT("MergeExclude")) || Mesh->ComponentHasTag(TEXT("CastlePCGDynamic"))
			|| Mesh->HasAnyFlags(RF_Transient)) { continue; }
		if (!Mesh->GetStaticMesh())
		{
			OutError = FString::Printf(TEXT("%s.%s 缺少静态网格。"), *Building->GetPathName(), *Mesh->GetName()); return false;
		}
		const FTransform ToBoard = Mesh->GetComponentTransform() * BoardInverse;
		const FBox Box = Mesh->GetStaticMesh()->GetBoundingBox().TransformBy(ToBoard.ToMatrixWithScale());
		const FVector Size = Box.GetSize();
		const FVector Center = Box.GetCenter();
		FCastlePCGPartAnnotation Part;
		Part.Component = Mesh;
		Part.bOriginalVisible = Building->BuildingMerge->GetStableVisibility(Mesh);
		Part.Role = ECastlePCGPartRole::Structural;
		// 已核对模型中屋顶厚约 122.7～291.7、平板厚 31.55、护栏高 162.5；柱梁不是薄面。
		if (FMath::IsNearlyEqual(Size.Z, 162.503, 1.0) && FMath::Min(Size.X, Size.Y) < 10.0)
		{
			Part.Role = ECastlePCGPartRole::PerimeterRailing;
		}
		else if (FMath::IsNearlyEqual(Size.Z, 31.55, 1.0) && FMath::Min(Size.X, Size.Y) >= 350.0)
		{
			Part.Role = ECastlePCGPartRole::TopCeilingSlab;
		}
		else if (Size.Z > 80.0 && Size.Z < Board->LayerHeight && FMath::Min(Size.X, Size.Y) >= 350.0)
		{
			Part.Role = ECastlePCGPartRole::Roof;
		}
		bool bExplicitRole = false;
		for (const FName Tag : Mesh->ComponentTags)
		{
			const FString Text = Tag.ToString();
			if (!Text.StartsWith(TEXT("PCGRole"))) { continue; }
			const FString Value = Text.Mid(8);
			if (bExplicitRole || !Text.StartsWith(TEXT("PCGRole=")))
			{
				OutError = FString::Printf(TEXT("%s.%s 的 PCGRole 标签格式非法或重复。"), *Building->GetPathName(), *Mesh->GetName()); return false;
			}
			bExplicitRole = true;
			if (Value == TEXT("Roof")) { Part.Role = ECastlePCGPartRole::Roof; }
			else if (Value == TEXT("TopCeilingSlab")) { Part.Role = ECastlePCGPartRole::TopCeilingSlab; }
			else if (Value == TEXT("PerimeterRailing")) { Part.Role = ECastlePCGPartRole::PerimeterRailing; }
			else if (Value == TEXT("Structural")) { Part.Role = ECastlePCGPartRole::Structural; }
			else { OutError = FString::Printf(TEXT("%s.%s 的 PCGRole=%s 拼写非法。"), *Building->GetPathName(), *Mesh->GetName(), *Value); return false; }
		}
		// 用实际占格中心的最近距离确定归属；梁柱可跨格，但不参与单格替换。
		double BestDistance = TNumericLimits<double>::Max();
		FIntVector Nearest = Minimum;
		for (const FIntVector& Cell : Cells)
		{
			FVector CellCenter = BoardInverse.TransformPosition(Board->GetGridWorldLocation(Cell));
			CellCenter.Z -= Board->LayerHeight * 0.5;
			const double RoleZ = Part.Role == ECastlePCGPartRole::Roof ? Box.Min.Z - Board->LayerHeight
				: (Part.Role == ECastlePCGPartRole::TopCeilingSlab ? Box.Max.Z - Board->LayerHeight : Box.Min.Z);
			const double Distance = FVector::DistSquared(FVector(Center.X, Center.Y, RoleZ), CellCenter);
			if (Distance < BestDistance || (FMath::IsNearlyEqual(Distance, BestDistance) && CellLess(Cell, Nearest)))
			{ BestDistance = Distance; Nearest = Cell; }
		}
		Part.LocalCellOffset = Nearest - Minimum;
		Part.CoveredCellOffsets.Add(Part.LocalCellOffset);
		if (Part.Role == ECastlePCGPartRole::PerimeterRailing)
		{
			const FVector CellCenter = BoardInverse.TransformPosition(Board->GetGridWorldLocation(Nearest));
			const bool bThinX = Size.X < Size.Y;
			Part.BoardFacing = bThinX ? (Center.X > CellCenter.X ? ECastlePCGFace::PositiveX : ECastlePCGFace::NegativeX)
				: (Center.Y > CellCenter.Y ? ECastlePCGFace::PositiveY : ECastlePCGFace::NegativeY);
			Part.CoveredCellOffsets.Reset();
			for (const FIntVector& Cell : Cells)
			{
				const FVector P = BoardInverse.TransformPosition(Board->GetGridWorldLocation(Cell));
				const bool bSameLayer = Cell.Z == Nearest.Z;
				const bool bCovered = bThinX ? (Cell.X == Nearest.X && P.Y > Box.Min.Y && P.Y < Box.Max.Y)
					: (Cell.Y == Nearest.Y && P.X > Box.Min.X && P.X < Box.Max.X);
				if (bSameLayer && bCovered) { Part.CoveredCellOffsets.Add(Cell - Minimum); }
			}
			if (Part.CoveredCellOffsets.IsEmpty() || Part.CoveredCellOffsets.Num() > 2)
			{
				OutError = FString::Printf(TEXT("%s.%s 护栏覆盖格数非法：%d。"), *Building->GetPathName(), *Mesh->GetName(), Part.CoveredCellOffsets.Num()); return false;
			}
		}
		Result.Add(Part);
	}
	if (Result.IsEmpty()) { OutError = FString::Printf(TEXT("%s 没有原始美术零件。"), *Building->GetPathName()); return false; }
	OutParts = MoveTemp(Result);
	return true;
}
