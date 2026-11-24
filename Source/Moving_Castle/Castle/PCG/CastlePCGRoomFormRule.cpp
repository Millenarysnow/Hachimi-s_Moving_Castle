#include "CastleBuildingPCGRule.h"

#include "CastlePCGSemantics.h"
#include "Engine/StaticMesh.h"
#include "HachimisV4/CastleHachimisV4Layout.h"

namespace
{
	FIntVector SampleCell(const FCastlePCGRoomSample& Sample, const FIntVector& Local)
	{
		return Sample.Origin + (Sample.bTurn ? FIntVector(-Local.Y, Local.X, Local.Z) : Local);
	}

	/** 按样例覆盖整块房格；完整匹配后再写入，避免半块样例出现在语义场。 */
	void AssignSamples(const FCastlePCGBoardQuery& Query, const TArray<FIntVector>& RoomCells,
		const TMap<FIntVector, FIntVector>& Islands, const UCastleHachimisV4Layout& Layout, bool bTurn, bool bStack,
		TSet<FIntVector>& Used, FCastlePCGSemanticField& Field)
	{
		for (const FIntVector& Origin : RoomCells)
		{
			FCastlePCGRoomSample Sample{&Layout, FIntVector::ZeroValue, Origin, bTurn};
			TArray<FIntVector, TInlineAllocator<4>> Matched;
			for (int32 X = 0; X < Layout.Grid.X; ++X)
			for (int32 Y = 0; Y < Layout.Grid.Y; ++Y)
			{
				const FIntVector Local(X, Y, 0);
				const FIntVector Cell = SampleCell(Sample, Local);
				const FCastlePCGBoardCell* Lower = Query.Find(Cell);
				if (Used.Contains(Cell) || !Lower || !Lower->bRoom) { continue; }
				if (bStack)
				{
					const FCastlePCGBoardCell* Upper = Query.FindOffset(Cell, Query.Up);
					if (!Upper || !Upper->bRoom || Islands.FindChecked(Cell) != Islands.FindChecked(Origin)) { continue; }
				}
				else if (Lower->Color != Query.Find(Origin)->Color) { continue; }
				Matched.Add(Local);
			}
			if (Matched.Num() != Layout.Grid.X * Layout.Grid.Y) { continue; }
			for (const FIntVector& Local : Matched)
			{
				Sample.Cell = Local;
				const FIntVector Cell = SampleCell(Sample, Local);
				Used.Add(Cell);
				FCastlePCGCellSpec& Owner = Field.Cells.FindChecked(Cell);
				if (bStack)
				{
					Owner.RoomFrame = Sample;
					FCastlePCGRoomSample& Upper = Field.Cells.FindChecked(Cell + Query.Up).RoomFrame;
					// 三层以上时，优先让本格的上接口决定该房的框架来源。
					if (!Upper.Layout || Upper.Cell.Z != 0)
					{
						Upper = Sample;
						Upper.Cell.Z = 1;
					}
				}
				else
				{
					Owner.RoomDeck = Sample;
					if (Owner.Deck == ECastlePCGDeck::Solid)
					{
						for (const ECastlePCGFace Face : Query.Sides)
						{
							if (Field.SideFaces.FindChecked({Cell, Face}).State == ECastlePCGSide::MergedOpen)
							{
								Owner.Deck = ECastlePCGDeck::Merged;
								break;
							}
						}
					}
				}
			}
		}
	}

