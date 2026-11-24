#if WITH_DEV_AUTOMATION_TESTS

#include "CastlePCGSemanticTestRules.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleBuildingPCGSubsystem.h"
#include "CastleLadderLinkComponent.h"
#include "CastleNavigationSubsystem.h"
#include "BuildingMergeComponent.h"
#include "HachimisV4/CastleHachimisV4PCG.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Algo/AllOf.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace CastlePCGTests
{
	// 共用既有自动化世界、正式规则集和飞入完成入口。
	UWorld* World();
	UCastlePCGRuleSetDataAsset* Rules(UObject* Outer);
	void Complete(ACastleBuildingBase* Building);
}

namespace CastlePCGTests
{
	/** 为回归用例绑定正式资产。不会改动项目地图、默认资产或现有棋盘。 */
	UCastleBuildingDefinition* LoadDefinition(const TCHAR* Family)
	{
		return LoadObject<UCastleBuildingDefinition>(nullptr,
			*FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), Family, Family));
	}

	bool SetGreenSevenGround(ACastleBoard& Board, FText& Reason)
	{
		FCastleGroundGenerationResult Ground;
		for (int32 X = 0; X < Board.GridSize.X; ++X)
		for (int32 Y = 0; Y < Board.GridSize.Y; ++Y)
		{
			FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
			Cell.Coordinate = Board.GridOrigin + FIntVector(X, Y, 0);
			Cell.Color = ECastleTileColor::Green;
			Cell.Number = 7;
		}
		return Board.ApplyGroundGenerationResult(Ground, Reason);
	}

	ACastleBuildingBase* Place(FAutomationTestBase& Test, ACastleBoard& Board, const UCastleBuildingDefinition& Definition,
		const FIntVector& Cell, int32 Level, ECastleTileColor Color, bool bMerge = false)
	{
		FCastleBuildingDescriptor Card;
		FText Reason;
		if (!Definition.CreateDescriptor(Level, Color, 7, Card, Reason)) { Test.AddError(Reason.ToString()); return nullptr; }
		ACastleBuildingBase* Result = Board.PlaceBuilding(Cell, Card, bMerge, Reason);
		if (!Test.TestNotNull(FString::Printf(TEXT("通过棋盘放置 %s 于 %s"), *Definition.GetName(), *Cell.ToString()), Result))
		{
			Test.AddError(Reason.ToString());
			return nullptr;
		}
		Complete(Result);
		for (ACastleBuildingBase* Building : Board.GetPlacedBuildings()) { Complete(Building); }
		// 合成可能销毁刚完成飞入的一级 Actor，后续必须使用棋盘当前占格所有者。
		return Board.GetBuildingAt(Cell);
	}

	int32 CountVisibleParts(const ACastleBuildingBase& Building, FName Tag = NAME_None)
	{
		int32 Count = 0;
		for (const auto* Part : TInlineComponentArray<UStaticMeshComponent*>(&Building))
		{
			Count += Part->IsVisible() && Part->IsRegistered() && Part->GetStaticMesh()
				&& (Tag.IsNone() || Part->ComponentHasTag(Tag));
		}
		return Count;
	}

	bool CheckHatchCursor(FAutomationTestBase& Test, const ACastleBoard& Board, const ACastleBuildingBase& Upper, const FIntVector& Cell)
	{
		// 从带洞楼板四周的短射线寻找真正命中上层作者件的位置，排除上方家具和下层地板。
		const FTransform Frame = CastleV4PCG::CellFrame(Board, Cell);
		const double Stand = Upper.HachimisLayout->StandingHeight;
		for (const double X : {-170.0, -90.0, 0.0, 90.0, 170.0})
		for (const double Y : {-170.0, -90.0, 0.0, 90.0, 170.0})
		{
			const FVector Start = Frame.TransformPosition(FVector(X, Y, Stand + 25));
			const FVector End = Frame.TransformPosition(FVector(X, Y, Stand - 25));
			FHitResult Hit;
			const FCollisionQueryParams Params(SCENE_QUERY_STAT(SemanticPCGUpperFloorCursor), true);
			if (!Board.GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params)
				|| Hit.GetActor() != &Upper || !Hit.GetComponent()->ComponentHasTag(TEXT("CastlePCGGroup=UpperFloor"))) { continue; }
			FCastleGridCursorTarget Cursor;
			return Test.TestTrue(TEXT("正式建造准星从带洞楼板得到上层格"),
				Board.TryResolveGridCursorTarget(Start, End, Cursor) && Cursor.Cell == Cell && Cursor.Surface == ECastleGridCursorSurface::Building);
		}
		Test.AddError(TEXT("没有找到对上层 UpperFloor 组件的 Visibility 命中。"));
		return false;
	}

	bool HasOpenPassage(const ACastleBoard& Board, const FIntVector& First, const FIntVector& Second)
	{
		const FVector View(0, 0, 135);
		const FVector Start = CastleV4PCG::CellFrame(Board, First).TransformPosition(View);
		const FVector End = CastleV4PCG::CellFrame(Board, Second).TransformPosition(View);
		FHitResult Hit;
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(SemanticPCGDoorCollision), true);
		return !Board.GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);
	}

	/** 从上往下拆本次操作的测试建筑，让正常注销在 Recast 仍存在时完成。 */
	void RemoveFixtureBuildings(FAutomationTestBase& Test, ACastleBoard& Board)
	{
		TArray<ACastleBuildingBase*> Placed = Board.GetPlacedBuildings();
		Placed.Sort([](const ACastleBuildingBase& A, const ACastleBuildingBase& B)
		{
			return A.GetActorLocation().Z > B.GetActorLocation().Z;
		});
		FText Reason;
		for (ACastleBuildingBase* Building : Placed)
		{
			if (!Board.RemoveBuilding(Building, Reason)) { Test.AddError(Reason.ToString()); }
		}
	}
}

