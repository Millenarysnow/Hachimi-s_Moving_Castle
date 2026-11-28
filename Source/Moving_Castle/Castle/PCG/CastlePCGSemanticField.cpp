#include "CastlePCGSemanticField.h"

#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingPCGRule.h"
#include "CastleDeterministicPCGNoise.h"
#include "CastlePCGAttachmentKit.h"
#include "CastlePCGSemantics.h"
#include "Engine/StaticMesh.h"

bool FCastlePCGAttachmentSpec::Validate(FString& Error) const
{
	// 视觉网格不参与导航，功能盒由规则以正的半尺寸显式提供。
	if (!IsValid(Kit) || Kit->Parts.IsEmpty() || !AnchorTransform.IsValid()
		|| AnchorTransform.GetScale3D().GetAbsMin() <= UE_SMALL_NUMBER
		|| (bAffectsNavigation && (NavigationExtent.ContainsNaN() || NavigationExtent.GetMin() <= 0.0)))
	{
		Error = FString::Printf(TEXT("挂载套件 %s 的零件、锚点或功能盒配置无效。"), *GetPathNameSafe(Kit));
		return false;
	}
	for (const FCastleV4Part& Part : Kit->Parts)
	{
		if (!IsValid(Part.Mesh) || !Part.Local.IsValid() || Part.Local.GetScale3D().GetAbsMin() <= UE_SMALL_NUMBER)
		{
			Error = FString::Printf(TEXT("挂载套件 %s 的零件 %s 缺少网格或局部变换无效。"),
				*Kit->GetPathName(), *Part.Node.ToString());
			return false;
		}
	}
	return true;
}

bool FCastlePCGSemanticField::AddAttachment(const FCastlePCGAttachmentKey& Key, const FCastlePCGAttachmentSpec& Spec, FString& Error)
{
	// 即使两个规则提出相同套件，重复拥有同一槽位也属于规则配置错误。
	if (!Cells.Contains(Key.OwnerCell) || Key.Anchor.IsNone() || Key.SlotTag.IsNone() || Attachments.Contains(Key))
	{
		Error = FString::Printf(TEXT("格 %s 的挂点 %s／槽位 %s 重复或无效，套件 %s。"),
			*Key.OwnerCell.ToString(), *Key.Anchor.ToString(), *Key.SlotTag.ToString(), *GetPathNameSafe(Spec.Kit));
		return false;
	}
	if (!Spec.Validate(Error))
	{
		Error = FString::Printf(TEXT("格 %s：%s"), *Key.OwnerCell.ToString(), *Error);
		return false;
	}
	Attachments.Add(Key, Spec);
	return true;
}

FCastlePCGBoardQuery::FCastlePCGBoardQuery(const ACastleBoard& Board,
	const TMap<FIntVector, ACastleBuildingBase*>& Occupancy, UObject* InNoise)
	: Rules(Board.PCGRuleSet), Noise(InNoise), Seed(Board.GroundSeed), GroundLayer(Board.GridOrigin.Z),
	  Pitch(Board.CellSize), LayerHeight(Board.LayerHeight),
	  bTowerSides(Board.PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides), TowerCell(Board.GetBoardCenterCoordinate())
{
	// 以建筑的首格作为稳定身份，不把 Actor、组件或世界记录传入规则。
	TMap<const ACastleBuildingBase*, FCastlePCGBoardCell> Buildings;
	for (const auto& Entry : Occupancy)
	{
		const ACastleBuildingBase& Building = *Entry.Value;
		FCastlePCGBoardCell* Data = Buildings.Find(&Building);
		if (!Data)
		{
			Data = &Buildings.Add(&Building);
			Data->BuildingRoot = Entry.Key;
			for (const FIntVector& Cell : Building.GetOccupiedCoordinates())
			{
				if (CastlePCGSemantics::CellLess(Cell, Data->BuildingRoot)) { Data->BuildingRoot = Cell; }
			}
			Data->Layout = Building.HachimisLayout;
			Data->BuildingCellCount = Building.GetOccupiedCoordinates().Num();
			Data->Color = static_cast<uint8>(Building.GetBuildingColor());
			Data->Number = Building.GetBuildingNumber();
			Data->bRoom = Data->Layout && Building.IsNormalRoom();
			Data->BoardYaw = (Board.GetActorQuat().Inverse() * Building.GetActorQuat()).Rotator().Yaw;
		}
		FCastlePCGBoardCell& Cell = Cells.Add(Entry.Key, *Data);
		// 与装配器使用同一棋盘格底基准，保留建筑旋转及棋盘原点带来的位置变化。
		Cell.CellInBuilding = CastlePCGSemantics::CellFrame(Board, Entry.Key).GetRelativeTransform(Building.GetActorTransform());
	}
	SortCells();
}

