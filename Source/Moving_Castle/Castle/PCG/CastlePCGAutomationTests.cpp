#if WITH_DEV_AUTOMATION_TESTS
#include "CastleBuildingPCGSubsystem.h"
#include "HachimisV4/CastleHachimisV4PCG.h"
#include "CastleBackpackComponent.h"
#include "CastleLadderLinkComponent.h"
#include "Components/BoxComponent.h"
#include "CastlePCGSemantics.h"
#include "CastleDeterministicPCGNoise.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "BuildingMergeComponent.h"
#include "AssemblyFlightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Algo/Reverse.h"
#include "Algo/AllOf.h"
#include "NavigationSystem.h"

namespace CastlePCGTests
{
	/** 测试显式提供正式资产与规则，生产路径只读取 DA 配置。 */
	UCastlePCGRuleSetDataAsset* Rules(UObject* Outer)
	{
		return LoadObject<UCastlePCGRuleSetDataAsset>(nullptr,
			TEXT("/Game/MovingCastle/Data/DA_CastlePCGRuleSet.DA_CastlePCGRuleSet"));
	}
	/** 游戏世界保留正式放置和快照组件语义，但夹具提供动态 Recast 数据，路径在真实地图验证。 */
	UWorld* World()
	{
		const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
			.CreateNavigation(true).CreateAISystem(false).ShouldSimulatePhysics(false);
		auto* W = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
        // 夹具显式建立 Recast 数据，允许真实梯子注册/注销请求刷新，实际路径留给地图测试。
        auto* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
        check(Navigation && Navigation->GetDefaultNavDataInstance(FNavigationSystem::Create));
		return W;
	}
	void Complete(ACastleBuildingBase* B)
	{
		if (B->AssemblyFlight->bIsPlaying) { B->AssemblyFlight->TickComponent(100.0f, LEVELTICK_All, nullptr); }
		if (IsValid(B) && B->BuildingMerge->bIsPlaying) { B->BuildingMerge->TickComponent(100.0f, LEVELTICK_All, nullptr); }
	}
}

