#include "CastleLegacyPCGAdapter.h"

#include "CastleBuildingPCGRule.h"
#include "CastlePCGSemantics.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "BuildingMergeComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

namespace CastleLegacyPCG
{
	/** 实例化模板仅用于读取蓝图构造后的静态网格；不运行 BeginPlay，不保存、不留在世界中。 */
	bool ReadVariant(UWorld* World, UClass* Class, const int32 ExpectedCount, TArray<FCastlePCGMeshTemplate>& Out, FString& Error)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.bDeferConstruction = true;
		AActor* Template = World->SpawnActor<AActor>(Class, FTransform::Identity, Params);
		if (!Template) { Error = FString::Printf(TEXT("PCG 无法生成变体模板 %s。"), *GetNameSafe(Class)); return false; }
		// SCS 注册前就关闭整 Actor 碰撞，防止只读模板短暂成为导航几何。
		Template->SetActorEnableCollision(false);
		Template->FinishSpawning(FTransform::Identity, true);
		TArray<UStaticMeshComponent*> Meshes;
		Template->GetComponents(Meshes);
		for (auto* Mesh : Meshes)
		{
			if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Mesh)) { continue; }
			if (!Mesh->GetStaticMesh()) { Error = FString::Printf(TEXT("PCG 变体 %s.%s 缺少静态网格。"), *Class->GetPathName(), *Mesh->GetName()); Template->Destroy(); return false; }
			FCastlePCGMeshTemplate& Part = Out.AddDefaulted_GetRef();
			Part.Mesh = Mesh->GetStaticMesh();
			Part.Relative = Mesh->GetComponentTransform().GetRelativeTransform(Template->GetActorTransform());
			for (int32 M = 0; M < Mesh->GetNumMaterials(); ++M) { Part.Materials.Add(Mesh->GetMaterial(M)); }
		}
		Template->Destroy();
		if (Out.Num() != ExpectedCount) { Error = FString::Printf(TEXT("PCG 变体 %s 应有 %d 个网格，实际 %d。"), *Class->GetPathName(), ExpectedCount, Out.Num()); return false; }
		int32 Slabs = 0, Ladders = 0, Railings = 0;
		for (FCastlePCGMeshTemplate& Part : Out)
		{
			const FBox Box = Part.Mesh->GetBoundingBox().TransformBy(Part.Relative.ToMatrixWithScale());
			const FVector Size = Box.GetSize();
			const bool bSlab = FMath::IsNearlyEqual(Box.Max.Z, 560.0, 1.0) && FMath::IsNearlyEqual(Size.Z, 31.55, 1.0)
				&& FMath::IsNearlyEqual(Size.X, 410.0, 1.0) && FMath::IsNearlyEqual(Size.Y, 410.0, 1.0);
			const bool bLadder = FMath::IsNearlyZero(Box.Min.Z, 1.0) && FMath::IsNearlyEqual(Box.Max.Z, 528.45, 1.0)
				&& Size.X < 50.0 && Size.Y > 70.0 && Size.Y < 150.0;
			// 仅旧模板使用已验收的几何契约；V4 的归属来自持久化 Group。
			Slabs += bSlab;
			Ladders += bLadder;
			if (bSlab) { Part.Group = TEXT("UpperFloor"); }
			if (bLadder) { Part.Group = TEXT("Ladder"); }
			Railings += FMath::IsNearlyEqual(Size.Z, 162.503, 1.0) && Size.Y < 10.0
				&& FMath::IsNearlyEqual(Size.X, 530.0, 1.0);
		}
		if ((ExpectedCount == 1 && Railings != 1) || (ExpectedCount == 2 && (Slabs != 1 || Ladders != 1)))
		{
			Error = FString::Printf(TEXT("PCG变体 %s 几何语义无效：护栏=%d，顶板=%d，竖梯=%d。"), *Class->GetPathName(), Railings, Slabs, Ladders);
			return false;
		}
		return true;
	}
	/** 将网格模板放入建筑局部坐标，动态件不继承模板 Actor 的运行逻辑。 */
	FCastlePCGMeshTemplate PlaceTemplate(const FCastlePCGMeshTemplate& Template, const FTransform& Frame, const AActor* Building)
	{
		FCastlePCGMeshTemplate Result = Template;
		Result.Relative = (Template.Relative * Frame).GetRelativeTransform(Building->GetActorTransform());
		return Result;
	}

	/** 只把显式请求的一组变体零件装配到本侧，始终使用同一个井口框架。 */
	void AddLegacyStairGroup(const CastleLegacyPCG::FVariants& Variants, const CastleV4PCG::FStairInterface& Stair,
		const ACastleBuildingBase& Building, FName Group, FCastlePCGApplyPlan& Plan)
	{
		for (const FCastlePCGMeshTemplate& Template : Variants.LV2)
		{
			if (Template.Group != Group) { continue; }
			FCastlePCGMeshTemplate Part = PlaceTemplate(Template, Stair.WorldFrame, &Building);
			Part.bSelectable = true;
			Plan.Visual.Visuals.Add(MoveTemp(Part));
		}
	}
	void AddHeightTransition(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FIntVector& Cell,
		const FIntVector& Side, const UCastlePCGRuleSetDataAsset& Rules, FCastlePCGApplyPlan& Plan)
	{
		constexpr double Run = 122.0;
		const double Height = Building.HachimisLayout->StandingHeight;
		const double Tilt = FMath::Atan2(-Height, Run);
		const double Width = 140.0;
		const FQuat Yaw = FVector(Side).ToOrientationQuat();
		const FTransform Frame = CastleV4PCG::CellFrame(Board, Cell).GetRelativeTransform(Building.GetActorTransform());
		const FVector Edge = FVector(Side) * (Board.CellSize * 0.5);
		const FVector SurfaceCenter = Edge + FVector(Side) * (Run * 0.5) + FVector(0, 0, Height * 0.5);
		const FQuat Rotation = Yaw * FQuat(FVector::RightVector, -Tilt);
		Plan.Gameplay.FunctionalBoxes.Add({FTransform(Rotation, SurfaceCenter - Rotation.GetUpVector() * (Rules.FloorThickness * 0.5)) * Frame,
			FVector(FMath::Sqrt(Run * Run + Height * Height) * 0.5 + 2.0, Width * 0.5, Rules.FloorThickness * 0.5), false});
	}

	/** 旧底板没有 V4 标签，只在旧适配器内按现有旧蓝图的楼板契约确定归属。 */
	UStaticMeshComponent* FindLegacyFloor(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FIntVector& Cell, FString& Error)
	{
		const FVector Center = CastleV4PCG::CellFrame(Board, Cell).GetLocation();
		const FVector GridPosition = Building.GetActorTransform().InverseTransformPosition(Center);
		TArray<UStaticMeshComponent*> Floors, Matches;
		Building.GetComponents(Floors);
		for (UStaticMeshComponent* Floor : Floors)
		{
			if (!UBuildingGameplayRootComponent::IsInGameplayHierarchy(Floor) || !Floor->CanEverAffectNavigation()
				|| !Floor->GetName().StartsWith(TEXT("GameplayFloor_"))) { continue; }
			if (!IsValid(Floor->GetStaticMesh()))
			{
				Error = Floor->GetPathName() + TEXT(" 缺少旧功能楼板网格。");
				return nullptr;
			}
			const FTransform Relative = Floor->GetComponentTransform().GetRelativeTransform(Building.GetActorTransform());
			const FBox Bounds = Floor->GetStaticMesh()->GetBoundingBox().TransformBy(Relative.ToMatrixWithScale());
			if (Bounds.IsInsideXY(GridPosition) && FMath::IsNearlyEqual(Bounds.Max.Z, GridPosition.Z, 0.1))
			{
				Matches.Add(Floor);
			}
		}
		if (Matches.Num() == 1) { return Matches[0]; }
		Error = FString::Printf(TEXT("%s 的格 %s 必须匹配一块旧功能板，实际 %d 块。"),
			*Building.GetPathName(), *Cell.ToString(), Matches.Num());
		return nullptr;
	}
}