FCastlePCGBoardQuery::FCastlePCGBoardQuery(TMap<FIntVector, FCastlePCGBoardCell> InCells,
	const UCastlePCGRuleSetDataAsset& InRules, UObject* InNoise, int32 InSeed, int32 InGroundLayer,
	TOptional<FIntVector> InTowerColumn)
	: Cells(MoveTemp(InCells)), Rules(&InRules), Noise(InNoise), Seed(InSeed), GroundLayer(InGroundLayer),
	  bTowerSides(InTowerColumn.IsSet()), TowerCell(InTowerColumn.Get(FIntVector::ZeroValue))
{
	SortCells();
}

void FCastlePCGBoardQuery::SortCells()
{
	Cells.GenerateKeyArray(SortedCells);
	SortedCells.Sort(CastlePCGSemantics::CellLess);
}

bool FCastlePCGBoardQuery::IsSkyExposed(const FIntVector& Cell) const
{
	// 同一列更高的格即使隔层悬空，也遮挡本格的露天条件。
	return !SortedCells.ContainsByPredicate([&](const FIntVector& Other)
	{
		return Other.X == Cell.X && Other.Y == Cell.Y && Other.Z > Cell.Z;
	});
}

int32 FCastlePCGBoardQuery::GetOverhangDepth(const FIntVector& Cell) const
{
	// 深度按格数计。GridOrigin.Z 是各棋盘自己的地面层，其下方不属于可悬空空间。
	int32 Depth = 0;
	for (FIntVector Below = Cell - Up; Below.Z >= GroundLayer && !Cells.Contains(Below); Below -= Up) { ++Depth; }
	return Depth;
}

bool FCastlePCGBoardQuery::IsMainTowerDoorFace(const FIntVector& Cell, const FIntVector& Side) const
{
	return bTowerSides && Side.Y == 0 && Side.Z == 0 && FMath::Abs(Side.X) == 1 && Cell + Side == TowerCell;
}

bool FCastlePCGBoardQuery::IsMainTowerColumn(const FIntVector& Cell) const
{
	return bTowerSides && Cell.X == TowerCell.X && Cell.Y == TowerCell.Y;
}

bool FCastlePCGBoardQuery::SampleNoise(const FIntVector& Cell, FName Channel, float& Value, FString& Error) const
{
	// 所有规则使用同一噪声契约，非法提供者或返回值不参与任何择格。
	if (!IsValid(Noise) || !Noise->Implements<UCastlePCGNoiseProvider>() || Channel.IsNone())
	{
		Error = FString::Printf(TEXT("格 %s 的噪声提供者 %s 或通道 %s 无效。"),
			*Cell.ToString(), *GetPathNameSafe(Noise), *Channel.ToString());
		return false;
	}
	Value = ICastlePCGNoiseProvider::Execute_SampleNoise(Noise, Seed, Cell, Channel);
	if (!FMath::IsFinite(Value) || Value < 0.0f || Value >= 1.0f)
	{
		Error = FString::Printf(TEXT("格 %s 的噪声提供者 %s 在通道 %s 返回了 [0,1) 之外的值。"),
			*Cell.ToString(), *GetPathNameSafe(Noise), *Channel.ToString());
		return false;
	}
	return true;
}

TMap<FIntVector, FIntVector> FCastlePCGBoardQuery::HorizontalComponents(const FCastlePCGSemanticField& Field, bool bRoomsOnly) const
{
	TMap<FIntVector, FIntVector> Components;
	for (const FIntVector& Start : SortedCells)
	{
		if (Components.Contains(Start) || (bRoomsOnly && !Cells.FindChecked(Start).bRoom)) { continue; }
		TArray<FIntVector> Queue{Start};
		Components.Add(Start, Start);
		for (int32 Index = 0; Index < Queue.Num(); ++Index)
		{
			const FIntVector Current = Queue[Index];
			for (const ECastlePCGFace Face : Sides)
			{
				const FIntVector Next = Current + CastlePCGSemantics::FaceOffset(Face);
				const FCastlePCGBoardCell* Neighbor = Find(Next);
				const FCastlePCGSideFaceSpec* Edge = Field.SideFaces.Find({Current, Face});
				if (Neighbor && (!bRoomsOnly || Neighbor->bRoom) && !Components.Contains(Next) && Edge && Edge->IsConnected())
				{
					Components.Add(Next, Start);
					Queue.Add(Next);
				}
			}
		}
	}
	return Components;
}

FVector FCastlePCGBoardQuery::CellBottom(const FIntVector& Cell) const
{
	// 家具与梯井一起转到棋盘局部坐标，因而不依赖棋盘在关卡中的平移或转动。
	return FVector(Cell.X * Pitch, Cell.Y * Pitch, Cell.Z * LayerHeight);
}
