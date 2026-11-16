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

namespace CastleCannonAdjacencyTests
{
	/** 使用正式地图、正式定义和放置动画，逐例验证实际 Recast 共享闭环；不补测试楼板或放宽投影。 */
	class FCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FCommand(FAutomationTestBase* InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(FString::Printf(TEXT("一级炮塔相邻导航超时，案例 %d，阶段 %d。"), Case, Stage));
				Cleanup(); return true;
			}
			if (!Board)
			{
				// 入口显式要求 TestMap1 的空棋盘；不能清除用户建筑来制造通过条件。
				for (const auto& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::Game)
					{
						for (TActorIterator<ACastleBoard> It(Context.World()); It; ++It) { Board = *It; break; }
					}
				}
				if (!Test->TestNotNull(TEXT("TestMap1 正式棋盘"), Board)) { return true; }
				if (!Test->TestEqual(TEXT("回归夹具要求初始无放置建筑"), Board->GetPlacedBuildingCount(), 0)) { return true; }
				Nav = Board->GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
				if (!Test->TestNotNull(TEXT("正式导航子系统"), Nav)) { return true; }
				for (int32 X = 0; X < Board->GridSize.X; ++X) for (int32 Y = 0; Y < Board->GridSize.Y; ++Y)
				{
					FCastleGroundCell Cell;
					if (!Test->TestTrue(TEXT("保存原始地面输入"), Board->TryGetGroundCell(Board->GridOrigin + FIntVector(X,Y,0), Cell))) { return true; }
					OriginalGround.Cells.Add(Cell);
				}
			}
			if (!Nav->IsNavigationReady()) { return false; }
			if (Stage == 0) { return Place(); }
			if (Stage == 1)
			{
				if (!First->bHasCompletedPlacement || !Second->bHasCompletedPlacement
					|| First->BuildingMerge->bIsPlaying || Second->BuildingMerge->bIsPlaying) { return false; }
				// 两个建筑各取自身底层中心，既不从邻楼吸附，也不指定绕行路线。
				FText Error;
				if (Mode() == 2) { Test->AddExpectedMessage(TEXT("NPC 导航："), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false); }
				if (!Test->TestTrue(Label() + TEXT(" 接收共享巡逻请求"), Nav->RequestSharedPatrolRoute({First->GetActorLocation(), Second->GetActorLocation()}, Error)))
				{ Test->AddError(Error.ToString()); Cleanup(); return true; }
				const auto Route = Nav->AcquireSharedPath();
				const bool bValidLoop = Route && Route->bValid && Route->bClosedLoop;
				Test->AddInfo(FString::Printf(TEXT("W1_CASE %s ValidLoop=%d LastFailure=%s"), *Label(), bValidLoop, *Nav->LastFailure.ToString()));
				if (Mode() == 2)
				{
					Test->TestFalse(Label() + TEXT(" 异色异数字必须封闭"), bValidLoop);
					Test->TestFalse(Label() + TEXT(" 失败必须说明原因"), Nav->LastFailure.IsEmpty());
				}
				else { Test->TestTrue(Label() + TEXT(" 跨交界路线必须为有效闭环"), bValidLoop); }
				Nav->ClearSharedRoute();
				if (!Remove()) { return true; }
				++Case; Stage = 0; Deadline = FPlatformTime::Seconds() + 90.0;
				if (Case == 60) { Cleanup(); return true; }
			}
			return false;
		}
	private:
		/** 每族四方向乘三种属性，最后十二例为箭塔/农田对照。 */
		int32 Mode() const { return Case % 3; }
		FString Label() const
		{
			return FString::Printf(TEXT("%s/%s/%s"), Families[Case / 12], Directions[(Case / 3) % 4], Modes[Mode()]);
		}
		bool Place()
		{
			const bool bControl = Case / 12 == 4;
			const TCHAR* FirstFamily = bControl ? TEXT("ArcherTower") : TEXT("CannonTower");
			const TCHAR* SecondFamily = bControl ? TEXT("Farm") : Families[Case / 12];
			auto* A = LoadObject<UCastleBuildingDefinition>(nullptr, *FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), FirstFamily, FirstFamily));
			auto* B = LoadObject<UCastleBuildingDefinition>(nullptr, *FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), SecondFamily, SecondFamily));
			if (!Test->TestNotNull(Label() + TEXT(" 首楼定义"), A) || !Test->TestNotNull(Label() + TEXT(" 邻楼定义"), B)) { Cleanup(); return true; }
			const FIntVector Center = Board->GridOrigin + FIntVector(2,2,0);
			const FIntVector Neighbor = Center + Offsets[(Case / 3) % 4];
			FCastleBuildingDescriptor CardA, CardB; FText Error;
			if (!A->CreateDescriptor(1, ECastleTileColor::Red, 3, CardA, Error)
				|| !B->CreateDescriptor(1, Mode() == 0 ? ECastleTileColor::Red : ECastleTileColor::Blue, Mode() == 1 ? 3 : 4, CardB, Error))
			{ Test->AddError(Error.ToString()); Cleanup(); return true; }
			// 只改测试世界的地面属性，使放置合法；属性不同的封闭案例也确实进入 PCG/Recast。
			auto Ground = OriginalGround;
			for (auto& Cell : Ground.Cells)
			{
				if (Cell.Coordinate == Center) { Cell.Color = CardA.Color; Cell.Number = CardA.Number; }
				if (Cell.Coordinate == Neighbor) { Cell.Color = CardB.Color; Cell.Number = CardB.Number; }
			}
			if (!Board->ApplyGroundGenerationResult(Ground, Error)) { Test->AddError(Error.ToString()); Cleanup(); return true; }
			First = Board->PlaceBuilding(Center, CardA, false, Error);
			if (!Test->TestNotNull(Label() + TEXT(" 放置首楼"), First)) { Test->AddError(Error.ToString()); Cleanup(); return true; }
			Second = Board->PlaceBuilding(Neighbor, CardB, false, Error);
			if (!Test->TestNotNull(Label() + TEXT(" 放置邻楼"), Second)) { Test->AddError(Error.ToString()); Cleanup(); return true; }
			Stage = 1; return false;
		}
		/** 每例撤回自己的两座楼；移除失败立即作为夹具失败，不继续污染下例。 */
		bool Remove()
		{
			FText Error;
			if (IsValid(Second) && !Board->RemoveBuilding(Second, Error)) { Test->AddError(Error.ToString()); return false; }
			Second = nullptr;
			if (IsValid(First) && !Board->RemoveBuilding(First, Error)) { Test->AddError(Error.ToString()); return false; }
			First = nullptr; return true;
		}
		void Cleanup()
		{
			if (Nav) { Nav->ClearSharedRoute(); }
			if (!Board) { return; }
			if (!Remove()) { return; }
			FText Error;
			if (!OriginalGround.Cells.IsEmpty() && !Board->ApplyGroundGenerationResult(OriginalGround, Error)) { Test->AddError(Error.ToString()); }
		}
		FAutomationTestBase* Test;
		ACastleBoard* Board = nullptr;
		UCastleNavigationSubsystem* Nav = nullptr;
		ACastleBuildingBase* First = nullptr;
		ACastleBuildingBase* Second = nullptr;
		FCastleGroundGenerationResult OriginalGround;
		int32 Case = 0, Stage = 0;
		double Deadline = FPlatformTime::Seconds() + 90.0;
		const TCHAR* Families[5] = {TEXT("ArcherTower"), TEXT("Farm"), TEXT("ShieldTower"), TEXT("NormalRoom"), TEXT("ArcherFarmControl")};
		const TCHAR* Directions[4] = {TEXT("+X"), TEXT("-X"), TEXT("+Y"), TEXT("-Y")};
		const TCHAR* Modes[3] = {TEXT("SameColor"), TEXT("SameNumber"), TEXT("Closed")};
		const FIntVector Offsets[4] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}};
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleCannonAdjacencyNavigationTest, "MovingCastle.Castle.PCG.CannonLevelOneAdjacency",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCastleCannonAdjacencyNavigationTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleCannonAdjacencyTests::FCommand(this));
	return true;
}
#endif
