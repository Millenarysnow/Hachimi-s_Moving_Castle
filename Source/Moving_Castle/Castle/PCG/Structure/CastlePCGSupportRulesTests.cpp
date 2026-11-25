#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastlePCGSupportRules.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Castle/PCG/HachimisV4/HachimisV4MeshValidation.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"

namespace CastlePCGSupportTests
{
	/** 使用重载后的正式资产覆盖跨进程引用和每个纯结构规则的实际配置。 */
	const UCastlePCGRuleSetDataAsset* LoadRules(FAutomationTestBase& Test)
	{
		const auto* Rules = LoadObject<UCastlePCGRuleSetDataAsset>(nullptr,
			TEXT("/Game/MovingCastle/Data/DA_CastlePCGRuleSet.DA_CastlePCGRuleSet"));
		if (!Test.TestNotNull(TEXT("正式规则集可从磁盘加载"), Rules)) { return nullptr; }
		FString Error;
		if (!Rules->ValidateConfiguration(Error))
		{
			Test.AddError(Error);
			return nullptr;
		}
		return Rules;
	}

	template<typename TRule>
	const TRule* FindRule(const UCastlePCGRuleSetDataAsset& Rules)
	{
		for (const UCastleBuildingPCGRule* Rule : Rules.Rules)
		{
			if (const TRule* Match = Cast<TRule>(Rule)) { return Match; }
		}
		return nullptr;
	}

	/** 用分离轴求三角投影与格心矩形相交，避免整套包围盒把凹缺口误判成屋面。 */
	bool IntersectsCore(const FVector2D (&Vertices)[3], const FVector2D& Center, double Half)
	{
		const FVector2D SeparatingAxes[] = {FVector2D(1, 0), FVector2D(0, 1),
			(Vertices[1] - Vertices[0]).GetRotated(90), (Vertices[2] - Vertices[1]).GetRotated(90),
			(Vertices[0] - Vertices[2]).GetRotated(90)};
		for (const FVector2D& Axis : SeparatingAxes)
		{
			const double Radius = Half * (FMath::Abs(Axis.X) + FMath::Abs(Axis.Y));
			const double A = (Vertices[0] - Center) | Axis;
			const double B = (Vertices[1] - Center) | Axis;
			const double C = (Vertices[2] - Center) | Axis;
			if (FMath::Min3(A, B, C) > Radius || FMath::Max3(A, B, C) < -Radius) { return false; }
		}
		return true;
	}

	/** 以非零起点的棋盘快照覆盖层高、最小格挂点和上方建筑的相对坐标。 */
	struct FQuadRoofFixture
	{
		static inline const FIntVector Root = FIntVector(8, 9, 0);
		static inline const FIntVector Quadrants[] = {FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(1, 1, 0)};

		static FCastlePCGBoardQuery Query(const UCastlePCGRuleSetDataAsset& Rules, FName Family, FIntVector Grid, uint8 Mask,
			bool bRoom = false, bool bOverhead = false)
		{
			auto* Layout = NewObject<UCastleHachimisV4Layout>();
			Layout->Family = Family;
			Layout->Grid = Grid;
			FCastlePCGBoardCell Lower;
			Lower.Layout = Layout;
			Lower.BuildingRoot = Root;
			Lower.BuildingCellCount = Grid.X * Grid.Y * Grid.Z;
			Lower.bRoom = bRoom;
			TMap<FIntVector, FCastlePCGBoardCell> Cells;
			for (int32 Z = 0; Z < Grid.Z; ++Z)
			for (int32 Y = 0; Y < Grid.Y; ++Y)
			for (int32 X = 0; X < Grid.X; ++X)
			{
				Cells.Add(Root + FIntVector(X, Y, Z), Lower);
			}
			for (int32 Bit = 0; Bit < UE_ARRAY_COUNT(Quadrants); ++Bit)
			{
				if ((Mask & (1 << Bit)) == 0) { continue; }
				const FIntVector Cell = Root + Quadrants[Bit] + FCastlePCGBoardQuery::Up * Grid.Z;
				FCastlePCGBoardCell Upper;
				Upper.BuildingRoot = Cell;
				Upper.BuildingCellCount = 1;
				Cells.Add(Cell, Upper);
			}
			if (bOverhead)
			{
				// 最右上的顶格紧邻上一层留空，隔一层有悬挑。
				const FIntVector Cell = Root + Quadrants[3] + FCastlePCGBoardQuery::Up * (Grid.Z + 1);
				FCastlePCGBoardCell Cantilever;
				Cantilever.BuildingRoot = Cell;
				Cantilever.BuildingCellCount = 1;
				Cells.Add(Cell, Cantilever);
			}
			return FCastlePCGBoardQuery(MoveTemp(Cells), Rules, nullptr, 0, 0);
		}