	/** 家具保存自身坐标中的真实包围盒，逐候选旋转到梯井坐标检查。 */
	bool CollectFurniture(const FCastlePCGBoardQuery& Query, const FCastlePCGRoomSample& Sample,
		TArray<TPair<FBox, FTransform>>& Furniture, FString& Error)
	{
		if (!Sample.Layout)
		{
			Error = TEXT("普通房通梯缺少对应的双层木构样例。");
			return false;
		}
		const UCastleHachimisV4Layout& Layout = *Sample.Layout;
		const FTransform SampleFrame = FTransform(-Layout.CellBottom(FIntVector::ZeroValue))
			* FTransform(FQuat(FVector::UpVector, Sample.bTurn ? HALF_PI : 0.0), Query.CellBottom(Sample.Origin));
		for (const FCastleV4Part& Part : Layout.Parts)
		{
			if (Part.Group != TEXT("Furniture")) { continue; }
			if (!IsValid(Part.Mesh) || !Part.Local.IsValid())
			{
				Error = FString::Printf(TEXT("%s 的家具 %s 网格或变换无效。"), *Layout.GetPathName(), *Part.Node.ToString());
				return false;
			}
			if (CastlePCGSemantics::RoomFramePartOwner(Layout, Part) == Sample.Cell)
			{
				const FBox Bounds = Part.Mesh->GetBoundingBox();
				if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN())
				{
					Error = FString::Printf(TEXT("%s 的家具 %s 包围盒无效。"), *Layout.GetPathName(), *Part.Node.ToString());
					return false;
				}
				Furniture.Emplace(Bounds, Part.Local * SampleFrame);
			}
		}
		return true;
	}

	bool OrientStair(const FCastlePCGBoardQuery& Query, const FIntVector& Lower, const FCastlePCGRoomSample& LowerFrame,
		const FCastlePCGRoomSample& UpperFrame, FCastlePCGVerticalSpec& Spec, FString& Error)
	{
		if (!IsValid(Spec.Kit) || !Spec.Kit->Hatch.Opening.IsValid)
		{
			Error = FString::Printf(TEXT("梯井套件 %s 无效。"), *GetPathNameSafe(Spec.Kit));
			return false;
		}
		TArray<TPair<FBox, FTransform>> Furniture;
		if (!CollectFurniture(Query, LowerFrame, Furniture, Error) || !CollectFurniture(Query, UpperFrame, Furniture, Error)) { return false; }
		const FCastleV4Hatch& Hatch = Spec.Kit->Hatch;
		FBox Shaft = Hatch.Opening;
		Shaft += Hatch.BottomStand;
		Shaft += Hatch.TopStand;
		Shaft += Hatch.LadderBottom;
		Shaft.Min.Z = Hatch.BottomStand.Z;
		Shaft.Max.Z = Hatch.TopStand.Z + Query.GetLayerHeight();
		for (int32 Quarter = 0; Quarter < 4; ++Quarter)
		{
			// 首选上层作者方向，继而按固定顺序检查另外三向。
			const double Yaw = (Quarter + int32(UpperFrame.bTurn)) * 90.0;
			const FTransform Candidate(FRotator(0.0, Yaw, 0.0), Query.CellBottom(Lower));
			if (!Furniture.ContainsByPredicate([&](const TPair<FBox, FTransform>& Part)
			{
				return Part.Key.TransformBy(Part.Value.GetRelativeTransform(Candidate).ToMatrixWithScale()).Intersect(Shaft);
			}))
			{
				Spec.BoardYaw = Yaw;
				return true;
			}
		}
		Error = FString::Printf(TEXT("套件 %s 的四个正式梯向均被作者家具堵住。"), *Spec.Kit->GetPathName());
		return false;
	}
}

bool UCastlePCGRoomFormRule::Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const
{
	// 资产内的样例名与作者格数必须一一对应，缺配置时不得换用其他样例。
	const auto& Config = Query.Config();
	const FName Names[] = {TEXT("L1"), TEXT("L2H"), TEXT("L3H"), TEXT("L2V"), TEXT("L3V"), TEXT("L4")};
	const FIntVector Dimensions[] = {{1,1,1},{2,1,1},{2,2,1},{1,1,2},{2,1,2},{2,2,2}};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
	{
		const UCastleHachimisV4Layout* Layout = Config.RoomExamples.FindRef(Names[Index]);
		if (!IsValid(Layout) || Layout->Family != TEXT("Room") || Layout->Form != Names[Index]
			|| Layout->Grid != Dimensions[Index] || Layout->Parts.IsEmpty())
		{
			Error = FString::Printf(TEXT("%s 的普通房样例 %s 无效：%s。"),
				*Config.GetPathName(), *Names[Index].ToString(), *GetPathNameSafe(Layout));
			return false;
		}
	}
	if (!IsValid(Config.RoomL1Bay) || Config.RoomL1Bay->WallKey != TEXT("Room_L1_Bay"))
	{
		Error = FString::Printf(TEXT("%s 的 Room_L1_Bay 资产无效：%s。"), *Config.GetPathName(), *GetPathNameSafe(Config.RoomL1Bay));
		return false;
	}
	TArray<FIntVector> Rooms;
	for (const FIntVector& Cell : Query.OrderedCells()) { if (Query.Find(Cell)->bRoom) { Rooms.Add(Cell); } }
	const auto Islands = Query.HorizontalComponents(Field, true);
	TSet<FIntVector> DeckUsed, FrameUsed;
	auto Assign = [&](FName Name, bool bTurn, bool bStack, TSet<FIntVector>& Used)
	{
		AssignSamples(Query, Rooms, Islands, *Config.RoomExamples.FindChecked(Name), bTurn, bStack, Used, Field);
	};
	Assign(TEXT("L3H"), false, false, DeckUsed);
	Assign(TEXT("L2H"), false, false, DeckUsed);
	Assign(TEXT("L2H"), true, false, DeckUsed);
	Assign(TEXT("L1"), false, false, DeckUsed);
	Assign(TEXT("L4"), false, true, FrameUsed);
	Assign(TEXT("L3V"), false, true, FrameUsed);
	Assign(TEXT("L3V"), true, true, FrameUsed);
	Assign(TEXT("L2V"), false, true, FrameUsed);
	for (const FIntVector& Cell : Rooms)
	{
		FCastlePCGVerticalSpec* Spec = Field.VerticalInterfaces.Find(Cell);
		if (!Spec || Spec->State != ECastlePCGVertical::Ladder || !Query.FindOffset(Cell, Query.Up)->bRoom) { continue; }
		if (!OrientStair(Query, Cell, Field.Cells.FindChecked(Cell).RoomFrame, Field.Cells.FindChecked(Cell + Query.Up).RoomFrame,
			*Spec, Error))
		{
			Error = FString::Printf(TEXT("格 %s：%s"), *Cell.ToString(), *Error);
			return false;
		}
	}
	return true;
}
