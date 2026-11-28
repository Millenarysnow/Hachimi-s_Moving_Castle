#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Items/CastleItemGrantLibrary.h"
#include "Castle/Items/CastlePendingRoomReward.h"
#include "Castle/Items/CastleRoomGroupDataAsset.h"
#include "Castle/Items/CastleRoomTraitComponent.h"
#include "Castle/PCG/CastleBuildingPCGComponent.h"
#include "Castle/SkyIslands/CastleSkyIslandTestFixture.h"
#include "Castle/SkyIslands/Gamble/CastleGambleSkyIslandDataAsset.h"
#include "CastleBackpackComponent.h"
#include "CastleBackpackTestObserver.h"
#include "CastleMainTowerComponent.h"
#include "CastleMassTypes.h"
#include "CastleNavigationSubsystem.h"
#include "CastlePlayerController.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/DefaultPawn.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Mass/CastlePatrolMovement.h"
#include "Misc/AutomationTest.h"
#include "Castle/Automation/CastleTowerOverviewTestUtilities.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

namespace CastleRoomGroupTests
{
	using B = ECastleBuildingType;
	using C = ECastleTileColor;
	using T = ECastleRoomTrait;
	using E = ECastleItemRarity;

	struct FGroupCase
	{
		const TCHAR* Name;
		E Rarity;
		int32 Price;
		int32 Cards;
	};

	// 期望来自房间组目录。按完整对象路径加载正式配置，保留独立的结果断言。
	static constexpr FGroupCase Cases[] = {
		{TEXT("01_RandomSupply"), E::Common, 48, 3}, {TEXT("02_WangcaiPatchBag"), E::Common, 40, 2},
		{TEXT("03_ShieldBehindField"), E::Common, 55, 2}, {TEXT("04_CustomNormalRoom"), E::Common, 36, 1},
		{TEXT("05_TwinRooms"), E::Common, 44, 2}, {TEXT("06_TopSupply"), E::Uncommon, 86, 1},
		{TEXT("07_TripleThreeArchers"), E::Uncommon, 105, 3}, {TEXT("08_LuckyCannons"), E::Uncommon, 96, 2},
		{TEXT("09_SequentialFarms"), E::Uncommon, 105, 3}, {TEXT("10_Highway"), E::Uncommon, 115, 3},
		{TEXT("11_RedStorm"), E::Rare, 185, 6}, {TEXT("12_DoubleStairPuzzle"), E::Rare, 168, 4},
		{TEXT("13_TricolorAmmoSquad"), E::Rare, 178, 4}, {TEXT("14_ThreeWayToolbox"), E::Rare, 172, 4},
		{TEXT("15_BigNumberRoom"), E::Rare, 148, 1}, {TEXT("16_RandomFuncCustomRoom"), E::Rare, 156, 1},
		{TEXT("17_ThreeBlueEights"), E::Epic, 275, 3}, {TEXT("18_HiveChainBox"), E::Epic, 340, 6},
		{TEXT("19_FullyCustomRoom"), E::Epic, 245, 1}, {TEXT("20_Normalize"), E::Epic, 300, 6},
		{TEXT("21_PortalRoom"), E::Epic, 315, 1}, {TEXT("22_GreenLushRedWither"), E::Legendary, 520, 3},
		{TEXT("23_SelfOrbitCannon"), E::Legendary, 490, 1}, {TEXT("24_TowerBuyout"), E::Legendary, 650, 9},
		{TEXT("25_DirectDeploy"), E::Legendary, 620, 8}, {TEXT("26_ColorShift"), E::Legendary, 500, 0}
	};
	static constexpr B FunctionTypes[] = {B::ArcherTower, B::CannonTower, B::Farm, B::ShieldTower, B::BeeHive, B::BalloonTower};

	TArray<TObjectPtr<UCastleBuildingDefinition>> MakePool()
	{
		// 常用的四种塔和普通房使用原生模型夹具，蜂巢与空投读取各自的正式定义。
		return {
			CastleBuildingTests::MakeDefinition(B::NormalRoom), CastleBuildingTests::MakeDefinition(B::ArcherTower),
			CastleBuildingTests::MakeDefinition(B::CannonTower), CastleBuildingTests::MakeDefinition(B::Farm),
			CastleBuildingTests::MakeDefinition(B::ShieldTower),
			LoadObject<UCastleBuildingDefinition>(nullptr, TEXT("/Game/MovingCastle/Data/Buildings/DA_BeeHive.DA_BeeHive")),
			LoadObject<UCastleBuildingDefinition>(nullptr, TEXT("/Game/MovingCastle/Data/Buildings/DA_BalloonTower.DA_BalloonTower"))
		};
	}

	/** 用完整对象名访问当前编号，避免资产重命名或目录移除后仍通过测试。 */
	UCastleRoomGroupDataAsset* LoadGroup(const int32 Ordinal)
	{
		const TCHAR* Name = Cases[Ordinal - 1].Name;
		return LoadObject<UCastleRoomGroupDataAsset>(nullptr, *FString::Printf(
			TEXT("/Game/MovingCastle/Data/Items/RoomGroups/DA_RoomGroup_%s.DA_RoomGroup_%s"), Name, Name));
	}

