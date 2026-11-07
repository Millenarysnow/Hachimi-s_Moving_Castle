// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingPreviewWidget.h"

#include "Rendering/DrawElements.h"

namespace
{
	/**
	 * 追加一条线段。单独保存线段而不是把所有点串成一条折线，避免 Slate
	 * 在两个不相邻的建筑边之间自动补画一条错误的连接线。
	 */
	void AddSegment(const FVector& Start, const FVector& End, TArray<UCastleBuildingPreviewWidget::FPreviewSegment>& OutSegments)
	{
		UCastleBuildingPreviewWidget::FPreviewSegment Segment;
		Segment.Start = Start;
		Segment.End = End;
		OutSegments.Add(Segment);
	}

	/** 根据颜色枚举返回统一的预览色，不让卡片各自维护一份颜色表。 */
	FLinearColor GetTilePreviewColor(ECastleTileColor Color)
	{
		switch (Color)
		{
		case ECastleTileColor::Red:
			return FLinearColor(0.98f, 0.18f, 0.18f, 1.0f);
		case ECastleTileColor::Blue:
			return FLinearColor(0.16f, 0.50f, 1.0f, 1.0f);
		case ECastleTileColor::Green:
			return FLinearColor(0.16f, 0.92f, 0.36f, 1.0f);
		case ECastleTileColor::Yellow:
			return FLinearColor(1.0f, 0.78f, 0.12f, 1.0f);
		case ECastleTileColor::Purple:
			return FLinearColor(0.76f, 0.30f, 1.0f, 1.0f);
		case ECastleTileColor::Gray:
		default:
			return FLinearColor(0.72f, 0.82f, 0.90f, 1.0f);
		}
	}

	/** 返回固定等级对应的标准占用形状。 */
	void BuildLevelCells(int32 Level, TArray<FIntVector>& OutCells)
	{
		OutCells.Reset();

		switch (Level)
		{
		case 2:
			OutCells.Add(FIntVector(0, 0, 0));
			OutCells.Add(FIntVector(1, 0, 0));
			break;
		case 3:
			for (int32 X = 0; X < 2; ++X)
			{
				for (int32 Y = 0; Y < 2; ++Y)
				{
					OutCells.Add(FIntVector(X, Y, 0));
				}
			}
			break;
		case 4:
			for (int32 X = 0; X < 2; ++X)
			{
				for (int32 Y = 0; Y < 2; ++Y)
				{
					for (int32 Z = 0; Z < 2; ++Z)
					{
						OutCells.Add(FIntVector(X, Y, Z));
					}
				}
			}
			break;
		case 1:
		case 5:
		default:
			OutCells.Add(FIntVector::ZeroValue);
			break;
		}
	}
}

void UCastleBuildingPreviewWidget::SetBuildingDescriptor(const FCastleBuildingDescriptor& InDescriptor)
{
	Descriptor = InDescriptor;
	bHasDescriptor = Descriptor.HasBuildingClass();
	RotationDegrees = -32.0f;
	InvalidateLayoutAndVolatility();
}

void UCastleBuildingPreviewWidget::ClearPreview()
{
	Descriptor = FCastleBuildingDescriptor();
	bHasDescriptor = false;
	RotationDegrees = -32.0f;
	InvalidateLayoutAndVolatility();
}

void UCastleBuildingPreviewWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bHasDescriptor)
	{
		return;
	}

	// 只旋转线框数据，不生成世界 Actor，因此这个效果不会改变棋盘状态。
	RotationDegrees = FMath::Fmod(RotationDegrees + InDeltaTime * 14.0f, 360.0f);
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UCastleBuildingPreviewWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const int32 MaxLayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bHasDescriptor)
	{
		return MaxLayerId;
	}

	TArray<FPreviewSegment> Segments;
	BuildPreviewSegments(Segments);
	if (Segments.Num() == 0)
	{
		return MaxLayerId;
	}

	const FVector2D WidgetSize = AllottedGeometry.GetLocalSize();
	if (WidgetSize.X <= KINDA_SMALL_NUMBER || WidgetSize.Y <= KINDA_SMALL_NUMBER)
	{
		return MaxLayerId;
	}

	TArray<FVector2D> ProjectedStarts;
	TArray<FVector2D> ProjectedEnds;
	ProjectedStarts.Reserve(Segments.Num());
	ProjectedEnds.Reserve(Segments.Num());

	FVector2D Minimum(FLT_MAX, FLT_MAX);
	FVector2D Maximum(-FLT_MAX, -FLT_MAX);
	for (const FPreviewSegment& Segment : Segments)
	{
		const FVector2D ProjectedStart = ProjectPoint(Segment.Start);
		const FVector2D ProjectedEnd = ProjectPoint(Segment.End);
		ProjectedStarts.Add(ProjectedStart);
		ProjectedEnds.Add(ProjectedEnd);
		Minimum.X = FMath::Min(Minimum.X, FMath::Min(ProjectedStart.X, ProjectedEnd.X));
		Minimum.Y = FMath::Min(Minimum.Y, FMath::Min(ProjectedStart.Y, ProjectedEnd.Y));
		Maximum.X = FMath::Max(Maximum.X, FMath::Max(ProjectedStart.X, ProjectedEnd.X));
		Maximum.Y = FMath::Max(Maximum.Y, FMath::Max(ProjectedStart.Y, ProjectedEnd.Y));
	}

	const FVector2D BoundsSize = Maximum - Minimum;
	const float Margin = 12.0f;
	const float AvailableWidth = FMath::Max(1.0f, WidgetSize.X - Margin * 2.0f);
	const float AvailableHeight = FMath::Max(1.0f, WidgetSize.Y - Margin * 2.0f);
	const float Scale = FMath::Min(
		AvailableWidth / FMath::Max(BoundsSize.X, 1.0f),
		AvailableHeight / FMath::Max(BoundsSize.Y, 1.0f));
	const FVector2D BoundsCenter = (Minimum + Maximum) * 0.5f;
	const FVector2D WidgetCenter = WidgetSize * 0.5f;

	const FLinearColor BaseColor = GetPreviewColor();
	const FLinearColor GlowColor(BaseColor.R, BaseColor.G, BaseColor.B, 0.14f);
	const FLinearColor CoreColor(BaseColor.R, BaseColor.G, BaseColor.B, 0.86f);
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();

	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FVector2D Start = WidgetCenter + (ProjectedStarts[SegmentIndex] - BoundsCenter) * Scale;
		const FVector2D End = WidgetCenter + (ProjectedEnds[SegmentIndex] - BoundsCenter) * Scale;

		TArray<FVector2D> GlowPoints;
		GlowPoints.Add(Start);
		GlowPoints.Add(End);
		FSlateDrawElement::MakeLines(OutDrawElements, MaxLayerId + 1, PaintGeometry, GlowPoints, ESlateDrawEffect::None, GlowColor, true, 5.0f);

		TArray<FVector2D> CorePoints;
		CorePoints.Add(Start);
		CorePoints.Add(End);
		FSlateDrawElement::MakeLines(OutDrawElements, MaxLayerId + 2, PaintGeometry, CorePoints, ESlateDrawEffect::None, CoreColor, true, 1.8f);
	}

	return MaxLayerId + 2;
}

void UCastleBuildingPreviewWidget::BuildOccupiedCells(TArray<FIntVector>& OutCells) const
{
	BuildLevelCells(FMath::Clamp(Descriptor.Level, 1, 5), OutCells);
}

