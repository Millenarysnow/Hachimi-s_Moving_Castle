#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "CastleBuildingPCGSubsystem.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleGroundTileActor.h"
#include "BuildingMergeComponent.h"
#include "CastleLadderLinkComponent.h"
#include "CastleNavigationSubsystem.h"
#include "CastleMassSpawner.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Interaction/CastlePatrolWaypointMarker.h"
#include "Mass/CastleMassPatrolFragments.h"
#include "Mass/CastleMassPatrolProcessor.h"
#include "MassCommonFragments.h"
#include "MassMovementFragments.h"
#include "MassEntityConfigAsset.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Units/BuildingInteraction/Mass/CastleMassBuildingTriggerProcessor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Misc/AutomationTest.h"

namespace CastlePCGNavigationTests
{
	/** 真实 TestMap1 中的完整 PCG→Recast→共享路线→Mass巡逻→进入触发链路。 */
	class FCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FCommand(FAutomationTestBase* InTest):Test(InTest){}
		virtual bool Update() override
		{
			if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("PCG正式建筑跨层验收超时120秒。"));Cleanup();return true;}
			if(!Board)
			{
				for(const auto& Context:GEngine->GetWorldContexts()) if(Context.WorldType==EWorldType::Game)
				{for(TActorIterator<ACastleBoard> It(Context.World());It;++It){Board=*It;break;}}
				if(!Board){return false;}
				World=Board->GetWorld();Nav=World->GetSubsystem<UCastleNavigationSubsystem>();
				if(!Test->TestNotNull(TEXT("TestMap1配置正式PCG规则"),Board->PCGRuleSet.Get())){return true;}
			}
			if(!Nav->IsNavigationReady()){return false;}
			if(Stage==0){return Place();}
			if(Stage==1){return Route();}
			if(Stage==2){return Patrol();}
			if(Stage==3){return Removed();}
			if(Stage==4){return true;}
			return false;
		}
	private:
		bool Place()
		{
			bool bBounds=false;
			for(TActorIterator<ANavMeshBoundsVolume> It(World);It;++It)
			{const FBox Box=It->GetComponentsBoundingBox(true);bBounds|=Box.Max.Z>=2500.0&&Box.GetSize().Z>=2500.0;}
			if(!Test->TestTrue(TEXT("正式地图导航体积顶面及高度覆盖至少2500cm"),bBounds)){return true;}
			auto* Definition=LoadObject<UCastleBuildingDefinition>(nullptr,TEXT("/Game/MovingCastle/Data/Buildings/DA_NormalRoom.DA_NormalRoom"));
			FCastleGroundCell Ground;
			if(!Test->TestTrue(TEXT("读取原点实际地面"),Board->TryGetGroundCell(Board->GridOrigin,Ground))){return true;}
			FCastleBuildingDescriptor Card;FText Error;
			Definition->CreateDescriptor(1,Ground.Color,Ground.Number,Card,Error);
			Lower=Board->PlaceBuilding(Board->GridOrigin,Card,false,Error);
			if(!Test->TestNotNull(TEXT("正式下层房间放置"),Lower)){Test->AddError(Error.ToString());return true;}
			Stage=5;
			// 原生放置动画由真实World Tick推进，延迟命令继续到上层放置。
			Stage=1;
			return false;
		}
		bool Route()
		{
			if(!Lower->bHasCompletedPlacement){return false;}
			if(!Upper)
			{
				FCastleBuildingDescriptor Card=Lower->GetBuildingDescriptor();Card.Number=Card.Number==9?1:Card.Number+1;
				FText Error;Upper=Board->PlaceBuilding(Board->GridOrigin+FIntVector(0,0,1),Card,false,Error);
				if(!Test->TestNotNull(TEXT("可走通上层正式房间放置"),Upper)){Test->AddError(Error.ToString());Cleanup();return true;}
				return false;
			}
			if(!Upper->bHasCompletedPlacement||Lower->BuildingMerge->bIsPlaying){return false;}
			TArray<FCastleActiveLadderConnection> Connections;
			World->GetSubsystem<UCastleBuildingPCGSubsystem>()->GetActiveLadderConnections(Board,Connections);
			if(!Test->TestEqual(TEXT("正式叠放动态激活唯一梯子"),Connections.Num(),1)){Cleanup();return true;}
			Ladder=Lower->FindComponentByClass<UCastleLadderLinkComponent>();
			if(!Test->TestNotNull(TEXT("PCG创建原生梯子NavLink"),Ladder)){Cleanup();return true;}
			UNavigationSystemV1* System=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FVector Bottom,Top;ENavLinkDirection::Type Direction;
			Ladder->GetLinkData(Bottom,Top,Direction);
			Bottom=Lower->GetActorTransform().TransformPosition(Bottom);Top=Lower->GetActorTransform().TransformPosition(Top);
			FNavLocation P;
			Test->TestTrue(TEXT("下端在真实Recast多边形内部精确投影"),System->ProjectPointToNavigation(Bottom,P,FVector(10,10,20))&&FVector::Dist2D(Bottom,P.Location)<1.0);
			Test->TestTrue(TEXT("上端在真实Recast多边形内部精确投影"),System->ProjectPointToNavigation(Top,P,FVector(10,10,20))&&FVector::Dist2D(Top,P.Location)<1.0);
			Test->TestEqual(TEXT("动态Link双向"),Direction,ENavLinkDirection::BothWays);
			Test->AddInfo(FString::Printf(TEXT("真实LV2接驳 Bottom=%s Top=%s 梯底=%s 梯顶=%s"),*Ladder->BottomStandOffset.ToString(),*Ladder->TopStandOffset.ToString(),*Ladder->LadderBottomOffset.ToString(),*Ladder->LadderTopOffset.ToString()));
			// 使用同一条垂直列，但分别从两层房间内部点击；提交的是点击结果而不是人工指定的楼层。
			FVector LowerPoint, UpperPoint;
			if (!VerifyFloorClick(Board->GridOrigin, Bottom.Z, false, LowerPoint)
				|| !VerifyFloorClick(Board->GridOrigin + FIntVector(0, 0, 1), Top.Z, true, UpperPoint))
			{ Cleanup(); return true; }
			ClickedFloors = {LowerPoint, UpperPoint};
			TActorIterator<ACastlePatrolRoute> Route(World);
			if (!Test->TestTrue(TEXT("TestMap1 包含正式巡逻编辑入口"), static_cast<bool>(Route))) { Cleanup(); return true; }
			FText Error;
			if (!Test->TestTrue(TEXT("通过正式巡逻入口提交两个点击楼层"), Route->SetWorldWaypoints(ClickedFloors, Error)))
			{ Test->AddError(Error.ToString()); Cleanup(); return true; }
			Stage=2;return false;
		}
		bool VerifyFloorClick(FIntVector Cell, double FloorZ, bool bUpperFloor, FVector& OutPoint)
		{
			FVector Expected = Board->GetGridWorldLocation(Cell);
			Expected.Z = FloorZ;
			const FVector Start = Expected + FVector(120, 120, 180);
			const FVector End = Start - FVector(0, 0, 100000);
			FHitResult VisibilityHit, PhysicalHit;
			const FCollisionQueryParams Query(SCENE_QUERY_STAT(StackedPatrolFloors), true);
			World->LineTraceSingleByChannel(VisibilityHit, Start, End, ECC_Visibility, Query);
			const bool bFloorHit = World->LineTraceSingleByChannel(PhysicalHit, Start, End, ECC_Pawn, Query);
			Test->AddInfo(FString::Printf(TEXT("叠层取点：Visibility=%s/%s %s；Pawn=%s/%s %s"),
				*GetNameSafe(VisibilityHit.GetActor()), *GetNameSafe(VisibilityHit.GetComponent()), *VisibilityHit.ImpactPoint.ToString(),
				*GetNameSafe(PhysicalHit.GetActor()), *GetNameSafe(PhysicalHit.GetComponent()), *PhysicalHit.ImpactPoint.ToString()));
			if (!Test->TestTrue(TEXT("同 XY 的每层都有真实可点击功能楼板"), bFloorHit
				&& UBuildingGameplayRootComponent::IsInGameplayHierarchy(PhysicalHit.GetComponent())
				&& FMath::IsNearlyEqual(PhysicalHit.ImpactPoint.Z, FloorZ, 1.0))) { return false; }
			if (bUpperFloor)
			{
				// 关键复现条件：美术命中归下层 Actor，不能再据此把上层楼板降一层。
				if (!Test->TestTrue(TEXT("上层视线命中下层建筑拥有的平板美术"), VisibilityHit.GetActor() == Lower
					&& FMath::IsNearlyEqual(VisibilityHit.ImpactPoint.Z, FloorZ, 1.0))) { return false; }
			}
			else if (Cast<ACastleGroundTileActor>(VisibilityHit.GetActor()))
			{
				// TestMap1 的第一层地块是更靠前的选中面，沿用它的顶面 Z；提交后仍应投影到下层真实楼板。
				Expected.Z = VisibilityHit.ImpactPoint.Z;
			}
			FText Error;
			if (!Test->TestTrue(TEXT("能从楼内射线解析路径点"), ACastlePatrolRoute::TryResolveWaypointFromRay(World, Start, End, OutPoint, Error)))
			{ Test->AddError(Error.ToString()); return false; }
			return Test->TestTrue(FString::Printf(TEXT("路径点属于实际点击层的格心（期望 %s，实际 %s）"),
				*Expected.ToCompactString(), *OutPoint.ToCompactString()), OutPoint.Equals(Expected, 1.0));
		}
		bool Patrol()
		{
			const auto Path=Nav->AcquireSharedPath();
			if(!Path||!Path->bValid){return false;}
			// 正式入口在 World Tick 中生成编号蓝图，校验显示点、请求点与真实投影不仅同 XY，且各自留在点击层。
			Test->TestTrue(TEXT("正式请求原样保存两个已吸附的点击点"), Nav->GetRequestedWaypoints() == ClickedFloors);
			if (Test->TestEqual(TEXT("NavMesh 分别投影两个点击楼层"), Path->Waypoints.Num(), ClickedFloors.Num()))
			{
				for (int32 Index = 0; Index < ClickedFloors.Num(); ++Index)
				{
					Test->TestTrue(TEXT("投影保持点击楼层和水平格心"), FMath::Abs(Path->Waypoints[Index].Z - ClickedFloors[Index].Z) <= Nav->ProjectionExtent.Z
						&& FVector::Dist2D(Path->Waypoints[Index], ClickedFloors[Index]) < 1.0);
				}
			}
			TSet<int32> Numbers;
			for (TActorIterator<ACastlePatrolWaypointMarker> It(World); It; ++It)
			{
				const int32 Index = It->GetDisplayedNumber() - 1;
				if (Test->TestTrue(TEXT("叠层标记编号有效且不重复"), ClickedFloors.IsValidIndex(Index) && !Numbers.Contains(Index)))
				{
					Numbers.Add(Index);
					Test->TestTrue(TEXT("实际编号标记在各自点击层的格心"), It->GetActorLocation().Equals(ClickedFloors[Index], 1.0));
				}
			}
			Test->TestEqual(TEXT("上下两层各有一个编号标记"), Numbers.Num(), ClickedFloors.Num());
			Test->TestEqual(TEXT("PCG梯子上下各一段攀爬区间"),Path->LadderRanges.Num(),2);
			auto* Config=NewObject<UMassEntityConfigAsset>(World);
			auto* Trait=NewObject<UCastleMassNPCTrait>(Config);
			Trait->Movement.Acceleration=0.0f;Trait->Movement.MaxSpeed=200.0f;Trait->Movement.LadderSpeedMultiplier=1.0f;
			Config->GetMutableConfig().AddTrait(*Trait);
			Spawner=World->SpawnActorDeferred<ACastleMassSpawner>(ACastleMassSpawner::StaticClass(),FTransform::Identity);
			Spawner->Configuration=Config;Spawner->Count=1;Spawner->Columns=1;Spawner->bFollowSharedPatrolRoute=true;
			Spawner->FinishSpawning(FTransform::Identity);
			Manager=&World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();Manager->FlushCommands();
			if(!Test->TestEqual(TEXT("生成一个真实巡逻NPC"),Spawner->SpawnedUnits.Num(),1)){Cleanup();return true;}
			const auto Entity=Spawner->SpawnedUnits[0].Entity;
			auto& Patrol=Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Entity);
			Patrol.Distance=0;Patrol.Speed=0;Patrol.PathRevision=Path->PathRevision;Patrol.RouteRevision=Path->RouteRevision;Patrol.bBlocked=false;
			Manager->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Path->Points[0]);
			const int64 LowerBefore=Lower->BuildingEffect->TriggerCount,UpperBefore=Upper->BuildingEffect->TriggerCount;
			const int32 Queries=Nav->PathQueryCount;
			for(int32 Step=0;Step<240&&Patrol.CompletedLaps<1;++Step){Run<UCastleMassPatrolProcessor>(0.1f);Run<UCastleMassBuildingTriggerProcessor>(0);}
			Test->TestTrue(TEXT("实际NPC完成上下双向爬梯闭环"),Patrol.CompletedLaps>=1);
			Test->TestTrue(TEXT("巡逻触发下层正式建筑"),Lower->BuildingEffect->TriggerCount>LowerBefore);
			Test->TestTrue(TEXT("巡逻触发上层正式建筑"),Upper->BuildingEffect->TriggerCount>UpperBefore);
			Test->TestEqual(TEXT("NPC运动不增加集中寻路次数"),Nav->PathQueryCount,Queries);
			FText Error;
			FVector AnchorBottom,AnchorTop;ENavLinkDirection::Type AnchorDirection;
			Ladder->GetLinkData(AnchorBottom,AnchorTop,AnchorDirection);
			AnchorBottom=Lower->GetActorTransform().TransformPosition(AnchorBottom);AnchorTop=Lower->GetActorTransform().TransformPosition(AnchorTop);
			Test->TestTrue(TEXT("路径点直接落在Link锚点也可求解"),Nav->RequestSharedPatrolRoute({AnchorBottom,AnchorTop},Error));
			const auto AnchorPath=Nav->AcquireSharedPath();
			Test->TestTrue(TEXT("锚点查询不能发布跨层直线或零长度路线"),AnchorPath&&AnchorPath->bValid&&AnchorPath->LadderRanges.Num()==2&&AnchorPath->Length>=1120.0);
			Test->AddExpectedMessage(TEXT("NPC 导航：共享路线起点或终点附近没有可行走 NavMesh"),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,1,false);
			Board->RemoveBuilding(Upper,Error);Upper=nullptr;
			Stage=3;return false;
		}
		bool Removed()
		{
			if(Lower->BuildingMerge->bIsPlaying){return false;}
			const auto Path=Nav->AcquireSharedPath();
			Test->TestTrue(TEXT("移除上层后共享路线失效"),Path&&!Path->bValid);
			Run<UCastleMassPatrolProcessor>(0.1f);
			const auto Entity=Spawner->SpawnedUnits[0].Entity;
			Test->TestEqual(TEXT("梯子移除后NPC速度归零"),Manager->GetFragmentDataChecked<FCastleMassPatrolFragment>(Entity).Speed,0.0);
			Test->TestTrue(TEXT("梯子移除后Velocity归零"),Manager->GetFragmentDataChecked<FMassVelocityFragment>(Entity).Value.IsZero());
			Cleanup();Stage=4;return false;
		}
		template<typename T>void Run(float Delta)
		{
			T* Processor=NewObject<T>(World);Processor->CallInitialize(World,Manager->AsShared());
			UE::Mass::FProcessingContext Context(*Manager,Delta);UE::Mass::Executor::Run(*Processor,Context);
		}
		void Cleanup()
		{
			if(Nav){Nav->ClearSharedRoute();}if(IsValid(Spawner)){Spawner->Destroy();}
			FText Error;if(IsValid(Upper)){Board->RemoveBuilding(Upper,Error);}if(IsValid(Lower)){Board->RemoveBuilding(Lower,Error);}
		}
		FAutomationTestBase* Test;double Deadline=FPlatformTime::Seconds()+120;int32 Stage=0;
		ACastleBoard* Board=nullptr;ACastleBuildingBase* Lower=nullptr;ACastleBuildingBase* Upper=nullptr;
		UWorld* World=nullptr;UCastleNavigationSubsystem* Nav=nullptr;UCastleLadderLinkComponent* Ladder=nullptr;
		ACastleMassSpawner* Spawner=nullptr;FMassEntityManager* Manager=nullptr;
		/** 本次叠层场景的原始点击请求，用于和标记及投影结果逐点比对。 */
		TArray<FVector> ClickedFloors;
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePCGNavigationTest,"MovingCastle.Castle.PCG.NavigationEndToEnd",
	EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FCastlePCGNavigationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastlePCGNavigationTests::FCommand(this));return true;
}
#endif
