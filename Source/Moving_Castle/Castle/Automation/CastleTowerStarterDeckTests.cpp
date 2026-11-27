// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/Actors/CastleBoard.h"
#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Castle/Components/CastleBackpackComponent.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleTowerStarterDeckTest, "MovingCastle.Placement.TowerStarterDeck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleTowerStarterDeckTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("用于生成实际侧格的测试世界"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// 固定种子逐个重建真实侧格和正式发牌入口。最后再重复首个种子，核对牌面、类型及发放顺序。
	TSet<FString> Hands;
	FString FirstHand;
	constexpr int32 SeedCount = 256;
	for (int32 Sample = 0; Sample <= SeedCount; ++Sample)
	{
		const int32 Seed = Sample % SeedCount;
		ACastleBoard* Board = World->SpawnActor<ACastleBoard>();
		ATargetPoint* Tower = World->SpawnActor<ATargetPoint>();
		if (!TestNotNull(TEXT("棋盘"), Board) || !TestNotNull(TEXT("侧格锚点"), Tower)) { return false; }
		ON_SCOPE_EXIT { World->DestroyActor(Tower); World->DestroyActor(Board); };

		CastleBuildingTests::ConfigureBoard(Board);
		Board->PlacementSurfaceMode = ECastlePlacementSurfaceMode::MainTowerSides;
		Board->GridSize.Z = 1;
		Board->bUseFixedTowerSideSeed = true;
		Board->FixedTowerSideSeed = Seed;
		Board->MainTowerActor = Tower;
		auto* Grid = NewObject<UCastleTowerSideGridComponent>(Tower);
		Tower->AddInstanceComponent(Grid);
		Grid->RegisterComponent();
		Board->TowerSideGrid = Grid;
		FText Reason;
		const FIntVector Base = Board->GetBoardCenterCoordinate();
		if (!TestTrue(TEXT("按样本种子生成第一层四面"), Grid->Initialize(Base, 1, 500.0f, 515.0f, 560.0f, 260.0f, Seed, Reason)))
		{
			AddError(Reason.ToString());
			return false;
		}
		Board->InitializePrototypeDeck();
		const TArray<FCastleBuildingDescriptor> Hand = Board->PrototypeBackpack->GetItems();
		const FString Label = FString::Printf(TEXT("样本 %d "), Seed);
		if (!TestEqual(Label + TEXT("主栏整批发五张"), Hand.Num(), 5)
			|| !TestEqual(Label + TEXT("临时栏为空"), Board->PrototypeBackpack->GetTempItemCount(), 0))
		{
			return false;
		}
		FString Signature;
		bool bHasNormalRoom = false;
		for (const FCastleBuildingDescriptor& Card : Hand)
		{
			if (!TestTrue(Label + TEXT("有效一级卡"), Card.Level == 1 && Card.Validate(Reason))) { return false; }
			const ECastleBuildingType Type = Card.GetBuildingType();
			if (!TestTrue(Label + TEXT("每张均为可通行建筑"), Type == ECastleBuildingType::NormalRoom
				|| Type == ECastleBuildingType::ArcherTower || Type == ECastleBuildingType::CannonTower || Type == ECastleBuildingType::Farm))
			{
				return false;
			}
			bHasNormalRoom |= Type == ECastleBuildingType::NormalRoom;
			Signature += FString::Printf(TEXT("%d,%d,%d;"), static_cast<int32>(Type), static_cast<int32>(Card.Color), Card.Number);
		}
		if (!TestTrue(Label + TEXT("至少有一张普通房"), bHasNormalRoom)) { return false; }
		Hands.Add(Signature);
		if (Sample == 0) { FirstHand = Signature; }
		if (Sample == SeedCount) { TestEqual(TEXT("相同种子按原顺序发放相同手牌"), Signature, FirstHand); }

		const auto* Front = Grid->FindCell(Base, ECastleGridFace::PositiveX);
		const auto* Back = Grid->FindCell(Base, ECastleGridFace::NegativeX);
		const auto* Left = Grid->FindCell(Base, ECastleGridFace::NegativeY);
		const auto* Right = Grid->FindCell(Base, ECastleGridFace::PositiveY);
		if (!TestTrue(Label + TEXT("四个地图侧面均可查到"), Front && Back && Left && Right)) { return false; }
		const auto Matches = [](const auto& A, const auto& B) { return A.Color == B.Color || A.Number == B.Number; };
		// 对已打乱的五牌做独立深搜，每步消耗一张，绕行中心匹配任一真实侧格。
		const auto Search = [&](auto&& Self, const FCastleBuildingDescriptor& Previous, int32 Used, int32 Depth) -> bool
		{
			if (Depth == Hand.Num()) { return Matches(Previous, *Back); }
			for (int32 Index = 0; Index < Hand.Num(); ++Index)
			{
				const int32 Mask = 1 << Index;
				const FCastleBuildingDescriptor& Card = Hand[Index];
				if ((Used & Mask) == 0 && Matches(Previous, Card)
					&& (Depth != 2 || Matches(Card, *Left) || Matches(Card, *Right))
					&& Self(Self, Card, Used | Mask, Depth + 1))
				{
					return true;
				}
			}
			return false;
		};
		bool bSolvable = false;
		for (int32 Index = 0; Index < Hand.Num() && !bSolvable; ++Index)
		{
			bSolvable = Matches(*Front, Hand[Index]) && Search(Search, Hand[Index], 1 << Index, 1);
		}
		if (!TestTrue(Label + TEXT("实际背包可用五格连通正门与后门"), bSolvable)) { return false; }
	}
	return TestTrue(TEXT("不同侧格种子得到多种开局手牌"), Hands.Num() > 200);
}

#endif