bool CastleLegacyPCG::ReadVariants(UWorld& World, const UCastlePCGRuleSetDataAsset& Rules, FVariants& Out, FString& Error)
{
	// 无旧建筑时调用方不会进入此入口，也无需加载或生成旧模板。
	if (!Rules.VariantLV1ActorClass || !Rules.VariantLV2ActorClass || !Rules.FlatFloorSlabMesh)
	{
		Error = Rules.GetPathName() + TEXT(" 缺少旧 LV1/LV2 变体或封闭楼板。");
		return false;
	}
	return ReadVariant(&World, Rules.VariantLV1ActorClass, 1, Out.LV1, Error)
		&& ReadVariant(&World, Rules.VariantLV2ActorClass, 2, Out.LV2, Error);
}

bool CastleLegacyPCG::Extract(ACastleBuildingBase& Building, const ACastleBoard& Board, TArray<FCastlePCGPartAnnotation>& OutParts, FString& Error)
{
	const TArray<FIntVector> Cells = Building.GetOccupiedCoordinates();
	FIntVector Minimum = Cells[0];
	for (const FIntVector& Cell : Cells) { Minimum = Minimum.ComponentMin(Cell); }
	const FTransform BoardInverse = Board.GetActorTransform().Inverse();
	TArray<UStaticMeshComponent*> Meshes;
	Building.GetComponents(Meshes);
	TArray<FCastlePCGPartAnnotation> Result;
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		// 功能分支、标签和已生成 PCG 件不属于原始模型；显隐不能用来判断动画中的原件。
		if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Mesh)
			|| Mesh->ComponentHasTag(TEXT("MergeExclude")) || Mesh->ComponentHasTag(TEXT("CastlePCGDynamic"))
			|| Mesh->HasAnyFlags(RF_Transient)) { continue; }
		if (!Mesh->GetStaticMesh())
		{
			Error = FString::Printf(TEXT("%s.%s 缺少静态网格。"), *Building.GetPathName(), *Mesh->GetName()); return false;
		}
		const FTransform ToBoard = Mesh->GetComponentTransform() * BoardInverse;
		const FBox Box = Mesh->GetStaticMesh()->GetBoundingBox().TransformBy(ToBoard.ToMatrixWithScale());
		const FVector Size = Box.GetSize();
		const FVector Center = Box.GetCenter();
		FCastlePCGPartAnnotation Part;
		Part.Component = Mesh;
		Part.bOriginalVisible = Building.BuildingMerge->GetStableVisibility(Mesh);
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
		else if (Size.Z > 80.0 && Size.Z < Board.LayerHeight && FMath::Min(Size.X, Size.Y) >= 350.0)
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
				Error = FString::Printf(TEXT("%s.%s 的 PCGRole 标签格式非法或重复。"), *Building.GetPathName(), *Mesh->GetName()); return false;
			}
			bExplicitRole = true;
			if (Value == TEXT("Roof")) { Part.Role = ECastlePCGPartRole::Roof; }
			else if (Value == TEXT("TopCeilingSlab")) { Part.Role = ECastlePCGPartRole::TopCeilingSlab; }
			else if (Value == TEXT("PerimeterRailing")) { Part.Role = ECastlePCGPartRole::PerimeterRailing; }
			else if (Value == TEXT("Structural")) { Part.Role = ECastlePCGPartRole::Structural; }
			else { Error = FString::Printf(TEXT("%s.%s 的 PCGRole=%s 拼写非法。"), *Building.GetPathName(), *Mesh->GetName(), *Value); return false; }
		}
		// 用实际占格中心的最近距离确定归属；梁柱可跨格，但不参与单格替换。
		double BestDistance = TNumericLimits<double>::Max();
		FIntVector Nearest = Minimum;
		for (const FIntVector& Cell : Cells)
		{
			FVector CellCenter = BoardInverse.TransformPosition(Board.GetGridWorldLocation(Cell));
			CellCenter.Z -= Board.LayerHeight * 0.5;
			const double RoleZ = Part.Role == ECastlePCGPartRole::Roof ? Box.Min.Z - Board.LayerHeight
				: (Part.Role == ECastlePCGPartRole::TopCeilingSlab ? Box.Max.Z - Board.LayerHeight : Box.Min.Z);
			const double Distance = FVector::DistSquared(FVector(Center.X, Center.Y, RoleZ), CellCenter);
			if (Distance < BestDistance || (FMath::IsNearlyEqual(Distance, BestDistance) && CastlePCGSemantics::CellLess(Cell, Nearest)))
			{ BestDistance = Distance; Nearest = Cell; }
		}
		Part.LocalCellOffset = Nearest - Minimum;
		Part.CoveredCellOffsets.Add(Part.LocalCellOffset);
		if (Part.Role == ECastlePCGPartRole::PerimeterRailing)
		{
			const FVector CellCenter = BoardInverse.TransformPosition(Board.GetGridWorldLocation(Nearest));
			const bool bThinX = Size.X < Size.Y;
			Part.BoardFacing = bThinX ? (Center.X > CellCenter.X ? ECastlePCGFace::PositiveX : ECastlePCGFace::NegativeX)
				: (Center.Y > CellCenter.Y ? ECastlePCGFace::PositiveY : ECastlePCGFace::NegativeY);
			Part.CoveredCellOffsets.Reset();
			for (const FIntVector& Cell : Cells)
			{
				const FVector P = BoardInverse.TransformPosition(Board.GetGridWorldLocation(Cell));
				const bool bSameLayer = Cell.Z == Nearest.Z;
				const bool bCovered = bThinX ? (Cell.X == Nearest.X && P.Y > Box.Min.Y && P.Y < Box.Max.Y)
					: (Cell.Y == Nearest.Y && P.X > Box.Min.X && P.X < Box.Max.X);
				if (bSameLayer && bCovered) { Part.CoveredCellOffsets.Add(Cell - Minimum); }
			}
			if (Part.CoveredCellOffsets.IsEmpty() || Part.CoveredCellOffsets.Num() > 2)
			{
				Error = FString::Printf(TEXT("%s.%s 护栏覆盖格数非法：%d。"), *Building.GetPathName(), *Mesh->GetName(), Part.CoveredCellOffsets.Num()); return false;
			}
		}
		Result.Add(Part);
	}
	if (Result.IsEmpty()) { Error = FString::Printf(TEXT("%s 没有原始美术零件。"), *Building.GetPathName()); return false; }
	OutParts = MoveTemp(Result);
	return true;
}