	/** 比较完整牌面，包含特质字段；用来核对不应发生任何写入的准备与失败。 */
	bool EqualCards(const TConstArrayView<FCastleBuildingDescriptor> A, const TConstArrayView<FCastleBuildingDescriptor> B)
	{
		if (A.Num() != B.Num()) { return false; }
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!CastleRoomCards::HaveSameIdentity(A[Index], B[Index])) { return false; }
		}
		return true;
	}

	struct FWorldFixture
	{
		UWorld* World = nullptr;
		ACastleBoard* Board = nullptr;
		TUniquePtr<CastleBuildingTests::FScopedNativeBuildingModels> Models;

		bool Initialize(const TArray<TObjectPtr<UCastleBuildingDefinition>>& Pool, FText& Reason,
			const FTransform& Transform = FTransform::Identity, const bool bNeedsDynamicNavigation = false)
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
				.CreateNavigation(bNeedsDynamicNavigation).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!World) { Reason = INVTEXT("测试世界创建失败。"); return false; }
			// PCG 替换临时楼板和返塔均会销毁 Actor，为测试世界登记完整上下文并在收尾配对移除。
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			if (bNeedsDynamicNavigation)
			{
				// 沿用正式 PCG 回归用例的世界配置，允许上下层同色贯通时登记真实 Recast 梯子。
				auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				const auto* NavData = Navigation ? Navigation->GetDefaultNavDataInstance(FNavigationSystem::Create) : nullptr;
				if (!NavData || NavData->GetRuntimeGenerationMode() != ERuntimeGenerationType::Dynamic)
				{
					Reason = INVTEXT("高速公路测试需要正式 PCG 梯子可用的动态导航数据。");
					return false;
				}
			}
			Models = MakeUnique<CastleBuildingTests::FScopedNativeBuildingModels>(World);
			Board = World->SpawnActor<ACastleBoard>(ACastleBoard::StaticClass(), Transform);
			if (!Board) { Reason = INVTEXT("测试棋盘创建失败。"); return false; }
			Board->PlacementSurfaceMode = ECastlePlacementSurfaceMode::GroundBoard;
			Board->GridSize = FIntVector(5, 5, 4);
			Board->BuildingDefinitions = Pool;
			FCastleGroundGenerationResult Ground;
			for (int32 X = 0; X < Board->GridSize.X; ++X)
			{
				for (int32 Y = 0; Y < Board->GridSize.Y; ++Y)
				{
					auto& Cell = Ground.Cells.AddDefaulted_GetRef();
					Cell.Coordinate = Board->GridOrigin + FIntVector(X, Y, 0);
					Cell.Color = C::Red;
					Cell.Number = 1;
				}
			}
			return Board->ApplyGroundGenerationResult(Ground, Reason);
		}

		~FWorldFixture()
		{
			Models.Reset();
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		ACastleBuildingBase* Place(UCastleBuildingDefinition& Definition, const FIntVector& Offset, int32 Level, FText& Reason) const
		{
			FCastleBuildingDescriptor Card;
			if (!Definition.CreateDescriptor(Level, C::Red, 1, Card, Reason)) { return nullptr; }
			return PlaceCard(Card, Offset, Reason);
		}

		ACastleBuildingBase* PlaceCard(const FCastleBuildingDescriptor& Card, const FIntVector& Offset, FText& Reason,
			const bool bMerge = false) const
		{
			auto* Building = Board->PlaceBuilding(Board->GridOrigin + Offset, Card, bMerge, Reason);
			return CastleBuildingTests::CompletePlacement(Building) ? Building : nullptr;
		}

		UCastleDirectorSubsystem* ConfigureDirector(ACastlePlayerController*& Controller, UCastleDirectorConfigDataAsset*& Config) const
		{
			Controller = World->SpawnActor<ACastlePlayerController>();
			AActor* TowerActor = World->SpawnActor<AActor>();
			auto* Tower = NewObject<UCastleMainTowerComponent>(TowerActor);
			Tower->InitializeHealth();
			World->SpawnActor<ACastlePatrolRoute>();
			Config = NewObject<UCastleDirectorConfigDataAsset>();
			Config->EliteEveryNSteps = 100;
			auto* Director = World->GetSubsystem<UCastleDirectorSubsystem>();
			Director->ConfigureForAutomation(Config, Board, Controller, Tower);
			return Director;
		}
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FCastleRoomGroupRulesTest, "MovingCastle.Items.RoomGroups.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FCastleRoomGroupRulesTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (const auto& Entry : CastleRoomGroupTests::Cases)
	{
		OutBeautifiedNames.Add(Entry.Name);
		OutTestCommands.Add(FString(Entry.Name).Left(2));
	}
}

