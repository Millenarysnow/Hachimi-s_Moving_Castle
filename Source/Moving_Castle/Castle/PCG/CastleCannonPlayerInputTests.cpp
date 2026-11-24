#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Castle/Actors/CastleBoard.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/Actors/CastleGroundTileActor.h"
#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Components/CastleBackpackComponent.h"
#include "Castle/Data/CastleBuildingDefinition.h"
#include "HachimisV4/CastleHachimisV4Layout.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/Animation/BuildingMergeComponent.h"
#include "Units/Navigation/Interaction/CastlePatrolRoute.h"
#include "Units/Navigation/CastleNavigationSubsystem.h"
#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"
#include "Tests/AutomationCommon.h"

namespace CastleCannonPlayerInputTests
{
	/** 在已加载地图的真实玩家视口执行输入；没有棋盘/视口就明确失败，不换地图或补建导航面。 */
	class FCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds()>Deadline)
			{ Test->AddError(FString::Printf(TEXT("玩家取点扩大复现超时：Family=%d Scenario=%d Stage=%d"),Family,Scenario,Stage)); Cleanup(); return true; }
			if (GFrameCounter < NextInputCheckFrame) { return false; }
			if (!Board) { return Initialize(); }
			if (!Nav->IsNavigationReady()) { return false; }
			if (Stage==0) { return Prepare(); }
			if (Stage==1 || Stage==4)
			{
				const auto Target=Controller->GetCurrentGridCursorTarget();
				const FIntVector Expected=Stage==1?Center:Center+FIntVector(1,0,0);
				if (!Test->TestTrue(TEXT("视口光标射线命中预期地面格"),Target.bValid && Target.Surface == ECastleGridCursorSurface::Ground && Target.Cell == Expected)) { Cleanup(); return true; }
				Press(EKeys::One); ++Stage; return false;
			}
			if (Stage==2 || Stage==5)
			{
				if (!Test->TestEqual(TEXT("数字键选中真实背包首槽"),Controller->GetSelectedBackpackIndex(),0)) { Cleanup(); return true; }
				// 炮塔放置和后续合成均走玩家当前预设的世界主交互键。
				Press(Controller->GetPrimaryPlaceInteractKey()); ++Stage; return false;
			}
			if (Stage==3 || Stage==6)
			{
				const int32 Count=Stage==3?1:2;
				const auto Buildings=Board->GetPlacedBuildings();
				if (!Test->TestEqual(TEXT("主交互键通过正式光标入口放置建筑"),Buildings.Num(),Count)) { Cleanup(); return true; }
				for (auto* B:Buildings) { if (!B->bHasCompletedPlacement||B->BuildingMerge->bIsPlaying) { return false; } }
				if (Stage==3) { AimGround(Center+FIntVector(1,0,0)); Stage=4; return false; }
				for (auto* B:Buildings) { if (B->GetOccupiedCoordinates().Contains(Center)) { Cannon=B; } else { Neighbor=B; } }
				if (!Test->TestNotNull(TEXT("真实一级炮塔"),Cannon)||!Test->TestNotNull(TEXT("真实相邻族建筑"),Neighbor)) { Cleanup(); return true; }
				BeginScenario(); return false;
			}
			if (Stage==10)
			{
				if (!Capture(Cannon,FirstHit,FirstExpected,FirstProjected)) { Cleanup(); return true; }
				Press(EKeys::P); Stage=11; return false;
			}
			if (Stage==11)
			{
				if (!Test->TestTrue(TEXT("P键实际进入巡逻草稿"),Route->IsEditing())) { Cleanup(); return true; }
				Press(Controller->GetPrimaryPlaceInteractKey()); Stage=12; return false;
			}
			if (Stage==12) { AimBuilding(Neighbor,Scenario==2); Stage=13; return false; }
			if (Stage==13)
			{
				if (!Capture(Neighbor,SecondHit,SecondExpected,SecondProjected)) { Cleanup(); return true; }
				Press(Controller->GetPrimaryPlaceInteractKey()); Stage=14; return false;
			}
			if (Stage==14)
			{
				Press(EKeys::Enter); Stage=15; return false;
			}
			if (Stage==15)
			{
				// 模拟 Enter 由下一次 PlayerInput 派发。先等待路线退出编辑态，再核对真实提交结果。
				if (Route->IsEditing()) { return false; }
				const auto& Points=Nav->GetRequestedWaypoints();
				if (!Test->TestEqual(TEXT("Enter经真实路径编辑提交两个点"),Points.Num(),2)) { Cleanup(); return true; }
				// 期望值由同一次原生射线与棋盘格坐标独立计算，不调用被测取点函数伪造预期。
				// 地块保留顶面高度，建筑取命中格的作者站立面。期望使用持久化的数据计算。
				Test->TestTrue(Cast<ACastleGroundTileActor>(FirstHit.GetActor())
					? TEXT("地块路径点1对齐格心并保留顶面高度") : TEXT("建筑路径点1等于D26本层楼板点"),Points[0].Equals(FirstExpected,0.1));
				Test->TestTrue(Cast<ACastleGroundTileActor>(SecondHit.GetActor())
					? TEXT("地块路径点2对齐格心并保留顶面高度") : TEXT("建筑路径点2等于D26本层楼板点"),Points[1].Equals(SecondExpected,0.1));
				Test->AddInfo(FString::Printf(TEXT("W4_INPUT_D26 P1=%s Expected1=%s Hit1=%s P2=%s Expected2=%s Hit2=%s"),
					*Points[0].ToString(),*FirstExpected.ToString(),*FirstHit.ImpactPoint.ToString(),
					*Points[1].ToString(),*SecondExpected.ToString(),*SecondHit.ImpactPoint.ToString()));
				const auto Path=Nav->AcquireSharedPath();
				const bool bLoop=Path&&Path->bValid&&Path->bClosedLoop;
				Test->AddInfo(FString::Printf(TEXT("W1_INPUT Family=%s Scenario=%s Project=(%d,%d) ValidLoop=%d LastFailure=%s"),
					Families[Family],Scenarios[Scenario],FirstProjected,SecondProjected,bLoop,*Nav->LastFailure.ToString()));
				// 原始美术屋顶的投影只作根因诊断；真实键鼠提交点经 D26 转换后，三种场景都必须闭环。
				Test->TestTrue(TEXT("楼顶与楼内真实输入均形成跨界有效闭环"),bLoop);
				Nav->ClearSharedRoute(); ++Scenario;
				if (Scenario<3) { BeginScenario(); return false; }
				if (!RemoveBuildings()) { Cleanup(); return true; }
				++Family; Scenario=0; Stage=0; Deadline=FPlatformTime::Seconds()+120.0;
				if (Family==4) { Cleanup(); return true; }
			}
			return false;
		}
	private:
		bool Initialize()
		{
			for (const auto& Context:GEngine->GetWorldContexts())
			{
				if (Context.WorldType==EWorldType::PIE||Context.WorldType==EWorldType::Game)
				{
					World=Context.World();
					for (TActorIterator<ACastleBoard> It(World);It;++It) { Board=*It; break; }
					if (Board) { break; }
				}
			}
			// TestMap1 允许流送初始化；给原生加载流程时间，不将首帧未出现误判为资产缺失。
			// 超过明确的初始化等待时间仍不存在时失败，绝不补建棋盘或切换地图。
			if (!Board && FPlatformTime::Seconds()<InitializationDeadline) { return false; }
			if (!Test->TestNotNull(TEXT("当前TestMap1测试世界必须包含正式棋盘"),Board)) { return true; }
			Test->AddInfo(FString::Printf(TEXT("W1_INPUT_MAP %s Type=%d"),*World->GetOutermost()->GetName(),int32(World->WorldType)));
			if (!Test->TestEqual(TEXT("要求空棋盘，不清理用户内容"),Board->GetPlacedBuildingCount(),0)) { return true; }
			Controller=Cast<ACastlePlayerController>(World->GetFirstPlayerController());
			Nav=World->GetSubsystem<UCastleNavigationSubsystem>();
			for (TActorIterator<ACastlePatrolRoute> It(World);It;++It) { Route=*It; break; }
			if (!Test->TestNotNull(TEXT("正式玩家控制器"),Controller)||!Test->TestNotNull(TEXT("正式巡逻编辑Actor"),Route)||!Test->TestNotNull(TEXT("导航子系统"),Nav)) { return true; }
			int32 Width,Height; Controller->GetViewportSize(Width,Height);
			if (!Test->TestTrue(TEXT("真实输入复现必须有有效玩家视口"),Width>0&&Height>0)) { return true; }
			if (!Test->TestEqual(TEXT("夹具要求原始路线为空"),Nav->GetRequestedWaypoints().Num(),0)) { return true; }
			for (int32 X=0;X<Board->GridSize.X;++X) for (int32 Y=0;Y<Board->GridSize.Y;++Y)
			{
				FCastleGroundCell Cell;
				if (!Board->TryGetGroundCell(Board->GridOrigin+FIntVector(X,Y,0),Cell)) { Test->AddError(TEXT("原地图地面不完整")); return true; }
				Ground.Cells.Add(Cell);
			}
			OriginalItems=Board->PrototypeBackpack->GetItems();
			OriginalView=Controller->GetViewTarget();
			Camera=World->SpawnActor<ACameraActor>();
			if (!Test->TestNotNull(TEXT("测试观察相机"),Camera)) { return true; }
			Controller->SetViewTarget(Camera);
			Center=Board->GridOrigin+FIntVector(2,2,0);
			return false;
		}
		/** 每次模拟按下和抬起，后续帧由引擎PlayerInput分发，不能直接调用私有取点函数。 */
		void Press(FKey Key)
		{
			// 与项目现有巡逻输入测试采用相同的键鼠事件入口，保留引擎设备过滤和输入栈。
			Controller->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,Key,IE_Pressed,FPlatformTime::Cycles64()));
			Controller->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,Key,IE_Released,FPlatformTime::Cycles64()));
			// Editor 的潜伏命令可能早于 PIE 的 PlayerInput。留出完整一帧派发事件，才检查状态或移动相机。
			NextInputCheckFrame = GFrameCounter + 2;
		}
		void AimGround(FIntVector Cell)
		{
			Camera->SetActorLocation(Board->GetGridWorldLocation(Cell)+FVector(0,0,1500));
			Camera->SetActorRotation(FRotator(-90,0,0));
		}
		void AimBuilding(ACastleBuildingBase* Building,bool bRoof)
		{
			// 两种观察位置均为显式测试输入；楼内相机位于屋顶下方，不改变任何组件碰撞。
			Camera->SetActorLocation(Building->GetActorLocation()+FVector(120,120,bRoof?1200:180));
			Camera->SetActorRotation(FRotator(-90,0,0));
		}
		void BeginScenario()
		{
			Nav->ClearSharedRoute(); AimBuilding(Cannon,Scenario==0); Stage=10;
		}
		/** 复用真实视口射线，独立记录地块原点或建筑本层锚点期望，最后由键鼠入口提交后核对。 */
		bool Capture(ACastleBuildingBase* Expected,FHitResult& Hit,FVector& ExpectedPoint,bool& bProjected)
		{
			int32 Width,Height; Controller->GetViewportSize(Width,Height);
			FVector Origin,Direction;
			if (!Controller->DeprojectScreenPositionToWorld(Width*0.5,Height*0.5,Origin,Direction)) { Test->AddError(TEXT("视口中心无法反投影")); return false; }
			FCollisionQueryParams Params(SCENE_QUERY_STAT(W1PatrolInput),true);
			Params.AddIgnoredActor(Route); if (Controller->GetPawn()) { Params.AddIgnoredActor(Controller->GetPawn()); }
			const bool bHit=World->LineTraceSingleByChannel(Hit,Origin,Origin+Direction*100000,ECC_Visibility,Params);
			Test->AddInfo(FString::Printf(TEXT("W1_INPUT_RAY Origin=%s Direction=%s Hit=%d Actor=%s Component=%s Point=%s Normal=%s"),
				*Origin.ToString(),*Direction.ToString(),bHit,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString()));
			// 楼内视线在正式资产中可能先命中棋盘TileMesh；忠实记录原生Visibility结果，
			// 不把地块关闭或替换成隐藏功能楼板。只校验射线仍位于目标建筑内的指定XY测试点。
			const FVector ExpectedXY=Expected->GetActorLocation()+FVector(120,120,0);
			if (!Test->TestTrue(TEXT("玩家视线在指定XY命中可放点顶面"),bHit&&Hit.ImpactNormal.Z>=0.5
				&&FVector::Dist2D(Hit.ImpactPoint,ExpectedXY)<0.1)) { return false; }
			FNavLocation Projected;
			bProjected=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)->ProjectPointToNavigation(Hit.ImpactPoint,Projected,Nav->ProjectionExtent);
			Test->AddInfo(FString::Printf(TEXT("W1_INPUT_HIT Family=%s Scenario=%s ExpectedBuilding=%s HitActor=%s Component=%s Point=%s Functional=%d Project=%d Projected=%s"),
				Families[Family],Scenarios[Scenario],*Expected->GetClass()->GetName(),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactPoint.ToString(),
				UBuildingGameplayRootComponent::IsInGameplayHierarchy(Hit.GetComponent()),bProjected,*Projected.Location.ToString()));
			if (Cast<ACastleBuildingBase>(Hit.GetActor()))
			{
				FCastleGridCursorTarget Target;
				if (!Test->TestTrue(TEXT("建筑射线可由正式棋盘解析命中格"),Board->TryResolveGridCursorTarget(Origin,Origin+Direction*100000,Target) && Target.Surface == ECastleGridCursorSurface::Building)) { return false; }
				const ACastleBuildingBase* FloorOwner = Board->GetBuildingAt(Target.Cell);
				if (!Test->TestNotNull(TEXT("射线命中格有实际建筑"), FloorOwner)) { return false; }
				const double StandingHeight = FloorOwner->HachimisLayout ? FloorOwner->HachimisLayout->StandingHeight : 0.0;
				ExpectedPoint = Board->GetGridWorldLocation(Target.Cell)
					+ Board->GetActorTransform().TransformVector(FVector(0, 0, StandingHeight - Board->LayerHeight * 0.5));
			}
			else
			{
				const auto* Tile = Cast<ACastleGroundTileActor>(Hit.GetActor());
				if (!Test->TestNotNull(TEXT("非建筑取点必须是原有地块命中"), Tile)) { return false; }
				ExpectedPoint = Board->GetGridWorldLocation(Tile->GetGroundCell().Coordinate);
				ExpectedPoint.Z = Hit.ImpactPoint.Z;
			}
			return true;
		}
		bool ClearBackpack()
		{
			FText Error; FCastleBuildingDescriptor Card;
			while (Board->PrototypeBackpack->GetItemCount()>0)
			{
				if (!Board->PrototypeBackpack->TryRemoveItemAt(0,Card,Error)) { Test->AddError(Error.ToString()); return false; }
			}
			return true;
		}
		bool Prepare()
		{
			FText Error; FCastleBuildingDescriptor A,B;
			auto* CannonDef=LoadObject<UCastleBuildingDefinition>(nullptr,TEXT("/Game/MovingCastle/Data/Buildings/DA_CannonTower.DA_CannonTower"));
			auto* NeighborDef=LoadObject<UCastleBuildingDefinition>(nullptr,*FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"),Families[Family],Families[Family]));
			if (!Test->TestNotNull(TEXT("正式炮塔定义"),CannonDef)||!Test->TestNotNull(TEXT("正式邻楼定义"),NeighborDef)) { Cleanup(); return true; }
			if (!CannonDef->CreateDescriptor(1,ECastleTileColor::Red,3,A,Error)||!NeighborDef->CreateDescriptor(1,ECastleTileColor::Red,4,B,Error)) { Test->AddError(Error.ToString()); Cleanup(); return true; }
			auto InputGround=Ground;
			for (auto& Cell:InputGround.Cells)
			{
				if (Cell.Coordinate==Center) { Cell.Color=A.Color; Cell.Number=A.Number; }
				if (Cell.Coordinate==Center+FIntVector(1,0,0)) { Cell.Color=B.Color; Cell.Number=B.Number; }
			}
			if (!Board->ApplyGroundGenerationResult(InputGround,Error)||!ClearBackpack()) { Test->AddError(Error.ToString()); Cleanup(); return true; }
			int32 Index;
			if (!Board->PrototypeBackpack->TryAddItem(A,Index,Error)||!Board->PrototypeBackpack->TryAddItem(B,Index,Error)) { Test->AddError(Error.ToString()); Cleanup(); return true; }
			AimGround(Center); Stage=1; return false;
		}
		bool RemoveBuildings()
		{
			FText Error;
			for (auto* B:Board->GetPlacedBuildings()) { if (!Board->RemoveBuilding(B,Error)) { Test->AddError(Error.ToString()); return false; } }
			Cannon=Neighbor=nullptr; return true;
		}
		void Cleanup()
		{
			// 失败发生在草稿中时也通过原有P绑定退出，不把输入消费状态遗留给后续测试。
			if (Route&&Route->IsEditing()) { Press(EKeys::P); }
			if (Controller&&OriginalView.IsValid()) { Controller->SetViewTarget(OriginalView.Get()); }
			if (IsValid(Camera)) { Camera->Destroy(); }
			if (Nav) { Nav->ClearSharedRoute(); }
			if (Route) { Route->LocalWaypoints.Reset(); }
			if (!Board||!Camera) { return; }
			if (!RemoveBuildings()||!ClearBackpack()) { return; }
			FText Error; int32 Index;
			for (const auto& Card:OriginalItems) { if (!Board->PrototypeBackpack->TryAddItem(Card,Index,Error)) { Test->AddError(Error.ToString()); return; } }
			if (!Board->ApplyGroundGenerationResult(Ground,Error)) { Test->AddError(Error.ToString()); }
		}
		FAutomationTestBase* Test;
		UWorld* World=nullptr;
		ACastleBoard* Board=nullptr;
		ACastlePlayerController* Controller=nullptr;
		ACastlePatrolRoute* Route=nullptr;
		UCastleNavigationSubsystem* Nav=nullptr;
		ACameraActor* Camera=nullptr;
		ACastleBuildingBase* Cannon=nullptr;
		ACastleBuildingBase* Neighbor=nullptr;
		TWeakObjectPtr<AActor> OriginalView;
		TArray<FCastleBuildingDescriptor> OriginalItems;
		FCastleGroundGenerationResult Ground;
		FIntVector Center;
		FHitResult FirstHit,SecondHit;
		/** 从原始射线和 D26 公式得到的独立期望，不从导航请求结果反推。 */
		FVector FirstExpected,SecondExpected;
		bool FirstProjected=false,SecondProjected=false;
		int32 Family=0,Scenario=0,Stage=0;
		double Deadline=FPlatformTime::Seconds()+120.0;
		double InitializationDeadline=FPlatformTime::Seconds()+20.0;
		uint64 NextInputCheckFrame=0;
		const TCHAR* Families[4]={TEXT("ArcherTower"),TEXT("Farm"),TEXT("ShieldTower"),TEXT("NormalRoom")};
		const TCHAR* Scenarios[3]={TEXT("CannonRoof_NeighborInside"),TEXT("BothInside"),TEXT("CannonInside_NeighborRoof")};
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCannonPlayerInputTest,"MovingCastle.Castle.PCG.CannonPlayerInput",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FCastleCannonPlayerInputTest::RunTest(const FString& Parameters)
{
	// 编辑器单独运行时显式加载TestMap1并启动PIE；已有Game/PIE世界时使用调用者加载的地图。
	// TestMap1 提供本测试所需的正式棋盘及导航输入；FCommand仍明确拒绝缺失的棋盘或视口。
	bool bHasGameWorld=false;
	for (const auto& Context:GEngine->GetWorldContexts())
	{
		bHasGameWorld |= Context.WorldType==EWorldType::Game || Context.WorldType==EWorldType::PIE;
	}
	if (GIsEditor&&!bHasGameWorld&&!AutomationOpenMap(TEXT("/Game/MovingCastle/Maps/Test/TestMap1"),true))
	{
		AddError(TEXT("无法为真实输入测试加载TestMap1 PIE。")); return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(CastleCannonPlayerInputTests::FCommand(this)); return true;
}
#endif