FCastleV4Hatch CastleLegacyPCG::LadderHatch(const double LayerHeight)
{
	// 旧 LV2 网格已验收的落脚点和缺角，与该模板的作者原点共用。
	FCastleV4Hatch Hatch;
	Hatch.BottomStand = FVector(-58.317, 40.0, 0.0);
	Hatch.TopStand = FVector(40.0, 40.0, LayerHeight);
	Hatch.LadderBottom = FVector(-58.317, 150.0, 0.0);
	Hatch.LadderTop = FVector(-58.317, 150.0, LayerHeight);
	Hatch.Opening = FBox(FVector(-205.0, 100.0, LayerHeight - 31.55), FVector(-50.0, 205.0, LayerHeight));
	return Hatch;
}

bool CastleLegacyPCG::Prepare(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
	const FVariants& Variants, const FCastlePCGSemanticField& Field, const CastleV4PCG::FStairs& Stairs,
	TConstArrayView<FCastlePCGPartAnnotation> OriginalParts, FCastlePCGApplyPlan& Plan, FString& Error)
{
	const auto& OccupiedCells = Building.GetOccupiedCoordinates();
	FIntVector Minimum = OccupiedCells[0];
	for (const auto& Cell : OccupiedCells) { Minimum = Minimum.ComponentMin(Cell); }
	const FTransform BoardInverse = Board.GetActorTransform().Inverse();
	// 这里的功能盒明确表达桥、封闭楼板或阻隔，不是缺少资产时使用的替代美术。
	auto AddBox = [&](FVector Center, FVector Extent, bool bIsNavigationBarrier, FQuat Rotation = FQuat::Identity)
	{
		const FTransform World = FTransform(Rotation, Center) * Board.GetActorTransform();
		Plan.Gameplay.FunctionalBoxes.Add({World.GetRelativeTransform(Building.GetActorTransform()), Extent, bIsNavigationBarrier});
	};
	for (const FIntVector& Cell : Building.GetOccupiedCoordinates())
	{
		const FCastlePCGVerticalSpec* Interface = Field.VerticalInterfaces.Find(Cell);
		if (!Interface || Interface->State == ECastlePCGVertical::None) { continue; }
		const bool bLadder = Interface->State == ECastlePCGVertical::Ladder;
		bool bHasSlab = false;
		for (const auto& Part : OriginalParts)
		{
			if (!Part.CoveredCellOffsets.Contains(Cell - Minimum)) { continue; }
			if (Part.Role == ECastlePCGPartRole::Roof) { Plan.Visual.Hidden.Add(Part.Component); }
			if (Part.Role == ECastlePCGPartRole::TopCeilingSlab)
			{
				bHasSlab |= Part.bOriginalVisible;
				if (bLadder) { Plan.Visual.Hidden.Add(Part.Component); }
			}
		}
		if (bLadder)
		{
			const CastleV4PCG::FStairInterface* Outgoing = Stairs.Find(Cell);
			if (!Outgoing || Outgoing->Kit)
			{
				Error = FString::Printf(TEXT("%s 在格 %s 缺少旧变体的层界框架。"), *Building.GetPathName(), *Cell.ToString());
				return false;
			}
			// 下层只拥有竖梯和 Link，上层四片功能板围住井口，避免整片封闭顶板堵住洞口。
			AddLegacyStairGroup(Variants, *Outgoing, Building, TEXT("Ladder"), Plan);
			CastleV4PCG::AddLink(Plan, Cell, Outgoing->WorldFrame.GetRelativeTransform(Building.GetActorTransform()), Outgoing->Hatch);
			continue;
		}
		const FTransform Frame = CastleV4PCG::CellFrame(Board, Cell);
		if (!bHasSlab)
		{
			FCastlePCGMeshTemplate Slab;
			Slab.Mesh = Rules.FlatFloorSlabMesh;
			for (const auto& Material : Slab.Mesh->GetStaticMaterials()) { Slab.Materials.Add(Material.MaterialInterface); }
			Plan.Visual.Visuals.Add(PlaceTemplate(Slab, Frame, &Building));
		}
		// 上层正式建筑本身带功能地板；封闭顶面补齐与变体同高的下层所属功能面。
		FVector Top = BoardInverse.TransformPosition(Frame.GetLocation());
		Top.Z += Board.LayerHeight - Rules.FloorThickness * 0.5;
		AddBox(Top, FVector(205.0, 205.0, Rules.FloorThickness * 0.5), false);
	}
	for (const FIntVector& Cell : Building.GetOccupiedCoordinates())
	{
		const bool bDeckOverride = Field.Cells.FindChecked(Cell).DeckOverride.IsSet();
		const CastleV4PCG::FStairInterface* Incoming = Stairs.Find(Cell - FCastlePCGBoardQuery::Up);
		if (bDeckOverride || (OccupiedCells.Num() == 1 && Incoming && Incoming->Kit))
		{
			// 旧单格原型的 StaticMesh 是原装底板视觉，GameplayRoot 下的功能板由玩法阶段独立处理。
			// 单格接入 V4 带孔楼板时隐藏原板。多格原型没有这个组件命名约定，仍按原流程逐格切功能板。
			const FCastlePCGPartAnnotation* OriginalDeck = OriginalParts.FindByPredicate([&](const FCastlePCGPartAnnotation& Part)
			{
				return Part.LocalCellOffset == Cell - Minimum && Part.Component->GetFName() == TEXT("StaticMesh");
			});
			if (!OriginalDeck)
			{
				Error = FString::Printf(TEXT("%s 在格 %s 缺少旧单格原装底板 StaticMesh。"), *Building.GetPathName(), *Cell.ToString());
				return false;
			}
			Plan.Visual.Hidden.Add(OriginalDeck->Component);
		}
		// 旧上层使用自身站立标高，来梯可来自 V4 套件或旧模板。
		if (Incoming)
		{
			UStaticMeshComponent* Floor = FindLegacyFloor(Board, Building, Cell, Error);
			if (!Floor || !CastleV4PCG::CutDeck(Board, Building, Cell, 0.0, *Floor, *Incoming, Plan, Error)) { return false; }
			// 带孔楼板与竖梯共用下层套件几何，上层仅持有 UpperFloor。
			if (Incoming->Kit)
			{
				if (!CastleV4PCG::AddStairGroup(Building, *Incoming, TEXT("UpperFloor"), Plan, Error)) { return false; }
			}
			else
			{
				AddLegacyStairGroup(Variants, *Incoming, Building, TEXT("UpperFloor"), Plan);
			}
		}
	}
	for (const auto& Part : OriginalParts)
	{
		if (Part.Role == ECastlePCGPartRole::Roof && Part.CoveredCellOffsets.ContainsByPredicate([&](const FIntVector& Offset)
		{ return Field.Cells.FindChecked(Minimum + Offset).Roof == ECastlePCGRoof::Override; }))
		{
			Plan.Visual.Hidden.Add(Part.Component);
		}
		if (Part.Role != ECastlePCGPartRole::PerimeterRailing || !Part.bOriginalVisible) { continue; }
		int32 ConnectedCount = 0;
		for (const auto& Offset : Part.CoveredCellOffsets)
		{
			ConnectedCount += Field.SideFaces.FindChecked({Minimum + Offset, Part.BoardFacing}).IsConnected() ? 1 : 0;
		}
		if (ConnectedCount == 0) { continue; }
		Plan.Visual.Hidden.Add(Part.Component);
		UStaticMeshComponent* Original = CastChecked<UStaticMeshComponent>(Part.Component);
		const FBox Box = Original->GetStaticMesh()->GetBoundingBox().TransformBy((Original->GetComponentTransform() * BoardInverse).ToMatrixWithScale());
		const bool bThinX = Box.GetSize().X < Box.GetSize().Y;
		TArray<FIntVector> Offsets = Part.CoveredCellOffsets;
		Offsets.Sort(CastlePCGSemantics::CellLess);
		const double SegmentLength = (bThinX ? Box.GetSize().Y : Box.GetSize().X) / Offsets.Num();
		for (int32 Index = 0; Index < Offsets.Num(); ++Index)
		{
			const bool bConnected = Field.SideFaces.FindChecked({Minimum + Offsets[Index], Part.BoardFacing}).IsConnected();
			FCastlePCGMeshTemplate Template;
			if (bConnected) { Template = Variants.LV1[0]; }
			else
			{
				// 指南 D4 指定复用同蓝图的单格护栏；找不到必须报错，不能保留双格或借用其他建筑。
				const FCastlePCGPartAnnotation* Reference = OriginalParts.FindByPredicate([&](const FCastlePCGPartAnnotation& Candidate)
				{
					if (Candidate.Role != ECastlePCGPartRole::PerimeterRailing || Candidate.CoveredCellOffsets.Num() != 1 || !Candidate.bOriginalVisible) { return false; }
					const auto* ReferenceComponent = CastChecked<UStaticMeshComponent>(Candidate.Component);
					const FVector ReferenceSize = ReferenceComponent->GetStaticMesh()->GetBoundingBox().TransformBy(
						(ReferenceComponent->GetComponentTransform() * BoardInverse).ToMatrixWithScale()).GetSize();
					return FMath::IsNearlyEqual(FMath::Max(ReferenceSize.X, ReferenceSize.Y), SegmentLength, 1.0)
						&& FMath::IsNearlyEqual(ReferenceSize.Z, Box.GetSize().Z, 1.0);
				});
				if (!Reference) { Error = FString::Printf(TEXT("%s 跨格护栏 %s 缺少本蓝图单格参考护栏。"), *Building.GetPathName(), *Original->GetName()); return false; }
				UStaticMeshComponent* ReferenceMesh = CastChecked<UStaticMeshComponent>(Reference->Component);
				Template.Mesh = ReferenceMesh->GetStaticMesh();
				for (int32 M = 0; M < ReferenceMesh->GetNumMaterials(); ++M) { Template.Materials.Add(ReferenceMesh->GetMaterial(M)); }
			}
			const FBox MeshBox = Template.Mesh->GetBoundingBox();
			const bool bMeshAlongX = MeshBox.GetSize().X > MeshBox.GetSize().Y;
			const double Length = bMeshAlongX ? MeshBox.GetSize().X : MeshBox.GetSize().Y;
			const FQuat Rotation(FVector::UpVector, (bThinX == bMeshAlongX) ? HALF_PI : 0.0);
			FVector Scale = FVector::OneVector;
			if (bMeshAlongX) { Scale.X = SegmentLength / Length; } else { Scale.Y = SegmentLength / Length; }
			FVector Center = Box.GetCenter();
			if (bThinX) { Center.Y = Box.Min.Y + SegmentLength * (Index + 0.5); }
			else { Center.X = Box.Min.X + SegmentLength * (Index + 0.5); }
			const FTransform Railing(Rotation, Center - Rotation.RotateVector(Scale * MeshBox.GetCenter()), Scale);
			Template.Relative = (Railing * Board.GetActorTransform()).GetRelativeTransform(Building.GetActorTransform());
			Plan.Visual.Visuals.Add(Template);
		}
	}
	for (const FIntVector& Cell : Building.GetOccupiedCoordinates())
	for (const ECastlePCGFace Direction : FCastlePCGBoardQuery::Sides)
	{
		const FCastlePCGSideFaceSpec& Face = Field.SideFaces.FindChecked({Cell, Direction});
		if (!Face.bHasNeighbor && Face.State != ECastlePCGSide::MainTowerDoor) { continue; }
		const FIntVector Offset = CastlePCGSemantics::FaceOffset(Direction);
		FVector Center = BoardInverse.TransformPosition(Board.GetGridWorldLocation(Cell));
		Center.Z -= Board.LayerHeight * 0.5;
		Center += FVector(Offset) * (Board.CellSize * 0.5);
		const double HalfLength = (Board.CellSize - Board.GroundTileSize) * 0.5 + 40.0;
		const FQuat Rotation(FVector::UpVector, Offset.Y != 0 ? HALF_PI : 0.0);
		if (Face.IsConnected())
		{
			Center.Z -= Rules.FloorThickness * 0.5;
			AddBox(Center, FVector(HalfLength, Rules.BridgeWidth * 0.5, Rules.FloorThickness * 0.5), false, Rotation);
		}
		else
		{
			Center.Z += Rules.BarrierHeight * 0.5;
			AddBox(Center, FVector(10.0, Board.GroundTileSize * 0.5, Rules.BarrierHeight * 0.5), true, Rotation);
		}
	}
	return true;
}

void CastleLegacyPCG::PrepareMixed(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
	const FVariants& Variants, const FCastlePCGSemanticField& Field, const CastleV4PCG::FStairs& Stairs, FCastlePCGApplyPlan& Plan)
{
	for (const FIntVector& Cell : Building.GetOccupiedCoordinates())
	{
		for (const ECastlePCGFace Direction : FCastlePCGBoardQuery::Sides)
		{
			const FCastlePCGSideFaceSpec& Face = Field.SideFaces.FindChecked({Cell, Direction});
			if (Face.bNeighborLegacy && Face.IsDoor())
			{
				AddHeightTransition(Board, Building, Cell, CastlePCGSemantics::FaceOffset(Direction), Rules, Plan);
			}
		}
		const CastleV4PCG::FStairInterface* Incoming = Stairs.Find(Cell - FCastlePCGBoardQuery::Up);
		if (Incoming && !Incoming->Kit)
		{
			// 旧下层的带洞板也安装到上层所在空间。竖梯仍由旧下层自己的准备函数安装。
			AddLegacyStairGroup(Variants, *Incoming, Building, TEXT("UpperFloor"), Plan);
		}
	}
}