/** 七个正式楼板梯资产均使用持久化组；用相同上层独立房检查两侧 Actor 和准星。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGStairOwnership, "MovingCastle.Castle.PCG.StairOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastlePCGStairOwnership::RunTest(const FString& Parameters)
{
	using namespace CastlePCGTests;
	UWorld* Fixture = World();
	ON_SCOPE_EXIT { Fixture->DestroyWorld(false); GEngine->DestroyWorldContext(Fixture); };
	auto* Board = Fixture->SpawnActor<ACastleBoard>();
	ON_SCOPE_EXIT { RemoveFixtureBuildings(*this, *Board); };
	Board->GridSize = FIntVector(7, 2, 3);
	Board->PCGRuleSet = Rules(Board);
	FText Reason;
	if (!TestTrue(TEXT("布置七族验收地面"), SetGreenSevenGround(*Board, Reason))) { AddError(Reason.ToString()); return false; }
	UCastleBuildingDefinition* Room = LoadDefinition(TEXT("NormalRoom"));
	if (!TestNotNull(TEXT("正式普通房定义"), Room)) { return false; }
	int32 Column = 0;
	for (const TCHAR* Family : {TEXT("NormalRoom"), TEXT("ArcherTower"), TEXT("CannonTower"), TEXT("Farm"),
		TEXT("BalloonTower"), TEXT("BeeHive"), TEXT("ShieldTower")})
	{
		UCastleBuildingDefinition* Definition = LoadDefinition(Family);
		if (!TestNotNull(Family, Definition)) { return false; }
		Board->BuildingDefinitions = {Definition, Room};
		const FIntVector Cell(Column++, 0, 0);
		ACastleBuildingBase* Lower = Place(*this, *Board, *Definition, Cell, 1, ECastleTileColor::Green);
		ACastleBuildingBase* Upper = Place(*this, *Board, *Room, Cell + FCastlePCGBoardQuery::Up, 1, ECastleTileColor::Purple);
		if (!Lower || !Upper || !TestNotNull(TEXT("正式本族梯资产"), Lower->HachimisLayout->Ladder.Get())) { return false; }
		int32 Ladders = 0;
		int32 Floors = 0;
		const UCastleV4LadderKit& Kit = *Lower->HachimisLayout->Ladder;
		for (const FCastleV4Part& Part : Kit.Parts)
		{
			Ladders += Part.Group == TEXT("Ladder");
			Floors += Part.Group == TEXT("UpperFloor");
			TestTrue(Kit.GetName() + TEXT(" 包含有效的作者网格与分组"), Part.Mesh
				&& (Part.Group == TEXT("Ladder") || Part.Group == TEXT("UpperFloor")));
			if (Part.Node.ToString().StartsWith(TEXT("Ladder_Shoe")))
			{
				TestEqual(TEXT("梯脚始终属于 Ladder"), Part.Group, FName(TEXT("Ladder")));
			}
		}
		TestTrue(TEXT("每一族均有两个非空组"), Ladders > 0 && Floors > 0);
		TestEqual(TEXT("下层拥有套件全部竖梯零件"), CountVisibleParts(*Lower, TEXT("CastlePCGGroup=Ladder")), Ladders);
		TestEqual(TEXT("上层拥有套件全部带洞楼板零件"), CountVisibleParts(*Upper, TEXT("CastlePCGGroup=UpperFloor")), Floors);
		TestEqual(TEXT("下层无上层组"), CountVisibleParts(*Lower, TEXT("CastlePCGGroup=UpperFloor")), 0);
		TestEqual(TEXT("上层无梯子组"), CountVisibleParts(*Upper, TEXT("CastlePCGGroup=Ladder")), 0);
		TestNotNull(TEXT("竖梯 Link 归下层"), Lower->FindComponentByClass<UCastleLadderLinkComponent>());
		CheckHatchCursor(*this, *Board, *Upper, Cell + FCastlePCGBoardQuery::Up);
		AddInfo(FString::Printf(TEXT("%s：Ladder=%d UpperFloor=%d"), *Kit.GetName(), Ladders, Floors));
		if (!Board->RemoveBuilding(Upper, Reason) || !Board->RemoveBuilding(Lower, Reason)) { AddError(Reason.ToString()); return false; }
	}
	return true;
}

/** 同一轮先提交门洞和梯链，指定一栋动画失败后其余视觉仍可完成。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGChannelIsolation, "MovingCastle.Castle.PCG.ChannelIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastlePCGChannelIsolation::RunTest(const FString& Parameters)
{
	using namespace CastlePCGTests;
	UWorld* Fixture = World();
	ON_SCOPE_EXIT { Fixture->DestroyWorld(false); GEngine->DestroyWorldContext(Fixture); };
	auto* Board = Fixture->SpawnActor<ACastleBoard>();
	ON_SCOPE_EXIT { RemoveFixtureBuildings(*this, *Board); };
	Board->GridSize = FIntVector(4, 4, 4);
	Board->PCGRuleSet = DuplicateObject(Rules(Board), Board);
	auto* Planar = NewObject<UCastlePCGTestClosedPlanarRule>(Board->PCGRuleSet);
	Board->PCGRuleSet->Rules[0] = Planar;
	UCastleBuildingDefinition* Farm = LoadDefinition(TEXT("Farm"));
	UCastleBuildingDefinition* Room = LoadDefinition(TEXT("NormalRoom"));
	if (!TestNotNull(TEXT("正式农田"), Farm) || !TestNotNull(TEXT("正式普通房"), Room)) { return false; }
	Board->BuildingDefinitions = {Farm, Room};
	FText Reason;
	if (!SetGreenSevenGround(*Board, Reason)) { AddError(Reason.ToString()); return false; }
	const FIntVector Base(1, 1, 0), Side(2, 1, 0), Above(1, 1, 1);
	ACastleBuildingBase* Lower = Place(*this, *Board, *Farm, Base, 1, ECastleTileColor::Green);
	ACastleBuildingBase* Neighbor = Place(*this, *Board, *Room, Side, 1, ECastleTileColor::Purple);
	if (!Lower || !Neighbor) { return false; }
	TestFalse(TEXT("Planar 显式关闭门洞后，即使同号 V4 和 Room 不自发开门"), HasOpenPassage(*Board, Base, Side));
	const FString Before = Lower->BuildingPCG->GetAppliedKey();
	const FString NeighborVisual = Neighbor->BuildingPCG->GetAppliedVisualKey();
	auto* PCG = Fixture->GetSubsystem<UCastleBuildingPCGSubsystem>();
	FString Error;
	Board->PCGRuleSet->Rules.RemoveAt(0);
	AddExpectedError(TEXT("必须配齐 Planar"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("移除必需 Planar 后整轮拒绝"), PCG->Recompute(Board, FVector::ZeroVector, Error));
	TestEqual(TEXT("拒绝时保留上次组件签名"), Lower->BuildingPCG->GetAppliedKey(), Before);
	Board->PCGRuleSet->Rules.Insert(Planar, 0);
	Planar->bCloseDoors = false;

	const double Duration = Lower->BuildingMerge->Duration;
	Lower->BuildingMerge->Duration = 0;
	AddExpectedError(TEXT("零件合并失败：动画时长"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("的建筑 PCG 视觉过渡失败"), EAutomationExpectedErrorFlags::Contains, 1);
	ACastleBuildingBase* Upper = Place(*this, *Board, *Room, Above, 1, ECastleTileColor::Green);
	Lower->BuildingMerge->Duration = Duration;
	if (!Upper) { return false; }
	TestTrue(TEXT("动画启动失败后本轮已提交门洞碰撞"), HasOpenPassage(*Board, Base, Side));
	TestTrue(TEXT("同轮邻房也应用了开放门洞表现"), Neighbor->BuildingPCG->GetAppliedVisualKey() != NeighborVisual);
	TestTrue(TEXT("同轮上层楼板视觉已就位"), CountVisibleParts(*Upper, TEXT("CastlePCGGroup=UpperFloor")) > 0);
	const auto* Link = Lower->FindComponentByClass<UCastleLadderLinkComponent>();
	TestTrue(TEXT("同轮玩法事务的 NavLink 已登记且保留"), Link && Link->GetId().IsValid());
	const FString After = Lower->BuildingPCG->GetAppliedKey();
	TestTrue(TEXT("相同布局重算不重试失败的动画"), PCG->Recompute(Board, FVector::ZeroVector, Error));
	TestEqual(TEXT("动画失败后推进稳定键"), Lower->BuildingPCG->GetAppliedKey(), After);
	return true;
}

/** 斜撑、屋顶覆写以规则声明接入，同步验证重复写入、功能键隔离和撤销。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGStructureExtensions, "MovingCastle.Castle.PCG.StructureExtensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastlePCGStructureExtensions::RunTest(const FString& Parameters)
{
	using namespace CastlePCGTests;
	UWorld* Fixture = World();
	ON_SCOPE_EXIT { Fixture->DestroyWorld(false); GEngine->DestroyWorldContext(Fixture); };
	auto* Board = Fixture->SpawnActor<ACastleBoard>();
	ON_SCOPE_EXIT { RemoveFixtureBuildings(*this, *Board); };
	Board->GridSize = FIntVector(5, 4, 4);
	Board->PCGRuleSet = DuplicateObject(Rules(Board), Board);
	UCastleBuildingDefinition* Archer = LoadDefinition(TEXT("ArcherTower"));
	UCastleBuildingDefinition* Room = LoadDefinition(TEXT("NormalRoom"));
	if (!TestNotNull(TEXT("正式箭塔"), Archer) || !TestNotNull(TEXT("正式普通房"), Room)) { return false; }
	Board->BuildingDefinitions = {Archer, Room};
	FText Reason;
	const FIntVector Support(1, 1, 0), Above(1, 1, 1), Overhang(2, 1, 1);
	if (!SetGreenSevenGround(*Board, Reason)) { AddError(Reason.ToString()); return false; }
	if (!Place(*this, *Board, *Room, Support, 1, ECastleTileColor::Green)) { return false; }
	ACastleBuildingBase* Tower = Place(*this, *Board, *Archer, Above, 2, ECastleTileColor::Green);
	if (!Tower || !TestTrue(TEXT("正式二级尾格悬挑"), Tower->GetOccupiedCoordinates().Contains(Overhang)
		&& !Board->GetBuildingAt(Overhang - FCastlePCGBoardQuery::Up))) { return false; }
	const UCastleHachimisV4Layout& Layout = *Tower->HachimisLayout;
	const FCastleV4Part* RoofPart = Layout.Parts.FindByPredicate([](const FCastleV4Part& Part)
	{
		return Part.Group == TEXT("Roof") && Part.Cells.Contains(FIntVector(1, 0, 0));
	});
	if (!TestNotNull(TEXT("取真实塔尾格作者屋顶"), RoofPart)) { return false; }
	const auto& Original = Tower->BuildingPCG->GetOriginalParts();
	const UPrimitiveComponent* OriginalRoof = Original[RoofPart - Layout.Parts.GetData()].Component;
	const FString Gameplay = Tower->BuildingPCG->GetAppliedGameplayKey();
	const FString Visual = Tower->BuildingPCG->GetAppliedVisualKey();

	auto* Brace = NewObject<UCastlePCGTestBraceRule>(Board->PCGRuleSet);
	auto* Roof = NewObject<UCastlePCGTestRoofRule>(Board->PCGRuleSet);
	Brace->Owner = Roof->Owner = Overhang;
	Brace->Kit = NewObject<UCastlePCGAttachmentKit>(Brace);
	Roof->Kit = NewObject<UCastlePCGAttachmentKit>(Roof);
	for (UCastlePCGAttachmentKit* Kit : {Brace->Kit.Get(), Roof->Kit.Get()})
	{
		FCastleV4Part Part = *RoofPart;
		Part.Local = FTransform::Identity;
		Part.Node = Kit == Brace->Kit ? TEXT("RegressionBrace") : TEXT("RegressionRoof");
		Kit->Parts.Add(Part);
	}
	Board->PCGRuleSet->Rules.Append({Brace, Roof});
	auto* PCG = Fixture->GetSubsystem<UCastleBuildingPCGSubsystem>();
	auto* Nav = Fixture->GetSubsystem<UCastleNavigationSubsystem>();
	const auto Before = Nav->GetSharedPath();
	FString Error;
	if (!PCG->Recompute(Board, FVector::ZeroVector, Error)) { AddError(Error); return false; }
	Complete(Tower);
	TestEqual(TEXT("纯表现规则保持 GameplayKey"), Tower->BuildingPCG->GetAppliedGameplayKey(), Gameplay);
	TestTrue(TEXT("悬挑和覆写都改变 VisualKey"), Tower->BuildingPCG->GetAppliedVisualKey() != Visual);
	TestEqual(TEXT("纯表现变化未请求导航失效"), Nav->GetSharedPath().PathRevision, Before.PathRevision);
	TestEqual(TEXT("纯表现变化未推进 Recast 构建版本"), Nav->GetSharedPath().NavigationRevision, Before.NavigationRevision);
	TestFalse(TEXT("覆写隐藏对应作者屋顶"), OriginalRoof->IsVisible());
	TestEqual(TEXT("斜撑挂件由悬挑格 Actor 拥有"), CountVisibleParts(*Tower, TEXT("CastlePCGNode=RegressionBrace")), 1);
	TestEqual(TEXT("替换屋顶由悬挑格 Actor 拥有"), CountVisibleParts(*Tower, TEXT("CastlePCGNode=RegressionRoof")), 1);

	Brace->bDuplicate = true;
	const FString Current = Tower->BuildingPCG->GetAppliedKey();
	AddExpectedError(TEXT("重复或无效"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("重复挂载键拒绝整轮"), PCG->Recompute(Board, FVector::ZeroVector, Error));
	TestEqual(TEXT("拒绝保留上一稳定目标"), Tower->BuildingPCG->GetAppliedKey(), Current);
	Brace->bDuplicate = false;
	// 新的直接支撑只改变悬挑条件，之前只读六邻会遗漏某些更远的斜撑规则依赖。
	if (!Place(*this, *Board, *Room, Overhang - FCastlePCGBoardQuery::Up, 1, ECastleTileColor::Purple)) { return false; }
	TestEqual(TEXT("补齐支撑后移除斜撑"), CountVisibleParts(*Tower, TEXT("CastlePCGNode=RegressionBrace")), 0);
	TestEqual(TEXT("补齐支撑后移除覆写"), CountVisibleParts(*Tower, TEXT("CastlePCGNode=RegressionRoof")), 0);
	TestTrue(TEXT("作者屋顶精确恢复"), OriginalRoof->IsVisible());
	return true;
}

namespace CastlePCGTests
{
	/** 截图拓扑通过正常棋盘入口复现；垂直二级农田由上下两张一级卡实际合成。 */
	struct FScreenshotStack
	{
		ACastleBuildingBase* Farm = nullptr;
		ACastleBuildingBase* Top = nullptr;
		ACastleBuildingBase* Archer = nullptr;
		ACastleBuildingBase* Corner = nullptr;
		TArray<UPrimitiveComponent*> FarmRoof;
		FIntVector Base;
		FIntVector TopCell;
		FIntVector ArcherStart;
		FIntVector CornerCell;

		bool Build(FAutomationTestBase& Test, ACastleBoard& Board)
		{
			UCastleBuildingDefinition* FarmDefinition = LoadDefinition(TEXT("Farm"));
			UCastleBuildingDefinition* ArcherDefinition = LoadDefinition(TEXT("ArcherTower"));
			UCastleBuildingDefinition* Room = LoadDefinition(TEXT("NormalRoom"));
			if (!Test.TestNotNull(TEXT("农田定义"), FarmDefinition) || !Test.TestNotNull(TEXT("箭塔定义"), ArcherDefinition)
				|| !Test.TestNotNull(TEXT("普通房定义"), Room)) { return false; }
			FText Reason;
			Board.BuildingDefinitions = {FarmDefinition, ArcherDefinition, Room};
			if (!SetGreenSevenGround(Board, Reason)) { Test.AddError(Reason.ToString()); return false; }

			Base = Board.GridOrigin + FIntVector(1, 1, 0);
			TopCell = Base + FIntVector(0, 0, 2);
			CornerCell = Base + FIntVector(1, 0, 0);
			ArcherStart = CornerCell + FIntVector(0, 1, 0);
			if (!Place(Test, Board, *FarmDefinition, Base, 1, ECastleTileColor::Green)
				|| !Place(Test, Board, *FarmDefinition, Base + FCastlePCGBoardQuery::Up, 1, ECastleTileColor::Green, true)) { return false; }
			Farm = Board.GetBuildingAt(Base);
			if (!Test.TestTrue(TEXT("绿色 7 号实际合成二级垂直农田"), Farm && Farm->GetBuildingLevel() == 2
				&& Farm == Board.GetBuildingAt(Base + FCastlePCGBoardQuery::Up) && Farm->GetOccupiedCoordinates().Num() == 2))
			{
				return false;
			}
			for (const auto& Part : Farm->BuildingPCG->GetOriginalParts())
			{
				if (Part.Role == ECastlePCGPartRole::Roof && Part.Component->IsVisible()) { FarmRoof.Add(Part.Component); }
			}
			Top = Place(Test, Board, *Room, TopCell, 1, ECastleTileColor::Green);
			Archer = Place(Test, Board, *ArcherDefinition, ArcherStart, 2, ECastleTileColor::Purple);
			Corner = Place(Test, Board, *Room, CornerCell, 1, ECastleTileColor::Purple);
			if (!Top || !Archer || !Corner) { return false; }
			Test.TestTrue(TEXT("紫色 7 号箭塔沿 X 横跨两格"), Archer->GetOccupiedCoordinates().Contains(ArcherStart + FIntVector(1, 0, 0)));
			Test.TestTrue(TEXT("孤立上层房保留可见且可选中的带洞楼板"), CountVisibleParts(*Top, TEXT("CastlePCGGroup=UpperFloor")) > 0);
			Test.TestTrue(TEXT("上层房可独立捕获合并所需可见零件"), CountVisibleParts(*Top) > 0);
			Test.TestTrue(TEXT("拐角普通房连通农田和箭塔两侧门洞"), HasOpenPassage(Board, Base, CornerCell)
				&& HasOpenPassage(Board, CornerCell, ArcherStart));
			TArray<FCastleActiveLadderConnection> Links;
			Board.GetWorld()->GetSubsystem<UCastleBuildingPCGSubsystem>()->GetActiveLadderConnections(&Board, Links);
			Test.TestTrue(TEXT("农田上层格持有上行到独立房的梯子"), Links.ContainsByPredicate([&](const auto& Link)
			{
				return Link.Building == Farm && Link.LowerCell == Base + FCastlePCGBoardQuery::Up && Link.UpperCell == TopCell;
			}));
			return CheckHatchCursor(Test, Board, *Top, TopCell);
		}

		bool RemoveTop(FAutomationTestBase& Test, ACastleBoard& Board)
		{
			FText Reason;
			if (!Board.RemoveBuilding(Top, Reason)) { Test.AddError(Reason.ToString()); return false; }
			Complete(Farm);
			Test.TestTrue(TEXT("撤除上层房后农田原屋顶复原"), !FarmRoof.IsEmpty()
				&& Algo::AllOf(FarmRoof, [](const UPrimitiveComponent* Piece) { return Piece->IsVisible(); }));
			TArray<FCastleActiveLadderConnection> Links;
			Board.GetWorld()->GetSubsystem<UCastleBuildingPCGSubsystem>()->GetActiveLadderConnections(&Board, Links);
			Test.TestEqual(TEXT("撤除独立房后仅保留农田本身的内梯"), Links.Num(), 1);
			Test.TestTrue(TEXT("撤除顶层房保留拐角两面通行"), HasOpenPassage(Board, Base, CornerCell)
				&& HasOpenPassage(Board, CornerCell, ArcherStart));
			Top = nullptr;
			return true;
		}
	};

	/** 使用真实地图的动态 Recast 和巡逻入口，提交截图中的三栋相邻建筑闭环。 */
	class FScreenshotPatrolCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FScreenshotPatrolCommand(FAutomationTestBase& InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() >= Deadline)
			{
				Test.AddError(TEXT("截图拓扑的真实地图和动态 Recast 未在 120 秒内就绪。"));
				Cleanup();
				return true;
			}
			if (!Board)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType != EWorldType::Game || !Context.World()) { continue; }
					for (TActorIterator<ACastleBoard> It(Context.World()); It; ++It) { Board = *It; break; }
					if (Board) { break; }
				}
				if (!Board) { return false; }
				if (!Test.TestEqual(TEXT("截图巡逻用空白验收地图"), Board->GetPlacedBuildingCount(), 0)) { return true; }
				Nav = Board->GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
				if (!Test.TestNotNull(TEXT("动态导航子系统"), Nav)) { return true; }
			}
			if (!Nav->IsNavigationReady()) { return false; }
			if (!bBuilt)
			{
				// 恢复测试地图数据，防止同一命令行的其他验收用例受本测试影响。
				for (int32 X = 0; X < Board->GridSize.X; ++X)
				for (int32 Y = 0; Y < Board->GridSize.Y; ++Y)
				{
					FCastleGroundCell Cell;
					if (!Board->TryGetGroundCell(Board->GridOrigin + FIntVector(X, Y, 0), Cell))
					{
						Test.AddError(TEXT("TestMap1 的验收地面必须完整。"));
						return true;
					}
					Ground.Cells.Add(Cell);
				}
				Definitions = Board->BuildingDefinitions;
				bOwnsFixture = true;
				if (!Stack.Build(Test, *Board)) { Cleanup(); return true; }
				bBuilt = true;
				return false;
			}
			TActorIterator<ACastlePatrolRoute> Route(Board->GetWorld());
			if (!Test.TestTrue(TEXT("TestMap1 注册正式巡逻入口"), static_cast<bool>(Route))) { Cleanup(); return true; }
			TArray<FVector> Points;
			auto AddPoint = [&](ACastleBuildingBase& Building, const FIntVector& Cell)
			{
				const FTransform Frame = CastleV4PCG::CellFrame(*Board, Cell);
				const double Height = Building.HachimisLayout->StandingHeight;
				FVector WorldPoint;
				FText Reason;
				// 通过和按 P 相同的 Pawn 射线入口选取各自楼板，保持所测路径点的真实层归属。
				for (const double Offset : {0.0, -75.0, 75.0})
				{
					const FVector Eye = Frame.TransformPosition(FVector(Offset, 0, Height + 55));
					const FVector Below = Frame.TransformPosition(FVector(Offset, 0, Height - 10));
					if (ACastlePatrolRoute::TryResolveWaypointFromRay(Board->GetWorld(), Eye, Below, WorldPoint, Reason))
					{
						Points.Add(WorldPoint);
						return true;
					}
				}
				Test.AddError(Reason.ToString());
				return false;
			};
			FText Reason;
			if (AddPoint(*Stack.Farm, Stack.Base) && AddPoint(*Stack.Corner, Stack.CornerCell)
				&& AddPoint(*Stack.Archer, Stack.ArcherStart))
			{
				const bool bSubmitted = Route->SetWorldWaypoints(Points, Reason);
				const FCastleSharedNavigationPath Path = Nav->GetSharedPath();
				if (!Test.TestTrue(TEXT("农田、拐角房和箭塔形成可走通的 Recast 闭环"),
					bSubmitted && Path.bValid && Path.bClosedLoop && Path.Waypoints.Num() == Points.Num()))
				{
					Test.AddError(Reason.IsEmpty() ? Nav->LastFailure.ToString() : Reason.ToString());
				}
			}
			Stack.RemoveTop(Test, *Board);
			Cleanup();
			return true;
		}
	private:
		void Cleanup()
		{
			if (!bOwnsFixture) { return; }
			bOwnsFixture = false;
			Nav->ClearSharedRoute();
			FText Reason;
			// 此处只有本测试拥有的空棋盘才会进入清理。
			RemoveFixtureBuildings(Test, *Board);
			Board->BuildingDefinitions = Definitions;
			if (!Board->ApplyGroundGenerationResult(Ground, Reason)) { Test.AddError(Reason.ToString()); }
		}

		FAutomationTestBase& Test;
		double Deadline = FPlatformTime::Seconds() + 120.0;
		ACastleBoard* Board = nullptr;
		UCastleNavigationSubsystem* Nav = nullptr;
		FCastleGroundGenerationResult Ground;
		TArray<TObjectPtr<UCastleBuildingDefinition>> Definitions;
		FScreenshotStack Stack;
		bool bOwnsFixture = false;
		bool bBuilt = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGScreenshotStack, "MovingCastle.Castle.PCG.ScreenshotStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastlePCGScreenshotStack::RunTest(const FString& Parameters)
{
	UWorld* World = CastlePCGTests::World();
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Board = World->SpawnActor<ACastleBoard>();
	ON_SCOPE_EXIT { CastlePCGTests::RemoveFixtureBuildings(*this, *Board); };
	Board->GridSize = FIntVector(6, 6, 5);
	Board->PCGRuleSet = CastlePCGTests::Rules(Board);
	CastlePCGTests::FScreenshotStack Stack;
	return Stack.Build(*this, *Board) && Stack.RemoveTop(*this, *Board);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGScreenshotPatrol, "MovingCastle.Castle.PCG.ScreenshotPatrol",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCastlePCGScreenshotPatrol::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastlePCGTests::FScreenshotPatrolCommand(*this));
	return true;
}

#endif