bool FCastleRoomGroupRulesTest::RunTest(const FString& Parameters)
{
	using namespace CastleRoomGroupTests;
	const int32 Ordinal = FCString::Atoi(*Parameters);
	if (Ordinal < 1 || Ordinal > UE_ARRAY_COUNT(Cases)) { return false; }
	const auto* Catalog = LoadObject<UCastleGambleSkyIslandDataAsset>(nullptr,
		TEXT("/Game/MovingCastle/Data/SkyIslands/DA_GambleSkyIsland.DA_GambleSkyIsland"));
	const auto* Group = LoadGroup(Ordinal);
	if (!TestNotNull(TEXT("正式赌博岛房间定义池"), Catalog)
		|| !TestNotNull(TEXT("正式房间组资产"), Group)
		|| !TestTrue(TEXT("资产被正式老虎机引用"), Catalog->RoomGroups.Contains(Group))) { return false; }
	const auto& Pool = Catalog->RoomDefinitions;
	const FGroupCase& Case = Cases[Ordinal - 1];
	TestEqual(TEXT("正式房间组稀有度"), Group->Rarity, Case.Rarity);
	TestEqual(TEXT("正式房间组售价"), Group->ShopPrice, Case.Price);
	const auto FindDefinition = [&Pool](const B Type) -> UCastleBuildingDefinition*
	{
		const auto* Found = Pool.FindByPredicate([Type](const UCastleBuildingDefinition* Definition)
		{
			return IsValid(Definition) && Definition->BuildingType == Type;
		});
		return Found ? Found->Get() : nullptr;
	};
	auto* Normal = FindDefinition(B::NormalRoom);
	auto* Archer = FindDefinition(B::ArcherTower);
	auto* Cannon = FindDefinition(B::CannonTower);
	if (!TestNotNull(TEXT("正式普通房定义"), Normal) || !TestNotNull(TEXT("正式箭塔定义"), Archer)
		|| !TestNotNull(TEXT("正式炮塔定义"), Cannon)) { return false; }

	auto* Backpack = NewObject<UCastleBackpackComponent>();
	FWorldFixture Scene;
	FText Reason;
	if (Ordinal == 24)
	{
		if (!TestTrue(TEXT("买断测试棋盘就绪"), Scene.Initialize(Pool, Reason))
			|| !TestNotNull(TEXT("一座占四格的炮塔"), Scene.Place(*Cannon, FIntVector::ZeroValue, 3, Reason))
			|| !TestNotNull(TEXT("第一座单格箭塔"), Scene.Place(*Archer, FIntVector(2, 0, 0), 1, Reason))
			|| !TestNotNull(TEXT("第二座单格箭塔"), Scene.Place(*Archer, FIntVector(3, 0, 0), 1, Reason)))
		{
			AddError(Reason.ToString());
			return false;
		}
	}

	int32 Slot = INDEX_NONE;
	if (Ordinal == 2 || Ordinal == 20 || Ordinal == 22 || Ordinal == 26)
	{
		// 首槽的蓝 6 炮塔同时证明补缝袋继承原牌面，以及归一化按数字而非容量生成。
		if (!Backpack->TryAddBuilding(Cannon, 1, C::Blue, 6, Slot, Reason)
			|| !Backpack->TryAddBuilding(Normal, 1, C::Red, 2, Slot, Reason))
		{
			AddError(Reason.ToString());
			return false;
		}
	}
	if (Ordinal == 20 || Ordinal == 22 || Ordinal == 26)
	{
		// 填满十槽后放两张红牌进入临时栏，显式检验染色边界及归一化的 FIFO 补位。
		while (!Backpack->IsFull())
		{
			if (!Backpack->TryAddBuilding(Normal, 1, C::Red, 1, Slot, Reason)) { AddError(Reason.ToString()); return false; }
		}
		FCastleBuildingDescriptor QueueCard;
		bool bOverflow = false;
		if (!Normal->CreateDescriptor(1, C::Red, 9, QueueCard, Reason)
			|| !Backpack->TryAddItemAllowOverflow(QueueCard, bOverflow, Slot, Reason)
			|| !Normal->CreateDescriptor(1, C::Red, 8, QueueCard, Reason)
			|| !Backpack->TryAddItemAllowOverflow(QueueCard, bOverflow, Slot, Reason))
		{
			AddError(Reason.ToString());
			return false;
		}
	}
	const auto Before = Backpack->GetItems();
	const auto TempBefore = Backpack->GetTempItems();
	auto* Observer = NewObject<UCastleBackpackTestObserver>();
	Observer->Backpack = Backpack;
	Backpack->OnBackpackChanged.AddDynamic(Observer, &UCastleBackpackTestObserver::Observe);
	FCastlePreparedRoomGroup Prepared;
	FRandomStream Random(147871);
	if (!TestTrue(TEXT("正式入口准备房间组"), UCastleItemGrantLibrary::TryPrepareRoomGroup(
		Group, Backpack, Scene.Board, Pool, Random, Prepared, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestTrue(TEXT("准备不写入两栏或广播"), EqualCards(Before, Backpack->GetItems())
		&& EqualCards(TempBefore, Backpack->GetTempItems()) && Observer->ObservedCounts.IsEmpty());

	if (Ordinal == 4 || Ordinal == 16 || Ordinal == 19)
	{
		FCastleWildcardRoomOptions Options;
		UCastlePendingRoomReward* Reward = Prepared.PendingReward;
		if (!TestNotNull(TEXT("返回现有房间选择器使用的奖励句柄"), Reward)
			|| !TestTrue(TEXT("首次打开选择器"), Reward->TryBeginClaim(Options, Reason))) { return false; }
		TestTrue(TEXT("弹窗牌面确认前零发牌"), Prepared.Cards.IsEmpty() && !Prepared.MainBarChange.IsSet());
		TestTrue(TEXT("可选四色和数字 1 至 6"), Options.Colors == TArray<C>(CastleTileColors::GetPlayable())
			&& Options.Numbers == TArray<int32>{1, 2, 3, 4, 5, 6});
		if (!TestEqual(TEXT("建筑候选数"), Options.Definitions.Num(), Ordinal == 19 ? 7 : 1)) { return false; }
		UCastleBuildingDefinition* Chosen = Options.Definitions.Last().Get();
		if (Ordinal == 4) { TestEqual(TEXT("普通房锁定定义"), Chosen, Normal); }
		if (Ordinal == 16) { TestTrue(TEXT("随机锁定为常规功能塔"), MakeArrayView(FunctionTypes).Contains(Chosen->BuildingType)); }
		if (Ordinal == 19)
		{
			TestTrue(TEXT("完全自定义开放全部七类"), Pool.Num() == 7
				&& !Pool.ContainsByPredicate([&Options](const auto& Definition) { return !Options.Definitions.Contains(Definition); }));
		}
		TestTrue(TEXT("取消选择保留整个候选集合"), Reward->TryCancelClaim(Reason));
		TestEqual(TEXT("取消不入库"), Backpack->GetItemCount(), 0);
		const auto FirstOptions = Options.Definitions;
		TestTrue(TEXT("再次打开保留同一随机功能或候选池"), Reward->TryBeginClaim(Options, Reason) && Options.Definitions == FirstOptions);
		bool bTemp = false;
		TestTrue(TEXT("确认一次进入正式发牌入口"), Reward->TryConfirmClaim(Chosen, C::Green, 6, bTemp, Slot, Reason));
		FCastleBuildingDescriptor Claimed;
		TestTrue(TEXT("只得到所选号色和建筑的一张一级牌"), Reward->IsClaimed() && !bTemp
			&& Backpack->GetItemCount() == 1 && Backpack->TryGetItemAt(0, Claimed)
			&& Claimed.Definition == Chosen && Claimed.Color == C::Green && Claimed.Number == 6 && Claimed.Level == 1);
		return true;
	}

	const auto& Cards = Prepared.Cards;
	if (!TestEqual(TEXT("精确生成张数"), Cards.Num(), Case.Cards)) { return false; }
	for (const auto& Card : Cards)
	{
		TestTrue(TEXT("全部是有效一级玩法牌"), Card.Validate(Reason) && Card.Level == 1 && Pool.Contains(Card.Definition));
	}
	const auto CheckCard = [&](const int32 Index, const B Type, const C Color, const int32 Number)
	{
		const auto& Card = Cards[Index];
		TestTrue(FString::Printf(TEXT("核对第 %d 张的类型、颜色与数字"), Index + 1),
			Card.GetBuildingType() == Type && (Color == C::Gray || Card.Color == Color) && (Number == 0 || Card.Number == Number));
	};
	const auto IsFunction = [](const FCastleBuildingDescriptor& Card) { return MakeArrayView(FunctionTypes).Contains(Card.GetBuildingType()); };
	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		const auto& Card = Cards[Index];
		const T ExpectedTrait = Ordinal == 10 ? T::Highway : Ordinal == 21 ? T::SkyIslandPortal : Ordinal == 23 ? T::SelfOrbiting : T::None;
		TestEqual(TEXT("特质种类正确"), Card.RoomTrait, ExpectedTrait);
		TestEqual(TEXT("仅自环绕初始为六轮"), Card.TraitRemainingRounds, Ordinal == 23 ? 6 : 0);
		switch (Ordinal)
		{
		case 1:
			TestTrue(TEXT("标准池数字为 1 至 6"), Card.Number <= 6);
			break;
		case 2:
			CheckCard(Index, B::NormalRoom, Index == 0 ? C::Blue : C::Gray, Index == 1 ? 6 : 0);
			TestTrue(TEXT("同色普通房数字为 1 至 6"), Card.Number <= 6);
			break;
		case 3:
			CheckCard(Index, Index == 0 ? B::ShieldTower : B::Farm, C::Gray, Cards[0].Number);
			TestTrue(TEXT("盾用 1 至 6，农田同号异色"), Card.Number <= 6 && (Index == 0 || Card.Color != Cards[0].Color));
			break;
		case 5:
			CheckCard(Index, B::NormalRoom, Cards[0].Color, Cards[0].Number);
			TestTrue(TEXT("双胞房间数字为 1 至 6"), Card.Number <= 6);
			break;
		case 6:
			TestTrue(TEXT("顶格补给为数字 6 的功能塔"), IsFunction(Card) && Card.Number == 6);
			break;
		case 7:
			CheckCard(Index, B::ArcherTower, C::Gray, 3);
			break;
		case 8:
			CheckCard(Index, B::CannonTower, C::Gray, 0);
			TestTrue(TEXT("炮塔只允许 1、2、5、6"), Card.Number != 3 && Card.Number != 4 && Card.Number <= 6);
			break;
		case 9:
			CheckCard(Index, B::Farm, Cards[0].Color, Index + 1);
			break;
		case 10:
		case 21:
			CheckCard(Index, B::NormalRoom, C::Gray, 1);
			break;
		case 11:
			CheckCard(Index, FunctionTypes[Index], C::Red, 0);
			TestTrue(TEXT("红色风暴各塔数字为 1 至 6"), Card.Number <= 6);
			break;
		case 12:
			CheckCard(Index, B::ArcherTower, Index < 2 ? C::Red : C::Blue, Index % 2 + 1);
			break;
		case 13:
			CheckCard(Index, Index < 2 ? B::CannonTower : Index == 2 ? B::Farm : B::NormalRoom,
				Index < 2 ? C::Red : Index == 2 ? C::Blue : C::Yellow, 2);
			break;
		case 14:
		{
			const B Types[] = {B::NormalRoom, B::ArcherTower, B::BeeHive, B::Farm};
			const C Colors[] = {C::Yellow, C::Red, C::Blue, C::Green};
			CheckCard(Index, Types[Index], Colors[Index], 2);
			break;
		}
		case 15:
			TestTrue(TEXT("大数字房间允许 7 至 9 的功能塔"), IsFunction(Card) && Card.Number >= 7 && Card.Number <= 9);
			break;
		case 17:
			TestTrue(TEXT("蓝 8 全部是功能塔"), IsFunction(Card) && Card.Color == C::Blue && Card.Number == 8);
			break;
		case 18:
			CheckCard(Index, Index < 4 ? B::BeeHive : B::NormalRoom, C::Green, Index < 4 ? 3 : 6 - Index);
			break;
		case 20:
			CheckCard(Index, B::CannonTower, C::Blue, 1);
			break;
		case 22:
			CheckCard(Index, B::NormalRoom, C::Green, 2);
			break;
		case 23:
			CheckCard(Index, B::CannonTower, C::Gray, 6);
			break;
		case 24:
			CheckCard(Index, B::ArcherTower, Cards[0].Color, Index + 1);
			break;
		case 25:
			TestTrue(TEXT("直接部署同功能、同数字和同颜色"), IsFunction(Card) && Card.Number <= 6
				&& CastleRoomCards::HaveSameIdentity(Card, Cards[0]));
			break;
		}
	}

	if (!TestTrue(TEXT("提交完整房间组"), UCastleItemGrantLibrary::TryCommitPreparedRoomGroup(Prepared, Backpack, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	const auto After = Backpack->GetItems();
	const auto TempAfter = Backpack->GetTempItems();
	TestEqual(TEXT("本次仅广播完整结果"), Observer->ObservedCounts.Num(), 1);
	TestEqual(TEXT("两栏总数精确增加"), After.Num() + TempAfter.Num() - Before.Num() - TempBefore.Num(),
		Cards.Num() - (Ordinal == 20 ? 1 : 0));
	if (Ordinal == 20)
	{
		TestTrue(TEXT("原首槽消费，临时栏队首先补到主栏末位"), After.Num() == 10 && After[0].Number == 2
			&& CastleRoomCards::HaveSameIdentity(After.Last(), TempBefore[0]));
		TestTrue(TEXT("临时栏未补入的牌留在新发牌之前"), CastleRoomCards::HaveSameIdentity(TempAfter[0], TempBefore[1]));
	}
	if (Ordinal == 22 || Ordinal == 26)
	{
		const C NewColor = Ordinal == 22 ? C::Green : After[0].Color;
		for (int32 Index = 0; Index < Before.Num(); ++Index)
		{
			auto Expected = Before[Index];
			if (Ordinal == 26 || Expected.Color == C::Red) { Expected.Color = NewColor; }
			TestTrue(TEXT("只修改主栏指定颜色，保留其它牌面"), CastleRoomCards::HaveSameIdentity(After[Index], Expected));
		}
		for (int32 Index = 0; Index < TempBefore.Num(); ++Index)
		{
			TestTrue(TEXT("临时栏原牌不被染色"), CastleRoomCards::HaveSameIdentity(TempAfter[Index], TempBefore[Index]));
		}
	}
	if (Ordinal == 2)
	{
		// 显式配置的空栏规则发两张普通房，不读取不存在的首卡。
		auto* EmptyBackpack = NewObject<UCastleBackpackComponent>();
		UCastlePendingRoomReward* Reward = nullptr;
		TestTrue(TEXT("空主栏补缝袋仍可发两张随机普通房"), UCastleItemGrantLibrary::TryGrantRoomGroup(
			Group, EmptyBackpack, nullptr, Pool, Random, Reward, Reason));
		TestEqual(TEXT("空栏规则精确两张"), EmptyBackpack->GetItemCount(), 2);
		for (const auto& Card : EmptyBackpack->GetItems())
		{
			TestTrue(TEXT("空栏产物为 1 至 6 的标准普通房"), Card.GetBuildingType() == B::NormalRoom && Card.Number <= 6);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleRoomGroupRejectedTransactionTest, "MovingCastle.Items.RoomGroups.Phase1A.RejectedTransactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleRoomGroupRejectedTransactionTest::RunTest(const FString& Parameters)
{
	using namespace CastleRoomGroupTests;
	auto Pool = MakePool();
	FWorldFixture Scene;
	FText Reason;
	if (!TestTrue(TEXT("拒绝测试棋盘就绪"), Scene.Initialize(Pool, Reason))) { AddError(Reason.ToString()); return false; }
	if (!TestNotNull(TEXT("棋盘只有普通房，仍视作没有功能塔"), Scene.Place(*Pool[0], FIntVector::ZeroValue, 1, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	auto* Backpack = Scene.Board->PrototypeBackpack.Get();
	auto* Observer = NewObject<UCastleBackpackTestObserver>();
	Observer->Backpack = Backpack;
	Backpack->OnBackpackChanged.AddDynamic(Observer, &UCastleBackpackTestObserver::Observe);
	FRandomStream Random(928);
	const int32 Seed = Random.GetCurrentSeed();
	for (const int32 Ordinal : {20, 24, 26})
	{
		const auto* Group = LoadGroup(Ordinal);
		if (!TestNotNull(TEXT("正式房间组"), Group)) { return false; }
		FCastlePreparedRoomGroup Prepared;
		const TCHAR* Expected = Ordinal == 24 ? TEXT("场上没有任何功能塔") : TEXT("主建筑栏为空");
		AddExpectedError(Expected, EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("购买前拒绝前置未满足的房间组"),
			UCastleItemGrantLibrary::TryPrepareRoomGroup(Group, Backpack, Scene.Board, Pool, Random, Prepared, Reason));
		TestTrue(TEXT("拒绝返回原因且不构造可提交事务"), Reason.ToString().Contains(Expected)
			&& !Prepared.Group && Prepared.Cards.IsEmpty() && !Prepared.PendingReward && !Prepared.MainBarChange.IsSet());
		TestEqual(TEXT("拒绝保留本次随机种子"), Random.GetCurrentSeed(), Seed);
	}
	TestTrue(TEXT("空栏拒绝不写库存也不广播"), Backpack->GetItemCount() == 0
		&& Backpack->GetTempItemCount() == 0 && Observer->ObservedCounts.IsEmpty());

	// 第一条规则先消耗候选随机数，第二条非法规则导致整批失败。验证原随机流、两栏和通知均未推进。
	const auto* RandomSupply = LoadGroup(1);
	if (!TestNotNull(TEXT("正式随机补给"), RandomSupply)) { return false; }
	auto* Broken = DuplicateObject(RandomSupply, GetTransientPackage());
	FCastleRoomCardRule InvalidRule;
	InvalidRule.NumberSource = ECastleRoomRuleNumber::RandomChoice;
	InvalidRule.NumberChoices = {1, 9, 10};
	Broken->CardRules.Add(InvalidRule);
	UCastlePendingRoomReward* Reward = nullptr;
	AddExpectedError(TEXT("第 2 条发牌规则失败"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("第二条非法规则拒绝整组"), UCastleItemGrantLibrary::TryGrantRoomGroup(
		Broken, Backpack, Scene.Board, Pool, Random, Reward, Reason));
	TestTrue(TEXT("晚失败无可见结果"), !Reward && Random.GetCurrentSeed() == Seed && Backpack->GetItemCount() == 0
		&& Backpack->GetTempItemCount() == 0 && Observer->ObservedCounts.IsEmpty());

	const auto* TopSupply = LoadGroup(6);
	if (!TestNotNull(TEXT("正式顶格补给"), TopSupply)) { return false; }
	auto IncompletePool = Pool;
	IncompletePool.Pop();
	AddExpectedError(TEXT("抽取池缺少建筑类别"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("缺失功能类型导致确定拒绝，不能缩小随机池"), UCastleItemGrantLibrary::TryGrantRoomGroup(
		TopSupply, Backpack, Scene.Board, IncompletePool, Random, Reward, Reason));
	TestEqual(TEXT("候选池错误未改变种子"), Random.GetCurrentSeed(), Seed);

	// 重复提交与夹在准备、扣费之间的库存变更都必须拒绝，不能覆盖已经写入的新牌。
	const auto* Shift = LoadGroup(26);
	if (!TestNotNull(TEXT("正式颜色转变"), Shift)) { return false; }
	int32 Slot;
	if (!Backpack->TryAddBuilding(Pool[0].Get(), 1, C::Yellow, 1, Slot, Reason)) { AddError(Reason.ToString()); return false; }
	FCastlePreparedRoomGroup Prepared;
	if (!UCastleItemGrantLibrary::TryPrepareRoomGroup(Shift, Backpack, Scene.Board, Pool, Random, Prepared, Reason)
		|| !Backpack->TryAddBuilding(Pool[0].Get(), 1, C::Red, 9, Slot, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	const auto Current = Backpack->GetItems();
	AddExpectedError(TEXT("主建筑栏已变化"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("拒绝使用过期主栏快照"), UCastleItemGrantLibrary::TryCommitPreparedRoomGroup(Prepared, Backpack, Reason));
	TestTrue(TEXT("过期失败保留新牌且不广播"), EqualCards(Current, Backpack->GetItems()) && Observer->ObservedCounts.IsEmpty());
	if (!UCastleItemGrantLibrary::TryPrepareRoomGroup(Shift, Backpack, Scene.Board, Pool, Random, Prepared, Reason)
		|| !UCastleItemGrantLibrary::TryCommitPreparedRoomGroup(Prepared, Backpack, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	AddExpectedError(TEXT("需要尚未提交的直接发牌事务"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("拒绝重复提交"), UCastleItemGrantLibrary::TryCommitPreparedRoomGroup(Prepared, Backpack, Reason));
	TestEqual(TEXT("只有重新准备后的提交广播一次"), Observer->ObservedCounts.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleRoomGroupDrawTest, "MovingCastle.Items.RoomGroups.Phase1A.IndependentDrawsAndBuyoutTie",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleRoomGroupDrawTest::RunTest(const FString& Parameters)
{
	using namespace CastleRoomGroupTests;
	auto Pool = MakePool();
	auto* Backpack = NewObject<UCastleBackpackComponent>();
	FText Reason;
	for (const int32 Ordinal : {1, 2, 7, 8, 10, 11, 17})
	{
		const auto* Group = LoadGroup(Ordinal);
		if (!TestNotNull(TEXT("正式房间组"), Group)) { return false; }
		bool bDifferentColor = false;
		bool bDifferentNumber = false;
		bool bDifferentType = false;
		for (int32 Sample = 0; Sample < 16; ++Sample)
		{
			FRandomStream Random(173 * Sample);
			FCastlePreparedRoomGroup Prepared;
			if (!UCastleItemGrantLibrary::TryPrepareRoomGroup(Group, Backpack, nullptr, Pool, Random, Prepared, Reason))
			{
				AddError(Reason.ToString());
				return false;
			}
			const auto& First = Prepared.Cards[0];
			for (const auto& Card : Prepared.Cards)
			{
				bDifferentColor |= Card.Color != First.Color;
				bDifferentNumber |= Card.Number != First.Number;
				bDifferentType |= Card.Definition != First.Definition;
			}
		}
		// 无共享的维度必须能在同批出现不同结果。固定值维度不参与比较。
		const FString Name = Group->DisplayName.ToString();
		if (Ordinal == 1 || Ordinal == 2 || Ordinal == 7 || Ordinal == 8 || Ordinal == 10)
		{
			TestTrue(Name + TEXT(" 的颜色逐张抽取"), bDifferentColor);
		}
		if (Ordinal == 1 || Ordinal == 2 || Ordinal == 8 || Ordinal == 11)
		{
			TestTrue(Name + TEXT(" 的数字逐张抽取"), bDifferentNumber);
		}
		if (Ordinal == 1 || Ordinal == 17) { TestTrue(Name + TEXT(" 的功能逐张抽取"), bDifferentType); }
	}

	FWorldFixture Scene;
	if (!Scene.Initialize(Pool, Reason) || !Scene.Place(*Pool[2], FIntVector::ZeroValue, 2, Reason)
		|| !Scene.Place(*Pool[1], FIntVector(2, 0, 0), 1, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	const auto* Buyout = LoadGroup(24);
	if (!TestNotNull(TEXT("正式功能塔买断"), Buyout)) { return false; }
	TSet<B> DrawnTypes;
	for (int32 Sample = 0; Sample < 16; ++Sample)
	{
		FRandomStream Random(173 * Sample);
		FRandomStream Replay = Random;
		FCastlePreparedRoomGroup Prepared, Replayed;
		if (!UCastleItemGrantLibrary::TryPrepareRoomGroup(Buyout, Backpack, Scene.Board, Pool, Random, Prepared, Reason)
			|| !UCastleItemGrantLibrary::TryPrepareRoomGroup(Buyout, Backpack, Scene.Board, Pool, Replay, Replayed, Reason)
			|| !TestEqual(TEXT("买断平局仍精确发九张"), Prepared.Cards.Num(), 9))
		{
			AddError(Reason.ToString());
			return false;
		}
		const B Type = Prepared.Cards[0].GetBuildingType();
		TestTrue(TEXT("一座双格炮塔与一座箭塔视为平局，整批共享胜出类型"), (Type == B::ArcherTower || Type == B::CannonTower)
			&& !Prepared.Cards.ContainsByPredicate([Type](const auto& Card) { return Card.GetBuildingType() != Type; }));
		TestTrue(TEXT("同种子平局结果及后续随机流可复现"),
			EqualCards(Prepared.Cards, Replayed.Cards) && Random.GetCurrentSeed() == Replay.GetCurrentSeed());
		DrawnTypes.Add(Type);
	}
	TestEqual(TEXT("平局两个类别皆可抽中"), DrawnTypes.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleHighwayRoomTraitTest, "MovingCastle.Items.RoomTraits.Phase1A.Highway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleHighwayRoomTraitTest::RunTest(const FString& Parameters)
{
	using namespace CastleRoomGroupTests;
	auto Pool = MakePool();
	// 高速公路和上层房参与正式 PCG 贯通，必须使用带作者楼板和语义的正式普通房。
	Pool[0] = LoadObject<UCastleBuildingDefinition>(nullptr, TEXT("/Game/MovingCastle/Data/Buildings/DA_NormalRoom.DA_NormalRoom"));
	if (!TestNotNull(TEXT("正式普通房带 PCG 楼板"), Pool[0].Get())) { return false; }
	FWorldFixture Scene;
	FText Reason;
	const FTransform Transform(FRotator(0, 37, 0), FVector(150, 260, 60), FVector(1.25, 1.5, 0.9));
	if (!Scene.Initialize(Pool, Reason, Transform, true)) { AddError(Reason.ToString()); return false; }
	FCastleBuildingDescriptor RoadCard;
	if (!Pool[0]->CreateDescriptor(1, C::Red, 1, RoadCard, Reason)) { AddError(Reason.ToString()); return false; }
	RoadCard.RoomTrait = T::Highway;
	const FIntVector Road(1, 1, 0);
	auto* Building = Scene.PlaceCard(RoadCard, Road, Reason);
	if (!TestNotNull(TEXT("高速公路使用普通房放置入口"), Building)) { AddError(Reason.ToString()); return false; }
	const auto Cell = Scene.Board->GridOrigin + Road;
	TestTrue(TEXT("保留普通房建筑定义、PCG 身份和不可升级合成规则"), Building->IsNormalRoom() && !Building->CanMergeToNextLevel()
		&& Building->GetRoomTraitComponent() && Building->GetBuildingDescriptor().RoomTrait == T::Highway);
	TestTrue(TEXT("正上方空时道路有效"), Scene.Board->IsHighwayCellActive(Cell));

	// 共享路线使用棋盘旋转及缩放后的采样。首末采样都在格外，必须精确截取中间区间。
	FCastleSharedNavigationPath Path;
	const FVector Center = Scene.Board->GetGridWorldLocation(Cell);
	const FVector Shift = Transform.TransformVector(FVector(Scene.Board->CellSize, 0, 0));
	Path.PatrolPoints = {Center - Shift, Center + Shift};
	const double CellWidth = Shift.Size();
	Path.PatrolDistances = {0.0, CellWidth * 2.0};
	Path.Length = Path.PatrolDistances.Last();
	UCastleNavigationSubsystem::CompileHighwayRanges(*Scene.World, Path);
	if (!TestEqual(TEXT("编译一段道路"), Path.HighwayRanges.Num(), 1)) { return false; }
	const double Enter = Path.HighwayRanges[0].StartDist;
	const double Exit = Path.HighwayRanges[0].EndDist;
	TestTrue(TEXT("入格边界精确"), FMath::IsNearlyEqual(Enter, CellWidth * 0.5, 1.e-6));
	TestTrue(TEXT("出格边界精确"), FMath::IsNearlyEqual(Exit, CellWidth * 1.5, 1.e-6));
	FCastleMassMovementSettings Settings;
	Settings.MaxSpeed = 100.0f;
	Settings.Acceleration = 100.0f;
	double Speed = 100.0;
	double Travel = 0.0;
	TestTrue(TEXT("同一帧先普通行 10cm 再道路行 135cm"), CastlePatrolMovement::Advance(
		Path, Settings, Enter - 10.0, 1.0, Speed, Travel) && FMath::IsNearlyEqual(Travel, 145.0, 1.e-6)
		&& FMath::IsNearlyEqual(Speed, 150.0));
	TestTrue(TEXT("离开道路后的同一帧立即按原速积分"), CastlePatrolMovement::Advance(
		Path, Settings, Exit - 15.0, 1.0, Speed, Travel) && FMath::IsNearlyEqual(Travel, 105.0, 1.e-6)
		&& FMath::IsNearlyEqual(Speed, 100.0));
	Speed = 150.0;
	TestTrue(TEXT("恰好出格也立即恢复原速"), CastlePatrolMovement::Advance(
		Path, Settings, Exit - 15.0, 0.1, Speed, Travel) && FMath::IsNearlyEqual(Travel, 15.0, 1.e-6)
		&& FMath::IsNearlyEqual(Speed, 100.0));
	// 梯子和高速公路可同时覆盖路径的一部分，在既有上限上乘道路倍率。
	FCastleLadderPathRange& Ladder = Path.LadderRanges.AddDefaulted_GetRef();
	Ladder.StartDist = Enter;
	Ladder.EndDist = Exit;
	Settings.LadderSpeedMultiplier = 0.5f;
	Speed = 0.0;
	TestTrue(TEXT("格内梯子达到原梯速的 1.5 倍"), CastlePatrolMovement::Advance(Path, Settings, Enter, 1.0, Speed, Travel)
		&& FMath::IsNearlyEqual(Speed, 75.0) && FMath::IsNearlyEqual(Travel, 56.25, 1.e-6));
	Path.LadderRanges.Reset();

	auto* Cover = Scene.Place(*Pool[0], Road + FIntVector(0, 0, 1), 1, Reason);
	if (!TestNotNull(TEXT("同色普通房能叠在高速公路上方"), Cover)) { AddError(Reason.ToString()); return false; }
	TestTrue(TEXT("带特质和普通房均通过正式 PCG 并保留独立建筑"),
		!Building->BuildingPCG->GetAppliedKey().IsEmpty() && !Cover->BuildingPCG->GetAppliedKey().IsEmpty()
		&& Scene.Board->GetBuildingAt(Cell) == Building);
	TestFalse(TEXT("Z+1 占用使道路失效"), Scene.Board->IsHighwayCellActive(Cell));
	UCastleNavigationSubsystem::CompileHighwayRanges(*Scene.World, Path);
	TestTrue(TEXT("覆盖后路线快照无道路"), Path.HighwayRanges.IsEmpty());
	Speed = 100.0;
	TestTrue(TEXT("覆盖后经过该格按 1.0 倍移动"), CastlePatrolMovement::Advance(Path, Settings, Enter, 1.0, Speed, Travel)
		&& FMath::IsNearlyEqual(Travel, 100.0));
	if (!Scene.Board->RemoveBuilding(Cover, Reason)) { AddError(Reason.ToString()); return false; }
	UCastleNavigationSubsystem::CompileHighwayRanges(*Scene.World, Path);
	TestEqual(TEXT("上方拆除后重新编译出道路"), Path.HighwayRanges.Num(), 1);
	// 用棋盘实际回收接口证明特质仍随一级房间流转。
	if (!Scene.Board->RemoveBuildingToBackpack(Building, Scene.Board->PrototypeBackpack, Reason)) { AddError(Reason.ToString()); return false; }
	FCastleBuildingDescriptor Recovered;
	TestTrue(TEXT("道路拆回保留原卡牌及高速公路标记"), Scene.Board->PrototypeBackpack->TryGetItemAt(0, Recovered)
		&& CastleRoomCards::HaveSameIdentity(Recovered, RoadCard));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePortalRoomTraitTest, "MovingCastle.Items.RoomTraits.Phase1A.SkyIslandPortal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePortalRoomTraitTest::RunTest(const FString& Parameters)
{
	using namespace CastleRoomGroupTests;
	auto Pool = MakePool();
	FWorldFixture Scene;
	FText Reason;
	if (!Scene.Initialize(Pool, Reason)) { AddError(Reason.ToString()); return false; }
	ACastlePlayerController* Controller;
	UCastleDirectorConfigDataAsset* Config;
	UCastleDirectorSubsystem* Director = Scene.ConfigureDirector(Controller, Config);
	auto* Pawn = Scene.World->SpawnActor<ADefaultPawn>(FVector(-1800, 0, 600), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("可传送的玩家 Pawn"), Pawn)) { return false; }
	Controller->Possess(Pawn);
	if (!TestTrue(TEXT("控制器已持有测试 Pawn"), Controller->GetPawn() == Pawn)) { return false; }
	Director->ClearedSteps = 4;
	const int32 Step = Director->GetNextStep();
	const auto Start = Pawn->GetActorTransform();
	// 显式登记夹具的主塔，让额外空岛与正式导演共享同一个返塔锚点。
	Scene.Board->MainTowerActor = Director->Tower->GetOwner();

	FCastleBuildingDescriptor Card;
	if (!Pool[0]->CreateDescriptor(1, C::Red, 1, Card, Reason)) { AddError(Reason.ToString()); return false; }
	Card.RoomTrait = T::SkyIslandPortal;
	auto* Room = Scene.PlaceCard(Card, FIntVector(1, 1, 0), Reason);
	if (!TestNotNull(TEXT("已放置传送门房间"), Room)) { AddError(Reason.ToString()); return false; }
	auto* Door = Room->GetRoomTraitComponent();
	if (!TestNotNull(TEXT("房间注册传送门网格组件"), Door)) { return false; }
	TestTrue(TEXT("门体挂在 GameplayRoot 下且响应 Visibility"),
		Door->GetAttachParent() == Room->GameplayRoot && Door->IsPortalAvailable());
	TestEqual(TEXT("复用离岛门网格"), Door->GetStaticMesh()->GetPathName(), FString(TEXT("/Engine/BasicShapes/Cube.Cube")));
	TestEqual(TEXT("复用离岛门材质"), Door->GetMaterial(0)->GetPathName(), FString(TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial")));

	const FVector Aim = Door->GetComponentLocation();
	const FVector Offset(-200, 0, 0);
	const FVector Miss(0, 200, 0);
	bool bHitPortal = false;
	TestFalse(TEXT("准星射线未命中门体不进岛"),
		Controller->TryInteractWithRoomPortalFromRay(Aim + Offset + Miss, Aim + Miss, Reason, &bHitPortal));
	TestFalse(TEXT("普通表面射线未标记门体命中"), bHitPortal);
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Battle);
	TestFalse(TEXT("战斗阶段 E 不触发"), Controller->TryInteractWithRoomPortalFromRay(Aim + Offset, Aim, Reason));
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Editing);
	// 原节点在额外空岛期间必须保留同一 Actor。战斗测试辅助会清节点，因此在其之后登记本步节点。
	auto* OriginalNode = Scene.World->SpawnActor<ACastleDirectorNodeActor>(FVector(-7500, 0, 600), FRotator::ZeroRotator);
	OriginalNode->ActivateNode();
	Director->Nodes = {OriginalNode};

	AddExpectedError(TEXT("四种空岛必须全部配置具体玩法类"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("配置缺失时拒绝传送"), Controller->TryInteractWithRoomPortalFromRay(Aim + Offset, Aim, Reason));
	TestTrue(TEXT("失败未移动玩家、消耗门或推进关数"), Pawn->GetActorTransform().Equals(Start) && Room->GetRoomTrait() == T::SkyIslandPortal
		&& Director->GetNextStep() == Step && Director->BonusSkyIslandVisitCount == 0);
	for (const auto Type : {ECastleSkyIslandType::Shop, ECastleSkyIslandType::NPC, ECastleSkyIslandType::Gamble, ECastleSkyIslandType::Rest})
	{
		Config->SkyIslandClasses.Add(Type, ACastleSkyIslandTestFixture::StaticClass());
	}
	TSet<FString> IslandTypes;
	for (int32 Visit = 0; Visit < 16 && IslandTypes.Num() < 4; ++Visit)
	{
		Door = Room->GetRoomTraitComponent();
		const FVector Center = Door->GetComponentLocation();
		bHitPortal = false;
		if (!TestTrue(TEXT("控制器门体射线入口成功进岛"),
			Controller->TryInteractWithRoomPortalFromRay(Center + Offset, Center, Reason, &bHitPortal)))
		{
			AddError(Reason.ToString());
			return false;
		}
		TestTrue(TEXT("控制器标记射线直接命中门体组件"), bHitPortal);
		ACastleSkyIslandBase* Island = Director->ActiveSkyIsland;
		if (!TestNotNull(TEXT("本次传送创建实际空岛 Actor"), Island)) { return false; }
		auto* Label = Island->FindComponentByClass<UTextRenderComponent>();
		auto* Departure = Island->FindComponentByClass<UCastleSkyIslandDepartureComponent>();
		if (!TestNotNull(TEXT("空岛类型标题"), Label) || !TestNotNull(TEXT("空岛离开门"), Departure)) { return false; }
		FString Type;
		Label->Text.ToString().Split(TEXT("  |"), &Type, nullptr);
		IslandTypes.Add(Type);
		TestTrue(TEXT("入岛传送位置正确并暂时关闭原节点碰撞"), Pawn->GetActorLocation().Equals(Island->GetPlayerArrivalTransform().GetLocation(), 0.1)
			&& OriginalNode->IsHidden() && !OriginalNode->GetActorEnableCollision() && Director->GetPhase() == ECastleDirectorPhase::SkyIsland);
		TestTrue(TEXT("成功时原地变回普通房并销毁门组件"), Room->IsNormalRoom() && Room->GetRoomTrait() == T::None
			&& !Room->GetRoomTraitComponent() && Scene.Board->GetBuildingAt(Scene.Board->GridOrigin + FIntVector(1, 1, 0)) == Room);
		const FVector DepartureCenter = Departure->GetComponentLocation();
		if (!TestTrue(TEXT("通过空岛离开门发出返塔请求"), Director->TryInteractWithSkyIsland(
			DepartureCenter + FVector::UpVector * 450.0, DepartureCenter, Reason))) { AddError(Reason.ToString()); return false; }
		Director->Tick(0.0f);
		CastleTowerOverviewTests::Check(*this, *Scene.Board, *Controller);
		TestTrue(TEXT("额外空岛返塔恢复原节点，不推进步骤"),
			!Director->ActiveSkyIsland && Director->GetClearedSteps() == 4 && Director->GetNextStep() == Step
			&& Director->GetPhase() == ECastleDirectorPhase::Editing && Director->Nodes.Num() == 1
			&& Director->Nodes[0] == OriginalNode && !OriginalNode->IsHidden() && OriginalNode->GetActorEnableCollision());
		TestFalse(TEXT("再次瞄准消耗过的同一位置不进岛"), Controller->TryInteractWithRoomPortalFromRay(Center + Offset, Center, Reason));
		if (Visit == 0)
		{
			// 直接调用导演也不能绕过已消耗门体的限制。拆回发放的普通牌不恢复传送门。
			AddExpectedError(TEXT("且尚有门体的传送门房间"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("普通房不能重入"), Director->TryEnterBonusSkyIslandFromPortal(Room, Reason));
			if (!Scene.Board->RemoveBuildingToBackpack(Room, Scene.Board->PrototypeBackpack, Reason)) { AddError(Reason.ToString()); return false; }
			FCastleBuildingDescriptor Recovered;
			TestTrue(TEXT("回收到主栏的卡是普通房"), Scene.Board->PrototypeBackpack->TryGetItemAt(0, Recovered)
				&& Recovered.RoomTrait == T::None && Recovered.Definition == Card.Definition);
		}
		else if (!Scene.Board->RemoveBuilding(Room, Reason)) { AddError(Reason.ToString()); return false; }
		if (IslandTypes.Num() < 4)
		{
			Room = Scene.PlaceCard(Card, FIntVector(1, 1, 0), Reason);
			if (!TestNotNull(TEXT("同一步能使用下一张新的门牌"), Room)) { AddError(Reason.ToString()); return false; }
		}
	}
	TestTrue(TEXT("全四类都能通过额外空岛随机入口到达"), IslandTypes.Contains(TEXT("Shop")) && IslandTypes.Contains(TEXT("NPC"))
		&& IslandTypes.Contains(TEXT("Gamble")) && IslandTypes.Contains(TEXT("Rest")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleSelfOrbitingRoomTraitTest, "MovingCastle.Items.RoomTraits.Phase1A.SelfOrbiting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleSelfOrbitingRoomTraitTest::RunTest(const FString& Parameters)
{
	using namespace CastleRoomGroupTests;
	auto Pool = MakePool();
	FWorldFixture Scene;
	FText Reason;
	if (!Scene.Initialize(Pool, Reason)) { AddError(Reason.ToString()); return false; }
	ACastlePlayerController* Controller;
	UCastleDirectorConfigDataAsset* Config;
	auto* Director = Scene.ConfigureDirector(Controller, Config);
	auto* Backpack = Scene.Board->PrototypeBackpack.Get();
	FCastleBuildingDescriptor Orbit, Ordinary;
	if (!Pool[2]->CreateDescriptor(1, C::Red, 6, Orbit, Reason) || !Pool[0]->CreateDescriptor(1, C::Red, 1, Ordinary, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	Orbit.RoomTrait = T::SelfOrbiting;
	Orbit.TraitRemainingRounds = 6;
	TArray<FCastleBuildingDescriptor> Cards;
	Cards.Init(Ordinary, 10);
	Cards[0] = Orbit;
	auto TempOrbit = Orbit;
	TempOrbit.TraitRemainingRounds = 4;
	Cards.Add(TempOrbit);
	if (!Backpack->TryAddItemsAllowOverflow(Cards, Reason)) { AddError(Reason.ToString()); return false; }
	auto PlacedOrbit = Orbit;
	PlacedOrbit.TraitRemainingRounds = 5;
	auto* Room = Scene.PlaceCard(PlacedOrbit, FIntVector(1, 1, 0), Reason);
	if (!TestNotNull(TEXT("另一张自环绕炮塔放到场上"), Room)) { AddError(Reason.ToString()); return false; }
	TestTrue(TEXT("场上炮塔保留回收信息并使用普通炮塔逻辑"),
		Room->GetBuildingType() == B::CannonTower && !Room->GetRoomTraitComponent() && Room->CanMergeToNextLevel());

	auto* Observer = NewObject<UCastleBackpackTestObserver>();
	Observer->Backpack = Backpack;
	Backpack->OnBackpackChanged.AddDynamic(Observer, &UCastleBackpackTestObserver::Observe);
	Director->SetPhaseForAutomation(ECastleDirectorPhase::SkyIsland);
	Director->SetPhaseForAutomation(ECastleDirectorPhase::Editing);
	TestEqual(TEXT("没有清场的返编辑不发牌"), Backpack->GetTempItemCount(), 1);

	for (int32 Round = 1; Round <= 7; ++Round)
	{
		Director->SetPhaseForAutomation(ECastleDirectorPhase::Battle);
		if (!Scene.Board->SetPlacementSurfacePresentationHidden(true, Reason)) { AddError(Reason.ToString()); return false; }
		Director->HandleBattleCleared(ECastleBattleKind::Normal, Round);
		if (!TestEqual(TEXT("实际受理清场后先进入领奖"), Director->GetPhase(), ECastleDirectorPhase::Reward)) { return false; }
		TestEqual(TEXT("尚未返回编辑时不发奖励"), Backpack->GetTempItemCount(), Round);
		if (Round == 1)
		{
			// 错误的定义池要阻止本轮结算。配置恢复后同一清场恰好结算一次。
			Scene.Board->BuildingDefinitions.Pop();
			AddExpectedError(TEXT("自环绕炮塔结算失败"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("不完整的功能池拒绝本轮奖励"), Director->TryChooseReward(ECastleDirectorReward::Healing, Reason));
			TestTrue(TEXT("失败保留余轮、临时库存与奖励阶段"), EqualCards(Backpack->GetItems(), MakeArrayView(Cards.GetData(), 10))
				&& Backpack->GetTempItemCount() == 1 && Director->GetPhase() == ECastleDirectorPhase::Reward);
			Scene.Board->BuildingDefinitions = Pool;
		}
		if (!TestTrue(TEXT("正式领奖成功返回编辑"), Director->TryChooseReward(ECastleDirectorReward::Healing, Reason)))
		{
			AddError(Reason.ToString());
			return false;
		}
		FCastleBuildingDescriptor Main;
		if (!Backpack->TryGetItemAt(0, Main)) { return false; }
		TestTrue(TEXT("仅主栏消耗轮数，归零变回普通炮塔"), Main.TraitRemainingRounds == FMath::Max(0, 6 - Round)
			&& Main.RoomTrait == (Round < 6 ? T::SelfOrbiting : T::None) && Main.Definition == Orbit.Definition
			&& Main.Color == Orbit.Color && Main.Number == 6);
		const auto Temp = Backpack->GetTempItems();
		TestEqual(TEXT("每轮一张，最多六轮，满主栏后追加到临时栏"), Temp.Num(), 1 + FMath::Min(6, Round));
		TestTrue(TEXT("临时栏和场上的自环绕不消耗轮数"), CastleRoomCards::HaveSameIdentity(Temp[0], TempOrbit)
			&& CastleRoomCards::HaveSameIdentity(Room->GetBuildingDescriptor(), PlacedOrbit));
		if (Round <= 6)
		{
			const auto& Reward = Temp.Last();
			TestTrue(TEXT("随机功能塔为合法颜色、一级数字六且不带特质"), MakeArrayView(FunctionTypes).Contains(Reward.GetBuildingType())
				&& CastleTileColors::IsPlayable(Reward.Color) && Reward.Level == 1 && Reward.Number == 6 && Reward.RoomTrait == T::None);
		}
	}
	TestEqual(TEXT("六轮各广播一次，第七轮无变更"), Observer->ObservedCounts.Num(), 6);
	AddExpectedError(TEXT("导演拒绝非法或重复清场通知"), EAutomationExpectedErrorFlags::Contains, 1);
	Director->HandleBattleCleared(ECastleBattleKind::Normal, 7);
	TestEqual(TEXT("重复清场不追加奖励"), Backpack->GetTempItemCount(), 7);

	// 留出真实主栏空位，棋盘回收仍是相同一级牌。之后与新炮塔合成，验证升级结果不会复制特质。
	FCastleBuildingDescriptor Removed, Recovered;
	if (!Backpack->TryRemoveItemAt(9, Removed, Reason) || !Scene.Board->RemoveBuildingToBackpack(Room, Backpack, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	TestTrue(TEXT("按原定义回收一级塔时保留五轮"), Backpack->TryGetItemAt(9, Recovered)
		&& CastleRoomCards::HaveSameIdentity(Recovered, PlacedOrbit));
	if (!Scene.PlaceCard(Recovered, FIntVector(1, 1, 0), Reason)
		|| !Scene.PlaceCard(Orbit, FIntVector(2, 1, 0), Reason, true))
	{
		AddError(Reason.ToString());
		return false;
	}
	const auto* Merged = CastleBuildingTests::FindPlacedLevel(Scene.Board, 2);
	TestTrue(TEXT("同色同数照常合成，二级塔不继承特质或余轮"), Merged && Merged->GetRoomTrait() == T::None
		&& Merged->GetBuildingDescriptor().TraitRemainingRounds == 0);
	return true;
}

#endif
