#include "CastlePCGSemantics.h"
#include "CastleBoard.h"
#include "CastleLegacyPCGAdapter.h"
#include "CastleBuildingBase.h"
#include "Engine/StaticMesh.h"
#include "HachimisV4/CastleHachimisV4PCG.h"

bool CastlePCGSemantics::CellLess(const FIntVector& A, const FIntVector& B)
{
	return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z);
}

FTransform CastlePCGSemantics::CellFrame(const ACastleBoard& Board, const FIntVector& Cell)
{
	// 棋盘接口返回格中心。用棋盘的完整变换扣除半层高，得到作者楼板共用的格底。
	const FVector BottomWorld = Board.GetGridWorldLocation(Cell)
		- Board.GetActorTransform().TransformVector(FVector(0, 0, Board.LayerHeight * 0.5));
	return FTransform(Board.GetActorQuat(), BottomWorld, Board.GetActorScale3D());
}

FIntVector CastlePCGSemantics::RoomFramePartOwner(const UCastleHachimisV4Layout& Layout, const FCastleV4Part& Part)
{
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
	return Owner;
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
	return CastleLegacyPCG::Extract(*Building, *Board, OutParts, OutError);
}
