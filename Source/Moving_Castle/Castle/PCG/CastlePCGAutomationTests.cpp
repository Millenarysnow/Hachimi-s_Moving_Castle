#if WITH_DEV_AUTOMATION_TESTS
#include "CastleBuildingPCGSubsystem.h"
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
#include "NavigationSystem.h"

namespace CastlePCGTests
{
	/** 测试显式提供正式资产与规则，生产路径只读取 DA 配置。 */
	UCastlePCGRuleSetDataAsset* Rules(UObject* Outer)
	{
		auto* Result = NewObject<UCastlePCGRuleSetDataAsset>(Outer);
		Result->VariantLV1ActorClass = LoadClass<AActor>(nullptr, TEXT("/Game/MovingCastle/BP/BP_Variant_LV1.BP_Variant_LV1_C"));
		Result->VariantLV2ActorClass = LoadClass<AActor>(nullptr, TEXT("/Game/MovingCastle/BP/BP_Variant_LV2.BP_Variant_LV2_C"));
		Result->FlatFloorSlabMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/MovingCastle/Model/Buildings/DaoDun/NormalRoom/Mesh9.Mesh9"));
		Result->Rules = {NewObject<UCastlePCGVerticalRule>(Result), NewObject<UCastlePCGPlanarRule>(Result)};
		return Result;
	}
	/** 游戏世界保留正式放置和快照组件语义，但本组不创建 Recast；导航单独在真实地图验证。 */
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
	for (const TCHAR* Family : {TEXT("NormalRoom"),TEXT("ArcherTower"),TEXT("CannonTower"),TEXT("Farm"),TEXT("ShieldTower")})
	{
		const FString Path = FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), Family, Family);
		auto* Definition = LoadObject<UCastleBuildingDefinition>(nullptr, *Path);
		if (!TestNotNull(Path, Definition)) { return false; }
		for (const auto& Level : Definition->Levels)
		{
			if (Level.Level > 4) { continue; }
			for (const bool bVertical : {false,true})
			{
				if (bVertical && !Level.VerticalBuildingClass) { continue; }
				for (const bool bRotated : {false,true})
				{
					const FIntVector Shape = Level.Level == 1 ? FIntVector(1,1,1) : (Level.Level == 2
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
					Building->SetActorLocation(FVector(bRotated ? (Shape.Y-1)*260.0 : (Shape.X-1)*260.0,
						bRotated ? (Shape.X-1)*260.0 : (Shape.Y-1)*260.0, Board->WorldBaseHeight - 280.0));
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
					const bool bFlat = FString(Family)==TEXT("NormalRoom") || FString(Family)==TEXT("ShieldTower");
					const int32 TopCount = Shape.X*Shape.Y;
					TestEqual(TEXT("屋顶数量对应每个顶层格"),Roof,FString(Family)==TEXT("NormalRoom") ? 0 : TopCount);
					TestEqual(TEXT("平板数量含内部层和封闭顶层"),Slab,Shape.X*Shape.Y*(Shape.Z-1)+(bFlat ? TopCount:0));
					TestEqual(TEXT("跨格护栏只出现在水平二级和四级"),Double,(!bVertical && (Level.Level==2 || Level.Level==4)) ? 1:0);
					const int32 ExpectedRails = Level.Level==1 ? 0 : (bVertical ? (Level.Level==2?4:8) : (Level.Level==2?3:(Level.Level==3?4:11)));
					TestEqual(TEXT("正式模型护栏数精确对应等级结构"),Rail,ExpectedRails);
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
	// 不推进下层动画直接撤回，旧件快照必须接续当前姿态而不丢零件。
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
	const auto* Double=Lower->BuildingPCG->GetOriginalParts().FindByPredicate([](const FCastlePCGPartAnnotation& P){return P.Role==ECastlePCGPartRole::PerimeterRailing&&P.CoveredCellOffsets.Num()==2;});
	if(!TestNotNull(TEXT("找到跨两格整体护栏"),Double)){return false;}
	UPrimitiveComponent* OriginalDouble=Double->Component;
	const FIntVector NeighborCell=FIntVector(2,2,0)+Double->CoveredCellOffsets[0]+CastlePCGSemantics::FaceOffset(Double->BoardFacing);
	Normal->CreateDescriptor(1,ECastleTileColor::Red,4,Card,Reason);
	Upper=Board->PlaceBuilding(NeighborCell,Card,false,Reason);
	if(!TestNotNull(TEXT("只连接整体护栏的一格"),Upper)){AddError(Reason.ToString());return false;}
	CastlePCGTests::Complete(Upper);CastlePCGTests::Complete(Lower);
	TestFalse(TEXT("跨双格原护栏被非破坏性隐藏"),OriginalDouble->IsVisible());
	TArray<UStaticMeshComponent*> Meshes;Lower->GetComponents(Meshes);
	int32 Generated=0;for(auto* Mesh:Meshes){Generated+=Mesh->ComponentHasTag(TEXT("CastlePCGDynamic"));}
	TestEqual(TEXT("一格开口变体+另一格完整护栏"),Generated,2);
	TestTrue(TEXT("撤回侧邻居"),Board->RemoveBuilding(Upper,Reason));
	CastlePCGTests::Complete(Lower);
	TestTrue(TEXT("双格原护栏精确恢复"),OriginalDouble->IsVisible());
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
#endif