void UCastleBuildingPreviewWidget::BuildPreviewSegments(TArray<FPreviewSegment>& OutSegments) const
{
	OutSegments.Reset();

	TArray<FIntVector> Cells;
	BuildOccupiedCells(Cells);
	if (Cells.Num() == 0)
	{
		return;
	}

	constexpr float CellSize = 1.0f;
	constexpr float CellHeight = 0.86f;
	FIntVector Minimum = Cells[0];
	FIntVector Maximum = Cells[0];
	for (const FIntVector& Cell : Cells)
	{
		Minimum.X = FMath::Min(Minimum.X, Cell.X);
		Minimum.Y = FMath::Min(Minimum.Y, Cell.Y);
		Minimum.Z = FMath::Min(Minimum.Z, Cell.Z);
		Maximum.X = FMath::Max(Maximum.X, Cell.X);
		Maximum.Y = FMath::Max(Maximum.Y, Cell.Y);
		Maximum.Z = FMath::Max(Maximum.Z, Cell.Z);
	}

	const FVector Center(
		(static_cast<float>(Minimum.X + Maximum.X) + 1.0f) * 0.5f,
		(static_cast<float>(Minimum.Y + Maximum.Y) + 1.0f) * 0.5f,
		(static_cast<float>(Minimum.Z + Maximum.Z) + 1.0f) * 0.5f);

	for (const FIntVector& Cell : Cells)
	{
		const FVector CellMinimum(
			(static_cast<float>(Cell.X) - Center.X) * CellSize,
			(static_cast<float>(Cell.Y) - Center.Y) * CellSize,
			(static_cast<float>(Cell.Z) - Center.Z) * CellHeight);
		const FVector CellMaximum = CellMinimum + FVector(CellSize, CellSize, CellHeight);
		AddCellWireframe(CellMinimum, CellMaximum, OutSegments);
	}

	const float TopHeight = (static_cast<float>(Maximum.Z) + 1.0f - Center.Z) * CellHeight;
	switch (Descriptor.BuildingType)
	{
	case ECastleBuildingType::ArcherTower:
		AddArcherTowerDetail(TopHeight, CellSize, OutSegments);
		break;
	case ECastleBuildingType::CannonTower:
		AddCannonTowerDetail(TopHeight, CellSize, OutSegments);
		break;
	case ECastleBuildingType::Farm:
		AddFarmDetail(TopHeight, CellSize, OutSegments);
		break;
	case ECastleBuildingType::Road:
	default:
		AddRoadDetail(TopHeight, CellSize, OutSegments);
		break;
	}
}

void UCastleBuildingPreviewWidget::AddCellWireframe(const FVector& CellMinimum, const FVector& CellMaximum, TArray<FPreviewSegment>& OutSegments)
{
	const FVector P000(CellMinimum.X, CellMinimum.Y, CellMinimum.Z);
	const FVector P100(CellMaximum.X, CellMinimum.Y, CellMinimum.Z);
	const FVector P010(CellMinimum.X, CellMaximum.Y, CellMinimum.Z);
	const FVector P110(CellMaximum.X, CellMaximum.Y, CellMinimum.Z);
	const FVector P001(CellMinimum.X, CellMinimum.Y, CellMaximum.Z);
	const FVector P101(CellMaximum.X, CellMinimum.Y, CellMaximum.Z);
	const FVector P011(CellMinimum.X, CellMaximum.Y, CellMaximum.Z);
	const FVector P111(CellMaximum.X, CellMaximum.Y, CellMaximum.Z);

	AddSegment(P000, P100, OutSegments);
	AddSegment(P100, P110, OutSegments);
	AddSegment(P110, P010, OutSegments);
	AddSegment(P010, P000, OutSegments);
	AddSegment(P001, P101, OutSegments);
	AddSegment(P101, P111, OutSegments);
	AddSegment(P111, P011, OutSegments);
	AddSegment(P011, P001, OutSegments);
	AddSegment(P000, P001, OutSegments);
	AddSegment(P100, P101, OutSegments);
	AddSegment(P110, P111, OutSegments);
	AddSegment(P010, P011, OutSegments);
}

