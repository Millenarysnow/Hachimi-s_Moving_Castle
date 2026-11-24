#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastlePCGSupportRules.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
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
		Field.Cells.Add(Covered + Up);
		FString Error;
		if (!Step->Evaluate(Query, Field, Error)) { AddError(Error); return false; }
		TestNull(TEXT("外露侧的紧邻上一层保持空置"), Query.Find(Exposed + Up));
		TestEqual(TEXT("隔层悬挑遮挡时停用半坡和上层地板覆写"),
			Field.Cells.FindChecked(Exposed).Roof == ECastlePCGRoof::Override, !bOverhead);
		TestEqual(TEXT("上层套件地板与半坡同启停"),
			Field.Cells.FindChecked(Covered + Up).DeckOverride.IsSet(), !bOverhead);
	}
	return true;
}

#endif
