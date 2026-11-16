#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Castle/Actors/CastleBoard.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/Data/CastleBuildingDefinition.h"
#include "Components/Animation/BuildingMergeComponent.h"
#include "Units/Navigation/CastleNavigationSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace CastleCannonExpandedTests
{
	/** 每例都保留正式放置、自动合成、PCG 和 Recast；不向测试世界添加替代通路。 */
	class FCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(FString::Printf(TEXT("扩大邻接复现超时：Case=%d Stage=%d"), Case, Stage));
				Cleanup(); return true;
			}
			if (!Board)
			{
				for (const auto& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
					{
						for (TActorIterator<ACastleBoard> It(Context.World()); It; ++It) { Board = *It; break; }
					}
				}
				if (!Test->TestNotNull(TEXT("扩大复现要求正式空棋盘"), Board)) { return true; }
				if (!Test->TestEqual(TEXT("不删除用户已有建筑来准备夹具"), Board->GetPlacedBuildingCount(), 0)) { return true; }
				Nav = Board->GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
				if (!Test->TestNotNull(TEXT("真实导航子系统"), Nav)) { return true; }
				for (int32 X=0; X<Board->GridSize.X; ++X) for (int32 Y=0; Y<Board->GridSize.Y; ++Y)
				{
					FCastleGroundCell Cell;
					if (!Test->TestTrue(TEXT("保存原地面"), Board->TryGetGroundCell(Board->GridOrigin+FIntVector(X,Y,0), Cell))) { return true; }
					Ground.Cells.Add(Cell);
				}
			}
			if (!Nav->IsNavigationReady()) { return false; }
			if (Stage == 0) { return Place(); }
			const auto Buildings = Board->GetPlacedBuildings();
			// 高级邻楼由逐格一级放置自动合成，不能在合成尚未结束时读取过期 Actor。
			if (Buildings.Num() != 2) { return false; }
			ACastleBuildingBase* Neighbor = nullptr;
			for (auto* Building : Buildings)
			{
				if (!Building->bHasCompletedPlacement || Building->BuildingMerge->bIsPlaying) { return false; }
				if (Building != Cannon) { Neighbor = Building; }
			}
			if (!Test->TestNotNull(Label()+TEXT(" 找到邻楼"), Neighbor)) { Cleanup(); return true; }
			if (!Test->TestEqual(Label()+TEXT(" 合成等级"), Neighbor->GetBuildingLevel(), Level())) { Cleanup(); return true; }
			const auto* Entry = Definition->FindLevel(Level());
			const UClass* ExpectedClass = IsVertical() ? Entry->VerticalBuildingClass.Get() : Entry->BuildingClass.Get();
			Test->TestTrue(Label()+TEXT(" 真实水平/垂直蓝图类"), Neighbor->GetClass() == ExpectedClass);
			// 使用接触格底层中心作为路线端点，避免高级邻楼中心落在两个楼层之间。
			FVector A = Board->GetGridWorldLocation(CannonCell);
			FVector B = Board->GetGridWorldLocation(ContactCell);
			const FVector HalfLayer=Board->GetActorTransform().TransformVector(FVector(0,0,Board->LayerHeight*0.5));
			A-=HalfLayer; B-=HalfLayer;
			FText Error;
			if (!Test->TestTrue(Label()+TEXT(" 接收路线"), Nav->RequestSharedPatrolRoute({A,B}, Error)))
			{ Test->AddError(Error.ToString()); Cleanup(); return true; }
			const auto Route = Nav->AcquireSharedPath();
			const bool bLoop = Route && Route->bValid && Route->bClosedLoop;
			Test->AddInfo(FString::Printf(TEXT("W1_EXPANDED %s Class=%s ValidLoop=%d LastFailure=%s"),
				*Label(), *Neighbor->GetClass()->GetName(), bLoop, *Nav->LastFailure.ToString()));
			Test->TestTrue(Label()+TEXT(" 跨界共享闭环"), bLoop);
			Nav->ClearSharedRoute();
			if (!Remove()) { return true; }
			++Case; Stage=0; Deadline=FPlatformTime::Seconds()+120.0;
			if (Case == 104) { Cleanup(); return true; }
			return false;
		}
	private:
		/** 前8例为炮塔互贴；其余按三族、两个等级、两种形状、四向和两种匹配属性展开。 */
		int32 Direction() const { return Case<8 ? Case/2 : ((Case-8)/2)%4; }
		int32 Level() const { return Case<8 ? 1 : 2+((Case-8)/16)%2; }
		bool IsVertical() const { return Case>=8 && ((Case-8)/8)%2 == 1; }
		const TCHAR* Family() const { return Case<8 ? TEXT("CannonTower") : Families[(Case-8)/32]; }
		FString Label() const
		{
			return FString::Printf(TEXT("%s/LV%d/%s/%s/%s"), Family(), Level(), IsVertical()?TEXT("Vertical"):TEXT("Horizontal"),
				Directions[Direction()], Case%2?TEXT("SameNumber"):TEXT("SameColor"));
		}
		bool Place()
		{
			auto* CannonDefinition=LoadObject<UCastleBuildingDefinition>(nullptr,TEXT("/Game/MovingCastle/Data/Buildings/DA_CannonTower.DA_CannonTower"));
			Definition=LoadObject<UCastleBuildingDefinition>(nullptr,*FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"),Family(),Family()));
			if (!Test->TestNotNull(TEXT("炮塔定义"),CannonDefinition) || !Test->TestNotNull(Label(),Definition)) { Cleanup(); return true; }
			const FIntVector Shape = Level()==1 ? FIntVector(1,1,1) : Level()==2
				? (IsVertical()?FIntVector(1,1,2):FIntVector(2,1,1)) : (IsVertical()?FIntVector(2,1,2):FIntVector(2,2,1));
			CannonCell=Board->GridOrigin+FIntVector(3,3,0);
			const FIntVector Offset=Offsets[Direction()];
			FIntVector Minimum=CannonCell+Offset;
			if (Offset.X<0) { Minimum.X=CannonCell.X-Shape.X; }
			if (Offset.Y<0) { Minimum.Y=CannonCell.Y-Shape.Y; }
			ContactCell=CannonCell+Offset;
			FCastleBuildingDescriptor CardA,CardB; FText Error;
			if (!CannonDefinition->CreateDescriptor(1,ECastleTileColor::Red,3,CardA,Error)
				|| !Definition->CreateDescriptor(1,Case%2?ECastleTileColor::Blue:ECastleTileColor::Red,Case%2?3:4,CardB,Error))
			{ Test->AddError(Error.ToString()); Cleanup(); return true; }
			auto InputGround=Ground;
			for (auto& Cell:InputGround.Cells)
			{
				if (Cell.Coordinate==CannonCell) { Cell.Color=CardA.Color; Cell.Number=CardA.Number; }
				if (Cell.Coordinate.X>=Minimum.X && Cell.Coordinate.X<Minimum.X+Shape.X
					&& Cell.Coordinate.Y>=Minimum.Y && Cell.Coordinate.Y<Minimum.Y+Shape.Y)
				{ Cell.Color=CardB.Color; Cell.Number=CardB.Number; }
			}
			if (!Board->ApplyGroundGenerationResult(InputGround,Error)) { Test->AddError(Error.ToString()); Cleanup(); return true; }
			Cannon=Board->PlaceBuilding(CannonCell,CardA,true,Error);
			if (!Test->TestNotNull(Label()+TEXT(" 一级炮塔"),Cannon)) { Test->AddError(Error.ToString()); Cleanup(); return true; }
			// 从下到上生成支撑；合成仍由正式飞入完成事件驱动，不手工拼装高级蓝图。
			for (int32 Z=0;Z<Shape.Z;++Z) for (int32 Y=0;Y<Shape.Y;++Y) for (int32 X=0;X<Shape.X;++X)
			{
				if (!Test->TestNotNull(Label()+TEXT(" 邻楼逐格放置"),Board->PlaceBuilding(Minimum+FIntVector(X,Y,Z),CardB,true,Error)))
				{ Test->AddError(Error.ToString()); Cleanup(); return true; }
			}
			Stage=1; return false;
		}
		/** 只撤回本夹具在原始空棋盘生成的建筑，失败立即报错，不继续下一例。 */
		bool Remove()
		{
			FText Error;
			for (auto* Building:Board->GetPlacedBuildings())
			{
				if (!Board->RemoveBuilding(Building,Error)) { Test->AddError(Error.ToString()); return false; }
			}
			Cannon=nullptr; return true;
		}
		void Cleanup()
		{
			if (Nav) { Nav->ClearSharedRoute(); }
			if (!Board || !Remove()) { return; }
			FText Error;
			if (!Ground.Cells.IsEmpty() && !Board->ApplyGroundGenerationResult(Ground,Error)) { Test->AddError(Error.ToString()); }
		}
		FAutomationTestBase* Test;
		ACastleBoard* Board=nullptr;
		ACastleBuildingBase* Cannon=nullptr;
		UCastleBuildingDefinition* Definition=nullptr;
		UCastleNavigationSubsystem* Nav=nullptr;
		FCastleGroundGenerationResult Ground;
		FIntVector CannonCell,ContactCell;
		int32 Case=0,Stage=0;
		double Deadline=FPlatformTime::Seconds()+120.0;
		const TCHAR* Families[3]={TEXT("ArcherTower"),TEXT("Farm"),TEXT("ShieldTower")};
		const TCHAR* Directions[4]={TEXT("+X"),TEXT("-X"),TEXT("+Y"),TEXT("-Y")};
		const FIntVector Offsets[4]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCannonExpandedTest,"MovingCastle.Castle.PCG.CannonExpandedAdjacency",
	EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FCastleCannonExpandedTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleCannonExpandedTests::FCommand(this)); return true;
}
#endif