void UCastleBuildingPreviewWidget::AddArcherTowerDetail(float TopHeight, float CellSize, TArray<FPreviewSegment>& OutSegments)
{
	const float HalfSize = CellSize * 0.5f;
	const float PyramidHeight = CellSize * 0.62f;
	const FVector A(-HalfSize, -HalfSize, TopHeight);
	const FVector B(HalfSize, -HalfSize, TopHeight);
	const FVector C(HalfSize, HalfSize, TopHeight);
	const FVector D(-HalfSize, HalfSize, TopHeight);
	const FVector Tip(0.0f, 0.0f, TopHeight + PyramidHeight);
	AddSegment(A, Tip, OutSegments);
	AddSegment(B, Tip, OutSegments);
	AddSegment(C, Tip, OutSegments);
	AddSegment(D, Tip, OutSegments);
}

void UCastleBuildingPreviewWidget::AddCannonTowerDetail(float TopHeight, float CellSize, TArray<FPreviewSegment>& OutSegments)
{
	const float BarrelHalfWidth = CellSize * 0.13f;
	const float BarrelHeight = TopHeight + CellSize * 0.18f;
	const float BarrelLength = CellSize * 0.95f;
	const FVector Start(-BarrelHalfWidth, -BarrelHalfWidth, BarrelHeight);
	const FVector End(BarrelLength, BarrelHalfWidth, BarrelHeight + BarrelHalfWidth * 0.35f);
	AddSegment(Start, End, OutSegments);
	AddSegment(FVector(Start.X, Start.Y, Start.Z + BarrelHalfWidth * 2.0f), FVector(End.X, End.Y, End.Z + BarrelHalfWidth * 2.0f), OutSegments);
	AddSegment(FVector(Start.X, Start.Y, Start.Z), FVector(Start.X, Start.Y, Start.Z + BarrelHalfWidth * 2.0f), OutSegments);
	AddSegment(FVector(End.X, End.Y, End.Z), FVector(End.X, End.Y, End.Z + BarrelHalfWidth * 2.0f), OutSegments);
}

void UCastleBuildingPreviewWidget::AddFarmDetail(float TopHeight, float CellSize, TArray<FPreviewSegment>& OutSegments)
{
	const float HalfSize = CellSize * 0.38f;
	for (int32 RowIndex = -1; RowIndex <= 1; ++RowIndex)
	{
		const float Y = static_cast<float>(RowIndex) * CellSize * 0.24f;
		AddSegment(FVector(-HalfSize, Y, TopHeight + 0.04f), FVector(HalfSize, Y, TopHeight + 0.04f), OutSegments);
	}
}

void UCastleBuildingPreviewWidget::AddRoadDetail(float TopHeight, float CellSize, TArray<FPreviewSegment>& OutSegments)
{
	const float HalfSize = CellSize * 0.32f;
	AddSegment(FVector(-HalfSize, 0.0f, TopHeight + 0.03f), FVector(HalfSize, 0.0f, TopHeight + 0.03f), OutSegments);
	AddSegment(FVector(0.0f, -HalfSize, TopHeight + 0.03f), FVector(0.0f, HalfSize, TopHeight + 0.03f), OutSegments);
}

FVector2D UCastleBuildingPreviewWidget::ProjectPoint(const FVector& Point) const
{
	// 通过固定俯视角和持续变化的偏航角得到稳定的三维等距效果。
	const FQuat CameraRotation = FRotator(-24.0f, RotationDegrees, 0.0f).Quaternion();
	const FVector ViewRight = CameraRotation.RotateVector(FVector::RightVector);
	const FVector ViewUp = CameraRotation.RotateVector(FVector::UpVector);
	return FVector2D(FVector::DotProduct(Point, ViewRight), -FVector::DotProduct(Point, ViewUp));
}

FLinearColor UCastleBuildingPreviewWidget::GetPreviewColor() const
{
	return GetTilePreviewColor(Descriptor.Color);
}