		static FCastlePCGSemanticField Field(const FCastlePCGBoardQuery& Query)
		{
			FCastlePCGSemanticField Result;
			for (const FIntVector& Cell : Query.OrderedCells())
			{
				// 复现 Topology 对独立上层建筑压住格的既有屋顶显隐语义。
				FCastlePCGCellSpec& Spec = Result.Cells.Add(Cell);
				const auto* Above = Query.Find(Cell + Query.Up);
				if (Above && Above->BuildingRoot != Query.Find(Cell)->BuildingRoot) { Spec.Roof = ECastlePCGRoof::Hidden; }
			}
			return Result;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGSupportAssets, "MovingCastle.Castle.PCG.Structure.Assets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePCGSupportAssets::RunTest(const FString& Parameters)
{
	using namespace CastlePCGSupportTests;
	const auto* Rules = LoadRules(*this);
	if (!Rules) { return false; }
	const auto* Brace = FindRule<UCastlePCGOverhangBraceRule>(*Rules);
	const auto* Step = FindRule<UCastlePCGHorizontalLevel2StepRule>(*Rules);
	const auto* QuadRoof = FindRule<UCastlePCGQuadRoofOverlayRule>(*Rules);
	if (!TestNotNull(TEXT("正式规则集包含四格叠层屋面规则"), QuadRoof)) { return false; }
	TSet<const UStaticMesh*> Meshes;
	TSet<const UCastlePCGAttachmentKit*> Kits;
	const auto CheckKit = [&](const UCastlePCGAttachmentKit* Kit)
	{
		Kits.Add(Kit);
		for (const FCastleV4Part& Part : Kit->Parts)
		{
			const auto* Mesh = Part.Mesh.Get();
			Meshes.Add(Mesh);
			const FBox Bounds = Mesh->GetBoundingBox().TransformBy(Part.Local.ToMatrixWithScale());
			TestTrue(Mesh->GetName() + TEXT(" 持久化网格有实际几何及原生材质"), Bounds.IsValid && Bounds.GetVolume() > 0
				&& !Mesh->GetStaticMaterials().IsEmpty() && !Mesh->GetStaticMaterials().ContainsByPredicate(
					[](const FStaticMaterial& Slot) { return !Slot.MaterialInterface; }));
			if (Part.Group == TEXT("Deck"))
			{
				// 角钉凸出铺面。用持久化网格在 40.8 cm 标高上的顶点验证铺面范围。
				const FMeshDescription* Source = Mesh->GetMeshDescription(0);
				if (TestNotNull(Mesh->GetName() + TEXT(" 的持久化网格源"), Source))
				{
					const auto Positions = FStaticMeshConstAttributes(*Source).GetVertexPositions();
					FBox WalkingSurface(ForceInit);
					for (const FVertexID Vertex : Source->Vertices().GetElementIDs())
					{
						const FVector Position = Part.Local.TransformPosition(FVector(Positions[Vertex]));
						if (FMath::IsNearlyEqual(Position.Z, 40.8, 0.1)) { WalkingSurface += Position; }
					}
					TestTrue(Mesh->GetName() + TEXT(" 的 40.8 cm 标高覆盖铺面"),
						WalkingSurface.IsValid && WalkingSurface.GetSize().X > 450.0 && WalkingSurface.GetSize().Y > 450.0);
				}
				// 六族作者边框在格底中心的 ±257.5 cm，角钉最高点为 42.4 cm。
				TestTrue(FString::Printf(TEXT("%s 保留作者边框和角钉尺寸，实测 %s"), *Mesh->GetName(), *Bounds.ToString()),
					FMath::IsNearlyEqual(Bounds.Min.X, -257.5, 0.1) && FMath::IsNearlyEqual(Bounds.Min.Y, -257.5, 0.1)
					&& FMath::IsNearlyEqual(Bounds.Max.X, 257.5, 0.1) && FMath::IsNearlyEqual(Bounds.Max.Y, 257.5, 0.1)
					&& FMath::IsNearlyEqual(Bounds.Max.Z, 42.4, 0.1));
			}
		}
	};
	for (const FName Family : CastlePCGSupport::Families())
	{
		for (const ECastlePCGBraceKind Kind : CastlePCGSupport::BraceKinds())
		{
			CheckKit(Brace->FamilyKits.FindChecked(Family).ByKind.FindChecked(Kind));
		}
		const FCastlePCGStepKits& StepKits = Step->FamilyKits.FindChecked(Family);
		CheckKit(StepKits.Slope);
		CheckKit(StepKits.Deck);
	}
	TestEqual(TEXT("48 份不同的 FBX 网格"), Meshes.Num(), 48);
	TestEqual(TEXT("30 套斜撑及 12 套坡顶/宽边板"), Kits.Num(), 42);

	const FString Root = TEXT("/Game/MovingCastle/Model/Buildings/HachimisV4/QuadRoofOverlay/");
	const auto AssetPath = [&Root](const FString& Name) { return Root + Name + TEXT(".") + Name; };
	const auto* Palette = LoadObject<UMaterial>(nullptr, *AssetPath(TEXT("M_HC515_RoofPalette")));
	const auto* BaseColor = LoadObject<UTexture2D>(nullptr, *AssetPath(TEXT("T_HC515_Roof_BaseColor")));
	const auto* ORM = LoadObject<UTexture2D>(nullptr, *AssetPath(TEXT("T_HC515_Roof_ORM")));
	if (!TestNotNull(TEXT("共享调色板材质可重载"), Palette)
		|| !TestNotNull(TEXT("基础色贴图可重载"), BaseColor) || !TestNotNull(TEXT("ORM 贴图可重载"), ORM)) { return false; }
	TestTrue(TEXT("基础色使用 sRGB 且有持久化源像素"), BaseColor->SRGB && BaseColor->Source.IsValid());
	TestTrue(TEXT("ORM 使用线性 Masks 且有持久化源像素"), !ORM->SRGB && ORM->CompressionSettings == TC_Masks && ORM->Source.IsValid());
	const UMaterialEditorOnlyData* Inputs = Palette->GetEditorOnlyData();
	const auto* BaseSample = Cast<UMaterialExpressionTextureSample>(Inputs->BaseColor.Expression);
	const auto* ORMSample = Cast<UMaterialExpressionTextureSample>(Inputs->AmbientOcclusion.Expression);
	TestTrue(TEXT("基础色使用贴图 RGB，不使用纯色常量"), BaseSample && BaseSample->Texture == BaseColor
		&& !Inputs->BaseColor.UseConstant && Inputs->BaseColor.OutputIndex == 0);
	TestTrue(TEXT("同一线性贴图的 R/G/B 准确连接 AO/粗糙度/金属度"), ORMSample && ORMSample->Texture == ORM
		&& Inputs->AmbientOcclusion.OutputIndex == 1 && Inputs->Roughness.Expression == ORMSample && Inputs->Roughness.OutputIndex == 2
		&& Inputs->Metallic.Expression == ORMSample && Inputs->Metallic.OutputIndex == 3
		&& !Inputs->AmbientOcclusion.UseConstant && !Inputs->Roughness.UseConstant && !Inputs->Metallic.UseConstant);
	// 盾塔交付仅内嵌基础色。其余输入恢复 Blender 写入 FBX 前的表面参数。
	const auto* ShieldPalette = LoadObject<UMaterial>(nullptr, *AssetPath(TEXT("M_HC515_ShieldRoofPalette")));
	const auto* ShieldColor = LoadObject<UTexture2D>(nullptr, *AssetPath(TEXT("T_HC515_ShieldRoof_BaseColor")));
	if (!TestNotNull(TEXT("盾塔调色板可重载"), ShieldPalette) || !TestNotNull(TEXT("盾塔图集可重载"), ShieldColor)) { return false; }
	const UMaterialEditorOnlyData* ShieldInputs = ShieldPalette->GetEditorOnlyData();
	const auto* ShieldSample = Cast<UMaterialExpressionTextureSample>(ShieldInputs->BaseColor.Expression);
	TestTrue(TEXT("盾塔调色板使用 1024×512 sRGB 源图集"), ShieldSample && ShieldSample->Texture == ShieldColor
		&& !ShieldInputs->BaseColor.UseConstant && ShieldColor->SRGB && ShieldColor->Source.IsValid()
		&& ShieldColor->Source.GetSizeX() == 1024 && ShieldColor->Source.GetSizeY() == 512);
	TestTrue(TEXT("盾塔粗糙度 0.5、金属度 0、高光 0.5、无自发光"), ShieldInputs->Roughness.UseConstant
		&& ShieldInputs->Roughness.Constant == 0.5f && ShieldInputs->Metallic.UseConstant
		&& ShieldInputs->Metallic.Constant == 0.0f && ShieldInputs->Specular.UseConstant
		&& ShieldInputs->Specular.Constant == 0.5f && ShieldInputs->EmissiveColor.UseConstant
		&& ShieldInputs->EmissiveColor.Constant == FColor::Black);
	Meshes.Reset();
	Kits.Reset();
	for (const FName Family : CastlePCGSupport::Families())
	{
		const FCastlePCGQuadRoofKits& FamilyKits = QuadRoof->FamilyKits.FindChecked(Family);
		const UMaterial* ExpectedPalette = Family == TEXT("Shield") ? ShieldPalette : Palette;
		for (const ECastlePCGQuadRoofKind Kind : CastlePCGSupport::QuadRoofKinds())
		{
			const UCastlePCGAttachmentKit* Kit = FamilyKits.ByKind.FindChecked(Kind);
			const FString KindName = CastlePCGSupport::QuadRoofName(Kind).ToString();
			const FString MeshName = FString::Printf(TEXT("SM_HC515_L4H_%s_%s"), *Family.ToString(), *KindName);
			const FString KitName = FString::Printf(TEXT("DA_%s_QuadRoof_%sKit"), *Family.ToString(), *KindName);
			TestEqual(KitName + TEXT(" 的正式对象路径"), Kit->GetPathName(), AssetPath(KitName));
			TestEqual(KitName + TEXT(" 只含一个视觉部件"), Kit->Parts.Num(), 1);
			const FCastleV4Part& Part = Kit->Parts[0];
			auto* Mesh = Part.Mesh.Get();
			TestEqual(MeshName + TEXT(" 的正式对象路径"), Mesh->GetPathName(), AssetPath(MeshName));
			TestEqual(MeshName + TEXT(" 来自唯一同名作者视觉节点"), Part.Node.ToString(), MeshName);
			TestTrue(MeshName + TEXT(" 只引用对应调色板"), Mesh->GetStaticMaterials().Num() == 1 && Mesh->GetMaterial(0) == ExpectedPalette);
			FString Reason;
			const bool bMeshValid = HachimisV4MeshValidation::Verify(*Mesh, Reason);
			TestTrue(MeshName + TEXT(" 通过渲染与距离场校验 ") + Reason, bMeshValid);
			if (const FMeshDescription* Source = Mesh->GetMeshDescription(0);
				TestNotNull(MeshName + TEXT(" 的持久化网格源"), Source))
			{
				const auto UVs = FStaticMeshConstAttributes(*Source).GetVertexInstanceUVs();
				TestTrue(MeshName + TEXT(" 有实际三角和 UV0"), Source->Triangles().Num() > 0 && UVs.GetNumChannels() == 1);
			}
			FTriMeshCollisionData Collision;
			TestTrue(MeshName + TEXT(" 只用视觉几何生成选中碰撞，不参与导航"), Mesh->GetBodySetup()
				&& Mesh->GetBodySetup()->CollisionTraceFlag == CTF_UseComplexAsSimple && !Mesh->bHasNavigationData
				&& Mesh->GetPhysicsTriMeshData(&Collision, true) && !Collision.Indices.IsEmpty());
			Meshes.Add(Mesh);
			Kits.Add(Kit);
		}
	}
	TestEqual(TEXT("六族各四种屋面，合计 24 独立网格"), Meshes.Num(), 24);
	TestEqual(TEXT("六族各四种屋面，合计 24 独立套件"), Kits.Num(), 24);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGOverhangStructure, "MovingCastle.Castle.PCG.Structure.Overhang",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePCGOverhangStructure::RunTest(const FString& Parameters)
{
	using namespace CastlePCGSupportTests;
	using enum ECastlePCGBraceKind;
	const auto* Rules = LoadRules(*this);
	if (!Rules) { return false; }
	auto* Layout = NewObject<UCastleHachimisV4Layout>();
	Layout->Family = TEXT("Archer");
	const FIntVector X(1, 0, 0), Y(0, 1, 0);
	const FIntVector C(2, 2, 4), Up = FCastlePCGBoardQuery::Up;
	const auto Data = [&](const FIntVector& Cell, bool bRoom = false)
	{
		FCastlePCGBoardCell Result;
		Result.BuildingRoot = Cell;
		Result.Layout = Layout;
		Result.bRoom = bRoom;
		return Result;
	};
	const auto Solve = [&](TMap<FIntVector, FCastlePCGBoardCell> Cells, TOptional<FIntVector> Tower)
	{
		FCastlePCGBoardQuery Query(MoveTemp(Cells), *Rules, nullptr, 0, C.Z - 1, Tower);
		FCastlePCGSemanticField Field;
		FString Error;
		if (!Rules->Solve(Query, Field, Error)) { AddError(Error); }
		return Field;
	};
	const auto Check = [&](const FCastlePCGSemanticField& Field, ECastlePCGBraceKind Kind)
	{
		const FName Name = CastlePCGSupport::BraceName(Kind);
		TestEqual(Name.ToString() + TEXT(" 只挂一次"), Field.GetAttachments().Num(), 1);
		const FCastlePCGAttachmentSpec* Spec = Field.GetAttachments().Find({C, TEXT("CellBottom"), TEXT("OverhangBrace")});
		if (TestNotNull(Name.ToString() + TEXT(" 由近墙首格持有"), Spec))
		{
			TestEqual(TEXT("使用悬挑格本族款式"), Spec->Kit->Parts[0].Group, Name);
			TestFalse(TEXT("斜撑不阻挡选中或导航"), Spec->bSelectable || Spec->bAffectsNavigation);
		}
	};
	Check(Solve({{C, Data(C)}, {C - X - Up, Data(C - X - Up)}}, {}), Cantilever);
	Check(Solve({{C, Data(C)}, {C - X - Up, Data(C - X - Up)}, {C + X - Up, Data(C + X - Up)}}, {}), ThroughArch);
	Check(Solve({{C, Data(C)}, {C + X, Data(C + X)}}, C - X), DeepCorbel);
	Check(Solve({{C, Data(C)}, {C + X, Data(C + X)}, {C - X - Up, Data(C - X - Up)}, {C + X * 2 - Up, Data(C + X * 2 - Up)}}, {}),
		OpposedSpan);
	// 从主塔侧双格悬挑上拆掉外格时，剩下的首格重新匹配单格资产。
	Check(Solve({{C, Data(C)}}, C - X), Cantilever);
	TestEqual(TEXT("拆除侧下承托建筑后斜撑消失"), Solve({{C, Data(C)}}, {}).GetAttachments().Num(), 0);
	TestEqual(TEXT("悬挑塔侧下只有普通房底板时没有承托斜撑"),
		Solve({{C, Data(C)}, {C - X - Up, Data(C - X - Up, true)}}, {}).GetAttachments().Num(), 0);
	// 同一普通房位置即使有占用，也不会让对侧主塔的单侧承托变成贯通拱撑。
	Check(Solve({{C, Data(C)}, {C - X - Up, Data(C - X - Up, true)}}, C + X), Cantilever);
	TestEqual(TEXT("普通房及地面层均不生成斜撑"),
		Solve({{C, Data(C, true)}, {C - X - Up, Data(C - X - Up)}}, C - X).GetAttachments().Num(), 0);
	TestEqual(TEXT("地面功能塔即使靠主塔柱也不生成斜撑"),
		Solve({{C - Up, Data(C - Up)}}, C - X).GetAttachments().Num(), 0);

	const FIntVector Walls[] = {Y * -1, X * -1, Y, X};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Walls); ++Index)
	{
		const FIntVector Primary = Walls[Index], Secondary = Walls[(Index + 1) % UE_ARRAY_COUNT(Walls)];
		const FCastlePCGSemanticField Field = Solve(
			{{C, Data(C)}, {C + Primary - Up, Data(C + Primary - Up)}, {C + Secondary - Up, Data(C + Secondary - Up)}}, {});
		Check(Field, CornerFan);
		const auto* Spec = Field.GetAttachments().Find({C, TEXT("CellBottom"), TEXT("OverhangBrace")});
		if (!Spec) { continue; }
		const FTransform& Anchor = Spec->AnchorTransform;
		const FCastleV4Part& Part = Spec->Kit->Parts[0];
		const FBox Bounds = Part.Mesh->GetBoundingBox().TransformBy((Part.Local * Anchor).ToMatrixWithScale());
		const auto MaxProjection = [&](const FIntVector& Direction)
		{
			return FVector::DotProduct(FVector(Direction), Bounds.GetCenter() + FVector(Direction) * Bounds.GetExtent());
		};
		TestTrue(TEXT("角撑挂点贴主墙且两轴朝向正确"), Anchor.GetLocation().Equals(FVector(Primary) * 257.5, 0.01)
			&& Anchor.TransformVectorNoScale(-FVector::RightVector).Equals(FVector(Primary), 0.01)
			&& Anchor.TransformVectorNoScale(-FVector::ForwardVector).Equals(FVector(Secondary), 0.01));
		// 作者根坐标内实测墙靴极值为 -22.47 cm（Y）、-279.97 cm（X）。
		// 主墙多出 22.47 cm，副墙多出同样的搭接；旋转四次后都应贴在 257.5 cm 墙沿处。
		TestTrue(TEXT("主墙与副墙靴板包围盒均按作者尺寸搭接，且没有镜像缩放"),
			Anchor.GetScale3D().Equals(FVector::OneVector) && FMath::IsNearlyEqual(MaxProjection(Primary), 279.97, 1.0)
			&& FMath::IsNearlyEqual(MaxProjection(Secondary), 279.97, 1.0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGStepSkyExposure, "MovingCastle.Castle.PCG.Structure.StepSkyExposure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePCGStepSkyExposure::RunTest(const FString& Parameters)
{
	const auto* Rules = CastlePCGSupportTests::LoadRules(*this);
	if (!Rules) { return false; }
	auto* Layout = NewObject<UCastleHachimisV4Layout>();
	Layout->Family = TEXT("Archer");
	Layout->Grid = FIntVector(2, 1, 1);
	const FIntVector Covered(1, 1, 0), Exposed(2, 1, 0), Up = FCastlePCGBoardQuery::Up;
	FCastlePCGBoardCell Lower;
	Lower.Layout = Layout;
	Lower.BuildingRoot = Covered;
	Lower.BuildingCellCount = 2;
	FCastlePCGBoardCell Upper;
	Upper.BuildingRoot = Covered + Up;
	Upper.BuildingCellCount = 1;
	const auto* Step = CastlePCGSupportTests::FindRule<UCastlePCGHorizontalLevel2StepRule>(*Rules);
	if (!TestNotNull(TEXT("正式水平二级叠放规则"), Step)) { return false; }
	for (const bool bOverhead : {false, true})
	for (const bool bHatch : {false, true})
	{
		TMap<FIntVector, FCastlePCGBoardCell> Cells = {{Covered, Lower}, {Exposed, Lower}, {Covered + Up, Upper}};
		if (bOverhead)
		{
			// 从已叠一侧的更高邻格侧挑过来，外露格紧邻上一层依然是空格。
			FCastlePCGBoardCell Cantilever = Upper;
			Cantilever.BuildingRoot = Exposed + Up * 2;
			Cells.Add(Exposed + Up * 2, Cantilever);
		}
		FCastlePCGBoardQuery Query(MoveTemp(Cells), *Rules, nullptr, 0, 0);
		FCastlePCGSemanticField Field;
		Field.Cells.Add(Exposed);
		FCastlePCGCellSpec& Deck = Field.Cells.Add(Covered + Up);
		Deck.Deck = bHatch ? ECastlePCGDeck::Hatch : ECastlePCGDeck::Solid;
		FString Error;
		if (!Step->Evaluate(Query, Field, Error)) { AddError(Error); return false; }
		TestNull(TEXT("外露侧的紧邻上一层保持空置"), Query.Find(Exposed + Up));
		TestEqual(TEXT("隔层悬挑遮挡时停用半坡和上层地板覆写"),
			Field.Cells.FindChecked(Exposed).Roof == ECastlePCGRoof::Override, !bOverhead);
		TestEqual(TEXT("通天且无梯时才生成实心宽边地板"),
			Deck.DeckOverride.IsSet(), !bOverhead && !bHatch);
		TestEqual(TEXT("覆写规则保留拓扑阶段的底盘玩法语义"), Deck.Deck == ECastlePCGDeck::Hatch, bHatch);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGQuadRoofGeometry, "MovingCastle.Castle.PCG.Structure.QuadRoofGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePCGQuadRoofGeometry::RunTest(const FString& Parameters)
{
	using namespace CastlePCGSupportTests;
	using enum ECastlePCGQuadRoofKind;
	const auto* Rules = LoadRules(*this);
	if (!Rules) { return false; }
	const auto* Rule = FindRule<UCastlePCGQuadRoofOverlayRule>(*Rules);
	if (!TestNotNull(TEXT("正式规则集含四格屋面结构规则"), Rule)) { return false; }
	const ECastlePCGQuadRoofKind Expected[] = {
		Corner, Corner, Row, Corner, Row, Diagonal, LastBay, Corner, Diagonal, Row, LastBay, Row, LastBay, LastBay};
	const FTransform BoardWorld(FRotator(0, 90, 0), FVector(-1970, 1250, 300));
	for (const int32 Layers : {1, 2})
	for (const FName Family : CastlePCGSupport::Families())
	{
		for (uint8 Mask = 1; Mask <= 14; ++Mask)
		{
			const FString Case = FString::Printf(TEXT("%s %d 层掩码 %d"), *Family.ToString(), Layers, Mask);
			const FCastlePCGBoardQuery Query = FQuadRoofFixture::Query(*Rules, Family, FIntVector(2, 2, Layers), Mask);
			FCastlePCGSemanticField Field = FQuadRoofFixture::Field(Query);
			FString Error;
			if (!Rule->Evaluate(Query, Field, Error)) { AddError(Case + TEXT(" ") + Error); return false; }
			const FIntVector First = FQuadRoofFixture::Root + Query.Up * (Layers - 1);
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const auto& Top = Field.Cells.FindChecked(First + FQuadRoofFixture::Quadrants[Index]);
				TestTrue(Case + TEXT(" 四格均替换整顶且仅最小顶格持有屋面"),
					Top.bOverrideWholeRoof && Top.Roof == (Index == 0 ? ECastlePCGRoof::Override : ECastlePCGRoof::Hidden));
			}
			const auto& Overlay = Field.Cells.FindChecked(First).RoofOverride;
			const auto* ExpectedKit = Rule->FamilyKits.FindChecked(Family).ByKind.FindChecked(Expected[Mask - 1]).Get();
			if (!TestTrue(Case + TEXT(" 挂本族正确款式"), Overlay.Kit == ExpectedKit)) { continue; }
			const double Pitch = Query.GetCellPitch(), Height = Query.GetLayerHeight();
			const FVector ExpectedOffset(Pitch * 0.5, Pitch * 0.5, Height);
			TestTrue(Case + TEXT(" 顶层中心挂载，可选中且不提交导航"),
				Overlay.AnchorTransform.GetLocation().Equals(ExpectedOffset, 0.01) && Overlay.bSelectable && !Overlay.bAffectsNavigation);
			const FCastleV4Part& Part = Overlay.Kit->Parts[0];
			const FMeshDescription* Mesh = Part.Mesh->GetMeshDescription(0);
			if (!TestNotNull(Case + TEXT(" 有持久化编辑器三角描述"), Mesh)) { continue; }
			const FVector FirstLocal(First.X * Pitch, First.Y * Pitch, First.Z * Height);
			const FTransform MeshWorld = Part.Local * Overlay.AnchorTransform * FTransform(FirstLocal) * BoardWorld;
			const FVector RoofCenter = BoardWorld.TransformPosition(FirstLocal + ExpectedOffset);
			const FBox Bounds = Part.Mesh->GetBoundingBox().TransformBy(MeshWorld.ToMatrixWithScale());
			// 2×2 建筑外沿半径 515 cm，预留作者滴水檐的外挑空间。
			const double Outer = Pitch + 45.0;
			TestTrue(Case + TEXT(" 世界包围盒不越出四格外沿及滴水檐范围 ") + Bounds.ToString(),
				Bounds.IsValid && Bounds.Min.X >= RoofCenter.X - Outer && Bounds.Max.X <= RoofCenter.X + Outer
				&& Bounds.Min.Y >= RoofCenter.Y - Outer && Bounds.Max.Y <= RoofCenter.Y + Outer);
			FVector2D Centers[4];
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Centers[Index] = FVector2D(BoardWorld.TransformPosition(FirstLocal + FVector(FQuadRoofFixture::Quadrants[Index]) * Pitch));
			}
			const auto Positions = FStaticMeshConstAttributes(*Mesh).GetVertexPositions();
			uint8 Covered = 0, HigherCovered = 0;
			for (const FTriangleID Triangle : Mesh->Triangles().GetElementIDs())
			{
				const auto Vertices = Mesh->GetTriangleVertices(Triangle);
				const FVector World[] = {MeshWorld.TransformPosition(FVector(Positions[Vertices[0]])),
					MeshWorld.TransformPosition(FVector(Positions[Vertices[1]])), MeshWorld.TransformPosition(FVector(Positions[Vertices[2]]))};
				const FVector2D XY[] = {FVector2D(World[0]), FVector2D(World[1]), FVector2D(World[2])};
				const bool bAboveSurface = FMath::Min3(World[0].Z, World[1].Z, World[2].Z) > RoofCenter.Z + 41.0;
				for (int32 Index = 0; Index < 4; ++Index)
				{
					const uint8 Bit = 1 << Index;
					if ((HigherCovered & Bit) == 0 && IntersectsCore(XY, Centers[Index], Pitch * 0.25))
					{
						Covered |= Bit;
						if (bAboveSurface) { HigherCovered |= Bit; }
					}
				}
			}
			TestEqual(Case + TEXT(" 世界三角投影完全避开占格核心"), int32(Covered & Mask), 0);
			TestEqual(Case + TEXT(" 每个空格核心都有露台面以上的屋面几何"), int32(HigherCovered), 15 ^ Mask);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGQuadRoofEligibility, "MovingCastle.Castle.PCG.Structure.QuadRoofEligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePCGQuadRoofEligibility::RunTest(const FString& Parameters)
{
	using namespace CastlePCGSupportTests;
	const auto* Rules = LoadRules(*this);
	if (!Rules) { return false; }
	const auto* Rule = FindRule<UCastlePCGQuadRoofOverlayRule>(*Rules);
	if (!TestNotNull(TEXT("正式规则集含四格屋面结构规则"), Rule)) { return false; }
	const auto CheckInactive = [&](const FCastlePCGBoardQuery& Query, const TCHAR* Label)
	{
		FCastlePCGSemanticField Field = FQuadRoofFixture::Field(Query);
		const FCastlePCGSemanticField Before = Field;
		FString Error;
		if (!Rule->Evaluate(Query, Field, Error)) { AddError(Error); return; }
		for (const FIntVector& Cell : Query.OrderedCells())
		{
			const auto& Spec = Field.Cells.FindChecked(Cell);
			TestTrue(Label, Spec.Roof == Before.Cells.FindChecked(Cell).Roof && !Spec.bOverrideWholeRoof);
		}
	};
	for (const int32 Layers : {1, 2})
	{
		const FIntVector Grid(2, 2, Layers);
		CheckInactive(FQuadRoofFixture::Query(*Rules, TEXT("Archer"), Grid, 0), TEXT("全空保留原大屋顶"));
		CheckInactive(FQuadRoofFixture::Query(*Rules, TEXT("Archer"), Grid, 15), TEXT("全满保留拓扑阶段按格隐藏"));
		CheckInactive(FQuadRoofFixture::Query(*Rules, TEXT("Archer"), Grid, 1, false, true), TEXT("隔层悬挑遮挡停用四格屋面"));
		CheckInactive(FQuadRoofFixture::Query(*Rules, TEXT("Archer"), Grid, 1, true), TEXT("普通房语义排除四格屋面"));
	}
	CheckInactive(FQuadRoofFixture::Query(*Rules, TEXT("Archer"), FIntVector(2, 1, 2), 1), TEXT("竖向三级无四格顶面"));
	CheckInactive(FQuadRoofFixture::Query(*Rules, TEXT("Archer"), FIntVector(1, 2, 2), 1), TEXT("旋转的竖向三级无四格顶面"));

	// 同一套触发边界适用于盾塔，全部有效掩码均应进入正式屋面，不产生占位日志。
	for (const int32 Layers : {1, 2})
	{
		const FIntVector Grid(2, 2, Layers);
		CheckInactive(FQuadRoofFixture::Query(*Rules, TEXT("Shield"), Grid, 0), TEXT("盾塔全空不触发四格叠层屋面"));
		CheckInactive(FQuadRoofFixture::Query(*Rules, TEXT("Shield"), Grid, 15), TEXT("盾塔全满不触发四格叠层屋面"));
		CheckInactive(FQuadRoofFixture::Query(*Rules, TEXT("Shield"), Grid, 1, false, true), TEXT("盾塔隔层悬挑遮挡不触发四格叠层屋面"));
		for (uint8 Mask = 1; Mask <= 14; ++Mask)
		{
			const auto Shield = FQuadRoofFixture::Query(*Rules, TEXT("Shield"), Grid, Mask);
			FCastlePCGSemanticField Field = FQuadRoofFixture::Field(Shield);
			FString Error;
			if (!Rule->Evaluate(Shield, Field, Error)) { AddError(Error); return false; }
			const auto& Owner = Field.Cells.FindChecked(FQuadRoofFixture::Root + Shield.Up * (Layers - 1));
			TestTrue(FString::Printf(TEXT("盾塔 %d 层占格 %d 使用本族套件"), Layers, Mask),
				Owner.Roof == ECastlePCGRoof::Override && Owner.RoofOverride.Kit
				&& Owner.RoofOverride.Kit->GetName().StartsWith(TEXT("DA_Shield_QuadRoof_")));
		}
	}
	return true;
}

#endif