/** 正式蓝图全量提取：各角色数量和覆盖格关系必须适用于水平、垂直与90度旋转。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGSemanticsTest, "MovingCastle.Castle.PCG.FormalBlueprintSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastlePCGSemanticsTest::RunTest(const FString& Parameters)
{
	UWorld* World = CastlePCGTests::World();
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Board = World->SpawnActor<ACastleBoard>();
	for (const TCHAR* Family : {TEXT("NormalRoom"),TEXT("ArcherTower"),TEXT("CannonTower"),TEXT("Farm"),TEXT("ShieldTower"),TEXT("BalloonTower"),TEXT("BeeHive")})
	{
		const FString Path = FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), Family, Family);
		auto* Definition = LoadObject<UCastleBuildingDefinition>(nullptr, *Path);
		if (!TestNotNull(Path, Definition)) { return false; }
		for (const auto& Level : Definition->Levels)
		{
			if (Level.Level > 4 && Definition->BuildingType == ECastleBuildingType::ShieldTower) { continue; }
			for (const bool bVertical : {false,true})
			{
				if (bVertical && !Level.VerticalBuildingClass) { continue; }
				for (const bool bRotated : {false,true})
				{
					const FIntVector Shape = Level.Level == 1 || Level.Level == 5 ? FIntVector(1,1,1) : (Level.Level == 2
						? (bVertical ? FIntVector(1,1,2) : FIntVector(2,1,1))
						: (Level.Level == 3 ? (bVertical ? FIntVector(2,1,2) : FIntVector(2,2,1)) : FIntVector(2,2,2)));
					TArray<FIntVector> Cells;
					for(int32 X=0;X<Shape.X;++X) for(int32 Y=0;Y<Shape.Y;++Y) for(int32 Z=0;Z<Shape.Z;++Z)
					{ Cells.Add(bRotated ? FIntVector(Y,X,Z) : FIntVector(X,Y,Z)); }
					FCastleBuildingDescriptor Card;
					FText Reason;
					Definition->CreateDescriptor(Level.Level, ECastleTileColor::Red, 3, Card, Reason);
					auto* Building = World->SpawnActor<ACastleBuildingBase>(bVertical ? Level.VerticalBuildingClass : Level.BuildingClass);
					if (!TestNotNull(TEXT("正式蓝图实例"), Building)) { return false; }
					if (!TestTrue(TEXT("初始化正式占格"), Building->InitializeBuilding(Card, Cells))) { return false; }
					Building->SetActorRotation(FRotator(0,bRotated ? 90 : 0,0));
					Building->SetActorLocation(FVector(bRotated ? (Shape.Y-1)*Board->CellSize*0.5 : (Shape.X-1)*Board->CellSize*0.5,
						bRotated ? (Shape.X-1)*Board->CellSize*0.5 : (Shape.Y-1)*Board->CellSize*0.5, Board->WorldBaseHeight - 280.0));
					TArray<FCastlePCGPartAnnotation> Parts;
					FString Error;
					if (!TestTrue(TEXT("提取正式语义"), CastlePCGSemantics::Extract(Building,Board,Parts,Error))) { AddError(Error); return false; }
					int32 Roof=0,Slab=0,Rail=0,Double=0;
					for (const auto& Part : Parts)
					{
						Roof += Part.Role == ECastlePCGPartRole::Roof;
						Slab += Part.Role == ECastlePCGPartRole::TopCeilingSlab;
						Rail += Part.Role == ECastlePCGPartRole::PerimeterRailing;
						Double += Part.Role == ECastlePCGPartRole::PerimeterRailing && Part.CoveredCellOffsets.Num()==2;
						for (const auto& Cell : Part.CoveredCellOffsets) { TestTrue(TEXT("语义覆盖格必须属于建筑"),Cells.Contains(Cell)); }
					}
					if (const UCastleHachimisV4Layout* Layout = Building->HachimisLayout)
					{
						TestEqual(TEXT("工艺分件逐一对应作者节点"), Parts.Num(), Layout->Parts.Num());
						TestEqual(TEXT("保持作者占格"), Layout->Grid, Shape);
						TestEqual(TEXT("所有墙位拆件都映射到单一外侧格面"), Double, 0);
						TSet<FIntVector> RoofCells;
						for (const auto& Part : Parts)
						{
							if (Part.Role == ECastlePCGPartRole::Roof) { RoofCells.Append(Part.CoveredCellOffsets); }
						}
						TestEqual(TEXT("屋面覆盖每个实际顶层格"), RoofCells.Num(), Layout->bChassisOnly ? 0 : Shape.X * Shape.Y);
						TestEqual(TEXT("仅双层作者造型含内楼板"), Slab > 0, Shape.Z == 2);
						TestEqual(TEXT("墙节点数量供给"), Rail > 0, !Layout->bChassisOnly);
					}
					else
					{
						const bool bFlat = FString(Family)==TEXT("NormalRoom") || FString(Family)==TEXT("ShieldTower");
						const int32 TopCount = Shape.X*Shape.Y;
						TestEqual(TEXT("屋顶数量对应每个顶层格"),Roof,FString(Family)==TEXT("NormalRoom") ? 0 : TopCount);
						TestEqual(TEXT("平板数量含内部层和封闭顶层"),Slab,Shape.X*Shape.Y*(Shape.Z-1)+(bFlat ? TopCount:0));
						TestEqual(TEXT("跨格护栏只出现在水平二级和四级"),Double,(!bVertical && (Level.Level==2 || Level.Level==4)) ? 1:0);
						const int32 ExpectedRails = Level.Level==1 ? 0 : (bVertical ? (Level.Level==2?4:8) : (Level.Level==2?3:(Level.Level==3?4:11)));
						TestEqual(TEXT("正式模型护栏数精确对应等级结构"),Rail,ExpectedRails);
					}
					if (Level.Level==1 && !bRotated)
					{
						auto* Tagged=CastChecked<UStaticMeshComponent>(Parts[0].Component);
						Tagged->ComponentTags.Add(TEXT("PCGRole=InvalidRole"));
						TArray<FCastlePCGPartAnnotation> InvalidParts;
						TestFalse(TEXT("非法显式标签拒绝整轮提取"),CastlePCGSemantics::Extract(Building,Board,InvalidParts,Error));
						TestTrue(TEXT("标签错误包含建筑与组件名"),Error.Contains(Building->GetPathName())&&Error.Contains(Tagged->GetName()));
						TestEqual(TEXT("失败不提交部分标注"),InvalidParts.Num(),0);
						Tagged->ComponentTags.Remove(TEXT("PCGRole=InvalidRole"));
					}
					if (!bRotated) { AddInfo(FString::Printf(TEXT("PCG统计 %s Roof=%d Slab=%d Railing=%d Double=%d"),*Building->GetClass()->GetName(),Roof,Slab,Rail,Double)); }
					Building->Destroy();
				}
			}
		}
	}
	return true;
}

/** 多格垂直规则不随输入顺序改变，平面规则双侧独立求值，装饰噪声扩展可直接复用接口。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGRuleTest, "MovingCastle.Castle.PCG.RuleEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastlePCGRuleTest::RunTest(const FString& Parameters)
{
	FCastlePCGRuleContext Context;
	Context.NoiseProvider=NewObject<UCastleDeterministicPCGNoise>();
	Context.LadderChannel=TEXT("LadderPlacement");
	// 纯逻辑规则仅以建筑身份分组，此处两个非空身份来自独立 CDO 类，不读取 Actor 状态。
	Context.Building=nullptr;
	for (const auto& Cell : TArray<FIntVector>{{1,1,0},{0,1,0},{1,0,0},{0,0,0}})
	{
		FCastlePCGCellAdjacency A;
		A.FirstCell=Cell; A.Offset=FIntVector(0,0,1); A.SecondCell=Cell+A.Offset; A.bCanTraverse=true;
		Context.Adjacencies.Add(A);
	}
	auto* Rule=NewObject<UCastlePCGVerticalRule>();
	FCastlePCGTarget Target;
	FString Error;
	TestTrue(TEXT("四格可通行求值成功"),Rule->Evaluate(Context,Target,Error));
	TestEqual(TEXT("四格均生成顶面"),Target.Ceilings.Num(),4);
	TestEqual(TEXT("只有坐标首格为梯子"),Target.Ceilings[FIntVector::ZeroValue],uint8(2));
	int32 Ladders=0;for(const auto& Pair:Target.Ceilings){Ladders+=Pair.Value==2;}
	TestEqual(TEXT("一对建筑唯一梯子"),Ladders,1);
	Context.bUseLadderNoise=true;
	FCastlePCGTarget NoiseA,NoiseB;
	Rule->Evaluate(Context,NoiseA,Error);
	Algo::Reverse(Context.Adjacencies);
	Rule->Evaluate(Context,NoiseB,Error);
	TestEqual(TEXT("噪声择格与求值顺序无关"),NoiseA.StableKey(),NoiseB.StableKey());
	for(auto& A:Context.Adjacencies){A.bCanTraverse=false;}
	FCastlePCGTarget Closed;Rule->Evaluate(Context,Closed,Error);
	for(const auto& Pair:Closed.Ceilings){TestEqual(TEXT("不可通行全为封闭平板"),Pair.Value,uint8(1));}
	return true;
}
/** 正式棋盘放置/移除验证唯一梯子、双格护栏局部替换和动画中连续撤回的精确复原。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGRuntimeTest, "MovingCastle.Castle.PCG.ReversiblePlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastlePCGRuntimeTest::RunTest(const FString& Parameters)
{
	UWorld* World=CastlePCGTests::World();
	ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
	auto* Board=World->SpawnActor<ACastleBoard>();
	Board->GridSize=FIntVector(6,6,4);
	Board->PCGRuleSet=CastlePCGTests::Rules(Board);
	auto* Definition=LoadObject<UCastleBuildingDefinition>(nullptr,TEXT("/Game/MovingCastle/Data/Buildings/DA_ArcherTower.DA_ArcherTower"));
	auto* Normal=LoadObject<UCastleBuildingDefinition>(nullptr,TEXT("/Game/MovingCastle/Data/Buildings/DA_NormalRoom.DA_NormalRoom"));
	Board->BuildingDefinitions={Definition,Normal};
	FCastleGroundGenerationResult Ground;
	for(int32 X=0;X<6;++X)for(int32 Y=0;Y<6;++Y){FCastleGroundCell C;C.Coordinate=FIntVector(X,Y,0);C.Color=ECastleTileColor::Red;C.Number=3;Ground.Cells.Add(C);}
	FText Reason;
	if(!TestTrue(TEXT("正式地面配置"),Board->ApplyGroundGenerationResult(Ground,Reason))){AddError(Reason.ToString());return false;}
	FCastleBuildingDescriptor Card;
	Definition->CreateDescriptor(3,ECastleTileColor::Red,3,Card,Reason);
	auto* Lower=Board->PlaceBuilding(FIntVector::ZeroValue,Card,false,Reason);
	if(!TestNotNull(TEXT("放置四格下层"),Lower)){AddError(Reason.ToString());return false;}
	CastlePCGTests::Complete(Lower);
	CastlePCGTests::Complete(Lower);
	TArray<UStaticMeshComponent*> Initial;
	Lower->GetComponents(Initial);
	TMap<UStaticMeshComponent*,bool> Visibility;
	for(auto* Mesh:Initial){Visibility.Add(Mesh,Mesh->IsVisible());}
	Card.Number=4;
	auto* Upper=Board->PlaceBuilding(FIntVector(0,0,1),Card,false,Reason);
	if(!TestNotNull(TEXT("同色四格上层"),Upper)){AddError(Reason.ToString());return false;}
	TArray<FCastleActiveLadderConnection> Ladders;
	auto* PCG=World->GetSubsystem<UCastleBuildingPCGSubsystem>();
	PCG->GetActiveLadderConnections(Board,Ladders);
	TestEqual(TEXT("上层飞入中不改变下层"),Ladders.Num(),0);
	CastlePCGTests::Complete(Upper);
	PCG->GetActiveLadderConnections(Board,Ladders);
	TestEqual(TEXT("四重合格只一把梯子"),Ladders.Num(),1);
	if(Ladders.Num()!=1){return false;}
	TestEqual(TEXT("确定性默认选择首格"),Ladders[0].LowerCell,FIntVector::ZeroValue);
	TestTrue(TEXT("梯子顶面精确高560厘米"),FMath::IsNearlyEqual(Ladders[0].TopWorldLocation.Z-Ladders[0].BottomWorldLocation.Z,560.0,0.01));
	TestTrue(TEXT("PCG变化走原生StartChange"),Lower->BuildingMerge->bIsPlaying);
	const FString Applied=Lower->BuildingPCG->GetAppliedKey();
	FString Error;
	TestTrue(TEXT("同一布局幂等重算"),PCG->Recompute(Board,FVector::ZeroVector,Error));
	TestEqual(TEXT("幂等重算不改目标"),Lower->BuildingPCG->GetAppliedKey(),Applied);
	// 模拟试装末尾的动画参数错误。接口已存在时，失败仍须保留旧组件和已登记的 Link。
	AActor* Snapshot = Lower->BuildingMerge->CreateVisualSnapshot(Error);
	if (!TestNotNull(TEXT("生成失败事务的旧件快照"), Snapshot)) { AddError(Error); return false; }
	const UCastleLadderLinkComponent* PriorLink = Lower->FindComponentByClass<UCastleLadderLinkComponent>();
	const double Duration = Lower->BuildingMerge->Duration;
	Lower->BuildingMerge->Duration = 0.0;
	AddExpectedError(TEXT("零件合并失败：动画时长"), EAutomationExpectedErrorFlags::Contains, 1);
	FCastlePCGApplyPlan InvalidMotion;
	InvalidMotion.Key = TEXT("InvalidMotion");
	const bool bApplied = Lower->BuildingPCG->Apply(Board, InvalidMotion, Snapshot, FVector::ZeroVector, Error);
	Lower->BuildingPCG->FinishApply(false);
	Lower->BuildingMerge->Duration = Duration;
	TestFalse(TEXT("非法动画的目标试装返回失败"), bApplied);
	TestEqual(TEXT("目标失败恢复原键"), Lower->BuildingPCG->GetAppliedKey(), Applied);
	TestTrue(TEXT("目标失败保留登记过的原梯组件"), PriorLink && PriorLink == Lower->FindComponentByClass<UCastleLadderLinkComponent>()
		&& PriorLink->GetId().IsValid());
	// 用恢复后的原状态撤回上层，检查最终显隐和导航组件全部复原。
	TestTrue(TEXT("动画中删除上层"),Board->RemoveBuilding(Upper,Reason));
	CastlePCGTests::Complete(Lower);
	PCG->GetActiveLadderConnections(Board,Ladders);
	TestEqual(TEXT("撤回后无残留梯子"),Ladders.Num(),0);
	for(const auto& Pair:Visibility){TestEqual(TEXT("原组件显隐100%恢复"),Pair.Key->IsVisible(),Pair.Value);}
	TArray<UStaticMeshComponent*> After;Lower->GetComponents(After);
	TestEqual(TEXT("撤回不残留动态或快照网格"),After.Num(),Initial.Num());
	Board->RemoveBuilding(Lower,Reason);

	Definition->CreateDescriptor(2,ECastleTileColor::Red,3,Card,Reason);
	Lower=Board->PlaceBuilding(FIntVector(2,2,0),Card,false,Reason);
	if(!TestNotNull(TEXT("双格护栏建筑"),Lower)){return false;}
	CastlePCGTests::Complete(Lower);
	TestTrue(TEXT("显式提取原件缓存"),Lower->BuildingPCG->CacheOriginalParts(Board,Error));
	const auto& Parts = Lower->BuildingPCG->GetOriginalParts();
	const auto* Side = Parts.FindByPredicate([](const FCastlePCGPartAnnotation& Part)
	{
		return Part.Role == ECastlePCGPartRole::PerimeterRailing && Part.BoardFacing == ECastlePCGFace::PositiveY;
	});
	if (!TestNotNull(TEXT("找到一侧的独立作者墙位"), Side)) { return false; }
	const FIntVector WorldCell = FIntVector(2,2,0) + Side->LocalCellOffset;
	const auto* OtherSide = Parts.FindByPredicate([&](const FCastlePCGPartAnnotation& Part)
	{
		return Part.Role == Side->Role && Part.BoardFacing == Side->BoardFacing && Part.LocalCellOffset != Side->LocalCellOffset;
	});
	if (!TestNotNull(TEXT("同面另有独立墙位"), OtherSide)) { return false; }
	Normal->CreateDescriptor(1, ECastleTileColor::Red, 4, Card, Reason);
	Upper = Board->PlaceBuilding(WorldCell + FIntVector(0,1,0), Card, false, Reason);
	if (!TestNotNull(TEXT("只连同面的一半"), Upper)) { AddError(Reason.ToString()); return false; }
	CastlePCGTests::Complete(Upper);
	CastlePCGTests::Complete(Lower);
	TestFalse(TEXT("只隐藏相邻格的原闭墙"), Side->Component->IsVisible());
	TestTrue(TEXT("不触碰同面另一槽的闭墙"), OtherSide->Component->IsVisible());
	TestTrue(TEXT("撤回侧邻居"), Board->RemoveBuilding(Upper, Reason));
	CastlePCGTests::Complete(Lower);
	TestTrue(TEXT("原闭墙和拾取资格恢复"), Side->Component->IsVisible() && Side->Component->GetCollisionEnabled() == ECollisionEnabled::QueryOnly);
	Board->RemoveBuilding(Lower,Reason);

	// 双侧护栏各自替换；同色不同数字不会自动合并，能独立检查两栋目标集合。
	Definition->CreateDescriptor(2,ECastleTileColor::Red,3,Card,Reason);
	Lower=Board->PlaceBuilding(FIntVector(0,0,0),Card,false,Reason);CastlePCGTests::Complete(Lower);
	Card.Number=4;Upper=Board->PlaceBuilding(FIntVector(0,1,0),Card,false,Reason);CastlePCGTests::Complete(Upper);CastlePCGTests::Complete(Lower);
	for(auto* Building:TArray<ACastleBuildingBase*>{Lower,Upper})
	{
		TArray<UStaticMeshComponent*> Visuals;Building->GetComponents(Visuals);
		int32 Variants=0;for(auto* Mesh:Visuals){Variants+=Mesh->ComponentHasTag(TEXT("CastlePCGDynamic"));}
		TestTrue(TEXT("相邻双方各自生成开口护栏"),Variants>=2);
	}
	Board->RemoveBuilding(Upper,Reason);CastlePCGTests::Complete(Lower);Board->RemoveBuilding(Lower,Reason);

	// 上层二级的一端匹配红色3号，另一端覆盖异色异数字；覆盖格必须封闭且零梯子。
	Normal->CreateDescriptor(1,ECastleTileColor::Red,3,Card,Reason);
	Lower=Board->PlaceBuilding(FIntVector(0,0,0),Card,false,Reason);CastlePCGTests::Complete(Lower);
	FCastleBuildingDescriptor Other;Normal->CreateDescriptor(1,ECastleTileColor::Blue,4,Other,Reason);
	FCastleGroundGenerationResult Mixed=Ground;for(auto& C:Mixed.Cells){if(C.Coordinate==FIntVector(1,0,0)){C.Color=Other.Color;C.Number=Other.Number;}}
	// 不改已有占格的地面状态；只把第二座支撑建筑所需地面作为显式输入重新配置。
	Board->RemoveBuilding(Lower,Reason);
	TestTrue(TEXT("配置异色异数字支撑地面"),Board->ApplyGroundGenerationResult(Mixed,Reason));
	Lower=Board->PlaceBuilding(FIntVector(0,0,0),Card,false,Reason);CastlePCGTests::Complete(Lower);
	auto* ClosedSupport=Board->PlaceBuilding(FIntVector(1,0,0),Other,false,Reason);
	if(!TestNotNull(TEXT("异色异数字支撑建筑"),ClosedSupport)){AddError(Reason.ToString());return false;}
	CastlePCGTests::Complete(ClosedSupport);CastlePCGTests::Complete(Lower);
	Definition->CreateDescriptor(2,ECastleTileColor::Red,3,Card,Reason);
	Upper=Board->PlaceBuilding(FIntVector(0,0,1),Card,false,Reason);
	if(!TestNotNull(TEXT("二级跨两种属性支撑"),Upper)){AddError(Reason.ToString());return false;}
	CastlePCGTests::Complete(Upper);PCG->GetActiveLadderConnections(Board,Ladders);
	TestEqual(TEXT("仅匹配一端生成一把梯子，封闭端零梯子"),Ladders.Num(),1);
	if(Ladders.Num()==1){TestTrue(TEXT("梯子只属于匹配下层"),Ladders[0].Building==Lower);}
	Board->RemoveBuilding(Upper,Reason);CastlePCGTests::Complete(Lower);CastlePCGTests::Complete(ClosedSupport);
	Board->RemoveBuilding(ClosedSupport,Reason);Board->RemoveBuilding(Lower,Reason);
	return true;
}
/** 逐房原样例在正式棋盘入口拼装。保留初始 Actor 和原卡片数据，拆除验证洞、屋面和组件复原。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleV4IndependentRooms, "MovingCastle.Castle.PCG.HachimisV4.IndependentRooms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCastleV4IndependentRooms::RunTest(const FString& Parameters)
{
	UWorld* World = CastlePCGTests::World();
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
	Board->GridSize = FIntVector(5, 5, 4);
	Board->PCGRuleSet = CastlePCGTests::Rules(Board);
	UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_NormalRoom.DA_NormalRoom"));
	if (!TestNotNull(TEXT("正式普通房定义"), Definition) || !TestNotNull(TEXT("正式 PCG 规则"), Board->PCGRuleSet.Get())) { return false; }
	Board->BuildingDefinitions = {Definition};
	FText Failure;
	FCastleGroundGenerationResult Ground;
	for (int32 X = 0; X < 5; ++X)
	for (int32 Y = 0; Y < 5; ++Y)
	{
		FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
		Cell.Coordinate = FIntVector(X, Y, 0);
		Cell.Color = ECastleTileColor::Red;
		Cell.Number = 3;
	}
	if (!Board->ApplyGroundGenerationResult(Ground, Failure)) { AddError(Failure.ToString()); return false; }
	FCastleBuildingDescriptor Card;
	if (!Definition->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Failure)) { AddError(Failure.ToString()); return false; }
	TMap<FIntVector, ACastleBuildingBase*> Placed;
	UCastleBuildingPCGSubsystem* PCG = World->GetSubsystem<UCastleBuildingPCGSubsystem>();
	auto CompleteAll = [&]()
	{
		for (auto& Pair : Placed) { CastlePCGTests::Complete(Pair.Value); }
	};
	auto Place = [&](const FIntVector& Cell)
	{
		ACastleBuildingBase* Room = Board->PlaceBuilding(Cell, Card, false, Failure);
		if (!TestNotNull(TEXT("通过正式棋盘放置一级房"), Room)) { AddError(Failure.ToString()); return false; }
		Placed.Add(Cell, Room);
		CompleteAll();
		CompleteAll();
		return TestTrue(TEXT("原房完成放置并保留明确 PCG 目标"), Room->bHasCompletedPlacement && !Room->BuildingPCG->GetAppliedKey().IsEmpty());
	};
	auto Verify = [&](FName DeckForm, FName FrameForm, int32 Links)
	{
		CastleV4PCG::FTargets Targets;
		for (auto& Pair : Placed)
		{
			const ACastleBuildingBase* Room = Board->GetBuildingAt(Pair.Key);
			TestEqual(TEXT("相邻 PCG 后原 Actor 独立存在"), Room, static_cast<const ACastleBuildingBase*>(Pair.Value));
			if (!Room) { return false; }
			TestTrue(TEXT("每房仍保留原色、数、一级及单格"), Room->GetBuildingDefinition() == Definition && Room->GetBuildingLevel() == 1
				&& Room->GetBuildingColor() == Card.Color && Room->GetBuildingNumber() == Card.Number
				&& Room->GetOccupiedCoordinates() == TArray<FIntVector>{Pair.Key});
			Targets.Add(Pair.Value);
		}
		TArray<FCastleActiveLadderConnection> Ladders;
		PCG->GetActiveLadderConnections(Board, Ladders);
		TestEqual(TEXT("互达层界择出期望梯数"), Ladders.Num(), Links);
		FString Error;
		CastleV4PCG::FRooms Rooms;
		if (!CastleV4PCG::BuildRooms(*Board->PCGRuleSet, Placed, Targets, Rooms, Error)) { AddError(Error); return false; }
		const auto& Origin = Rooms.FindChecked(FIntVector::ZeroValue);
		TestEqual(TEXT("普通房使用对应作者底盘"), Origin.Deck.Layout->Form, DeckForm);
		TestEqual(TEXT("同一格拥有自己的样例结构或保持露天"), Origin.Frame.Layout ? Origin.Frame.Layout->Form : NAME_None, FrameForm);
		return true;
	};
	const FIntVector A(0,0,0), B(1,0,0), C(0,1,0), D(1,1,0), Up(0,0,1);
	if (!Place(A) || !Verify(TEXT("L1"), NAME_None, 0)) { return false; }
	if (!Place(A + Up) || !Verify(TEXT("L1"), TEXT("L2V"), 1)) { return false; }
	auto* Backpack = NewObject<UCastleBackpackComponent>(Board);
	Backpack->RegisterComponent();
	if (!TestTrue(TEXT("竖向房可单独回收为一级卡"), Board->RemoveBuildingToBackpack(Placed.FindChecked(A + Up), Backpack, Failure)))
	{ AddError(Failure.ToString()); return false; }
	Placed.Remove(A + Up);
	CompleteAll();
	FCastleBuildingDescriptor Recovered;
	TestTrue(TEXT("背包恰好返回被回收房间的一张原卡"), Backpack->GetItemCount() == 1 && Backpack->TryGetItemAt(0, Recovered)
		&& Recovered.Level == 1 && Recovered.Color == Card.Color && Recovered.Number == Card.Number && Recovered.Definition == Definition);
	if (!Place(C) || !Verify(TEXT("L2H"), NAME_None, 0)) { return false; }
	if (!Place(C + Up) || !Place(A + Up) || !Verify(TEXT("L2H"), TEXT("L3V"), 1)) { return false; }
	if (!Place(D) || !Place(B) || !Verify(TEXT("L3H"), TEXT("L3V"), 1)) { return false; }
	if (!Place(D + Up) || !Place(B + Up) || !Verify(TEXT("L3H"), TEXT("L4"), 1)) { return false; }
	// 天面以上，从格心发射选择射线。命中的造型部件必须属于该房，而非整座样例的共同演员。
	const FVector DTop = Board->GetGridWorldLocation(D + Up);
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(HachimisRoomSelect), true);
	TestTrue(TEXT("屋面选中命中被指向的独立边角房"), World->LineTraceSingleByChannel(Hit, DTop + FVector(0,0,650),
		DTop - FVector(0,0,200), ECC_Visibility, Query) && Hit.GetActor() == Placed.FindChecked(D + Up));
	if (!TestTrue(TEXT("移除一个角变为七格"), Board->RemoveBuilding(Placed.FindChecked(D + Up), Failure)))
	{ AddError(Failure.ToString()); return false; }
	Placed.Remove(D + Up);
	CompleteAll();
	if (!Verify(TEXT("L3H"), TEXT("L3V"), 1) || !Place(D + Up) || !Verify(TEXT("L3H"), TEXT("L4"), 1)) { return false; }
	TMap<ACastleBuildingBase*, FString> Keys;
	for (auto& Pair : Placed) { Keys.Add(Pair.Value, Pair.Value->BuildingPCG->GetAppliedKey()); }
	FString Error;
	if (!PCG->Recompute(Board, FVector::ZeroVector, Error)) { AddError(Error); return false; }
	for (auto& Pair : Keys)
	{
		TestEqual(TEXT("重算后相同的 Room 计划保持原键"), Pair.Key->BuildingPCG->GetAppliedKey(), Pair.Value);
		TestFalse(TEXT("幂等布局不重播局部动画"), Pair.Key->BuildingMerge->bIsPlaying);
	}
	// 在世界释放导航前拿掉所有测试楼层。撤除后还可按另一顺序重建相同格局。
	auto ClearRooms = [&]()
	{
		TArray<FIntVector> Cells;
		Placed.GenerateKeyArray(Cells);
		Cells.Sort([](const FIntVector& First, const FIntVector& Second){ return First.Z > Second.Z; });
		for (const FIntVector& Cell : Cells)
		{
			if (!Board->RemoveBuilding(Placed.FindChecked(Cell), Failure)) { AddError(Failure.ToString()); return false; }
			Placed.Remove(Cell);
			CompleteAll();
		}
		return true;
	};
	TMap<FIntVector, FString> LayoutBefore;
	for (const auto& Pair : Placed) { LayoutBefore.Add(Pair.Key, Pair.Value->BuildingPCG->GetAppliedKey()); }
	if (!ClearRooms()) { return false; }
	for (const FIntVector& Cell : {D, B, C, A, D+Up, B+Up, C+Up, A+Up}) { if (!Place(Cell)) { return false; } }
	for (const auto& Pair : Placed)
	{
		TestEqual(TEXT("逐层倒序放置后同一格得到同一套构件"), Pair.Value->BuildingPCG->GetAppliedKey(), LayoutBefore.FindChecked(Pair.Key));
	}
	if (!ClearRooms()) { return false; }

	// 下层横向互达时，两个不相连的上层岛也要各有入口。连起中间格后只保留一个确定性梯位。
	const FIntVector E(2,0,0);
	for (const FIntVector& Cell : {A, B, E, A+Up, E+Up}) { if (!Place(Cell)) { return false; } }
	TArray<FCastleActiveLadderConnection> Links;
	PCG->GetActiveLadderConnections(Board, Links);
	TestEqual(TEXT("上下分量配对保留两个不互达入口"), Links.Num(), 2);
	if (!Place(B + Up) || !Place(E + Up * 2)) { return false; }
	PCG->GetActiveLadderConnections(Board, Links);
	TestEqual(TEXT("三层错位时两道层界分别择梯"), Links.Num(), 2);
	TestTrue(TEXT("三层梯的上下格都属于真实占用"), Algo::AllOf(Links, [&](const FCastleActiveLadderConnection& Link)
	{
		return Placed.Contains(Link.LowerCell) && Placed.Contains(Link.UpperCell);
	}));
	return ClearRooms();
}
#endif