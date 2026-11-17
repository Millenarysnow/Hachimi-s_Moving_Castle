#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Support/CastleSupportFlightActors.h"
#include "Castle/Support/CastleSupportTowerData.h"
#include "Castle/Support/CastleSupportTowerSubsystem.h"
#include "CastleBuildingTestUtilities.h"
#include "CastleMassFragments.h"
#include "CastleMassTraits.h"
#include "CastleMassUnitSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "MassCommonFragments.h"
#include "MassEntityQuery.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"
#include "Misc/AutomationTest.h"
#include "Units/Mass/Projectiles/CastleMassHomingProjectileFragments.h"

namespace CastleSupportTests
{
	UCastleBuildingDefinition* LoadDefinition(const TCHAR* Name)
	{
		return LoadObject<UCastleBuildingDefinition>(nullptr, *FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), Name, Name));
	}

	/** 棋盘几何、等级蓝图与落地碰撞均走生产边界；敌人用不自移的 Mass 模板验证准确伤害。 */
	struct FFixture
	{
		UWorld* World;
		ACastleBoard* Board = nullptr;
		UCastleSupportTowerSubsystem* Support = nullptr;
		UCastleMassUnitSubsystem* Units = nullptr;
		FFixture()
		{
			const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Board = World->SpawnActor<ACastleBoard>();
			Support = World->GetSubsystem<UCastleSupportTowerSubsystem>();
			Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
		}
		~FFixture()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		FMassEntityManager& Manager() const { return World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager(); }
		bool Ground(const int32 Width, const int32 Height, const ECastleTileColor Color, FText& Reason)
		{
			Board->GridSize = FIntVector(Width, Height, 3);
			FCastleGroundGenerationResult Result;
			for (int32 X = 0; X < Width; ++X) for (int32 Y = 0; Y < Height; ++Y)
			{
				FCastleGroundCell& Cell = Result.Cells.AddDefaulted_GetRef();
				Cell.Coordinate = FIntVector(X, Y, 0);
				Cell.Color = Color;
				Cell.Number = 4;
			}
			return Board->ApplyGroundGenerationResult(Result, Reason);
		}
		ACastleBuildingBase* Place(UCastleBuildingDefinition* Definition, const FIntVector& Cell, const int32 Level,
			const ECastleTileColor Color, const int32 Number, FText& Reason, const bool bMerge = false)
		{
			FCastleBuildingDescriptor Card;
			if (!Definition || !Definition->CreateDescriptor(Level, Color, Number, Card, Reason)) { return nullptr; }
			ACastleBuildingBase* Building = Board->PlaceBuilding(Cell, Card, bMerge, Reason);
			return CastleBuildingTests::CompletePlacement(Building) ? Building : nullptr;
		}
		bool Enemies(const TArray<FTransform>& Transforms, TArray<FCastleMassUnitHandle>& Out, FText& Reason) const
		{
			UMassEntityConfigAsset* Config = NewObject<UMassEntityConfigAsset>();
			auto* Trait = NewObject<UCastleMassEnemyTrait>(Config);
			Trait->MaxHealth = 1000.0f;
			Config->GetMutableConfig().AddTrait(*Trait);
			return Units->SpawnUnits(Config, Transforms, Out, Reason);
		}
		FCastleMassUnitFragment& Vital(const FCastleMassUnitHandle& Unit) const
		{
			return Manager().GetFragmentDataChecked<FCastleMassUnitFragment>(Unit.Entity);
		}
		TArray<int32> BeeCounts() const
		{
			TArray<int32> Counts;
			for (TActorIterator<ACastleBeeSwarmVisual> It(World); It; ++It)
			{
				TArray<UInstancedStaticMeshComponent*> Components;
				It->GetComponents(Components);
				for (const auto* Mesh : Components)
				{
					if (Mesh->GetFName() == FName(TEXT("BeeBodies"))) { Counts.Add(Mesh->GetInstanceCount()); }
				}
			}
			Counts.Sort();
			return Counts;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleSupportAssetTest, "MovingCastle.Support.AssetsAndMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleSupportAssetTest::RunTest(const FString& Parameters)
{
	using namespace CastleSupportTests;
	FFixture Game;
	FText Reason;
	if (!TestTrue(TEXT("建立正式蓝图使用的棋盘地面"), Game.Ground(8, 5, ECastleTileColor::Red, Reason))) { AddError(Reason.ToString()); return false; }
	const TCHAR* Names[] = {TEXT("BeeHive"), TEXT("BalloonTower")};
	for (int32 Family = 0; Family < 2; ++Family)
	{
		auto* Definition = LoadDefinition(Names[Family]);
		if (!TestNotNull(Names[Family], Definition) || !TestTrue(TEXT("正式定义四级完整"), Definition->ValidateConfiguration(Reason))) { return false; }
		TestNull(TEXT("没有借用箭塔攻击定义"), Definition->AttackData.Get());
		TestNull(TEXT("没有五级替代类"), Definition->FindLevel(5));
		for (const int32 Level : {2, 3})
		{
			const auto* Entry = Definition->FindLevel(Level);
			TestTrue(TEXT("显式区分竖向和水平美术类"), Entry && Entry->VerticalBuildingClass != Entry->BuildingClass);
		}
		const int32 Y = Family * 2;
		if (!TestNotNull(TEXT("放置同色同号的第一张一级卡"), Game.Place(Definition, FIntVector(0, Y, 0), 1, ECastleTileColor::Red, 4, Reason, true))) { AddError(Reason.ToString()); return false; }
		Game.Place(Definition, FIntVector(1, Y, 0), 1, ECastleTileColor::Red, 4, Reason, true);
		const auto Buildings = Game.Board->GetPlacedBuildings();
		TestTrue(TEXT("使用真实美术与合成流程生成同定义二级"), Buildings.ContainsByPredicate([&](const ACastleBuildingBase* Building)
		{
			return Building->GetBuildingDefinition() == Definition && Building->GetBuildingLevel() == 2 && Building->GetOccupiedCoordinates().Num() == 2;
		}));
		for (int32 Level = 3; Level <= 4; ++Level)
		{
			if (!TestNotNull(TEXT("放置已配置高等级塔，可见美术通过检查"), Game.Place(Definition, FIntVector(Level * 2 - 2, Y, 0), Level, ECastleTileColor::Red, 4, Reason)))
			{ AddError(Reason.ToString()); return false; }
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleHiveCombatTest, "MovingCastle.Support.HiveCombat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleHiveCombatTest::RunTest(const FString& Parameters)
{
	using namespace CastleSupportTests;
	FFixture Game;
	FText Reason;
	const auto Color = ECastleTileColor::Red;
	if (!TestTrue(TEXT("生成棋盘"), Game.Ground(6, 4, Color, Reason))) { return false; }
	auto* Definition = LoadDefinition(TEXT("BeeHive"));
	auto* A = Game.Place(Definition, FIntVector(0, 0, 0), 2, Color, 5, Reason);
	auto* B = Game.Place(Definition, FIntVector(0, 1, 0), 1, Color, 2, Reason);
	auto* C = Game.Place(Definition, FIntVector(1, 1, 0), 1, Color, 1, Reason);
	if (!TestNotNull(TEXT("10 蜜蜂主塔"), A) || !TestNotNull(TEXT("2 蜜蜂主塔"), B) || !TestNotNull(TEXT("第 13 只蜜蜂"), C))
	{ AddError(Reason.ToString()); return false; }
	TArray<FCastleMassUnitHandle> Enemies;
	const FVector Target = A->GetActorLocation() + FVector(0, 0, 650);
	if (!TestTrue(TEXT("生成两个固定目标"), Game.Enemies({FTransform(Target), FTransform(Target + FVector(10, 15, 30))}, Enemies, Reason))) { return false; }
	Game.Vital(Enemies[0]).Health = 900.0f;
	// 较近的敌人生命更低；两批必须都弃近选当前生命最高者。
	if (!Game.Support->QueueLaunch(*A) || !Game.Support->QueueLaunch(*B)) { return false; }
	Game.Support->Tick(1.0f);
	float Health = Game.Vital(Enemies[1]).Health;
	Game.Support->Tick(1.0f);
	TestEqual(TEXT("12 只时维持每秒两次"), Health - Game.Vital(Enemies[1]).Health, 6.0f);
	TestEqual(TEXT("较近但生命较低者未被命中"), Game.Vital(Enemies[0]).Health, 900.0f);
	if (!Game.Support->QueueLaunch(*C)) { return false; }
	Game.Support->Tick(1.0f);
	Health = Game.Vital(Enemies[1]).Health;
	Game.Support->Tick(1.0f);
	TestEqual(TEXT("13 只时连同一级蜂群翻倍"), Health - Game.Vital(Enemies[1]).Health, 13.0f);
	Game.Vital(Enemies[1]).Health = 1.0f;
	Game.Support->Tick(1.0f);
	Game.Support->Tick(1.0f);
	TestEqual(TEXT("击杀只扣至死亡，不重复命中"), Game.Vital(Enemies[1]).Health, 0.0f);
	TestTrue(TEXT("存活蜂换到剩余敌人并开始新攻击窗口"), Game.Vital(Enemies[0]).Health < 900.0f);
	// Level4 在真实阵亡坐标生成半数实例，且不与备用巢队列混淆。
	TestTrue(TEXT("先回收上一段测试出击"), Game.Support->EndBattleSupport());
	auto* D = Game.Place(Definition, FIntVector(3, 0, 0), 4, Color, 4, Reason);
	if (!TestNotNull(TEXT("四级蜂巢"), D)) { return false; }
	Game.Vital(Enemies[0]).Health = 1.0f;
	Game.Support->QueueLaunch(*D);
	Game.Support->Tick(3.0f);
	bool bChildAtDeath = false;
	for (TActorIterator<ACastleBeeSwarmVisual> It(Game.World); It; ++It)
	{
		if (!It->GetActorLocation().Equals(Target, 0.1)) { continue; }
		TArray<UInstancedStaticMeshComponent*> Components;
		It->GetComponents(Components);
		bChildAtDeath |= Components.ContainsByPredicate([](const UInstancedStaticMeshComponent* Mesh)
		{ return Mesh->GetFName() == FName(TEXT("BeeBodies")) && Mesh->GetInstanceCount() == 16; });
	}
	TestTrue(TEXT("32 只致死后在阵亡点孵化 16 只"), bChildAtDeath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBalloonCombatTest, "MovingCastle.Support.BalloonCombat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBalloonCombatTest::RunTest(const FString& Parameters)
{
	using namespace CastleSupportTests;
	FFixture Game;
	FText Reason;
	const auto Color = ECastleTileColor::Yellow;
	if (!TestTrue(TEXT("准备真实地板"), Game.Ground(6, 4, Color, Reason))) { return false; }
	auto* Definition = LoadDefinition(TEXT("BalloonTower"));
	auto* Tower = Game.Place(Definition, FIntVector(0, 0, 0), 2, Color, 4, Reason);
	auto* Archer = Game.Place(LoadDefinition(TEXT("ArcherTower")), FIntVector(0, 1, 0), 1, Color, 3, Reason);
	if (!TestNotNull(TEXT("二级黄色空投"), Tower) || !TestNotNull(TEXT("落地范围内的箭塔"), Archer)) { AddError(Reason.ToString()); return false; }
	if (!Game.Support->QueueLaunch(*Tower)) { return false; }
	Game.Support->Tick(0.01f);
	uint64 Id = 0;
	FVector Balloon;
	if (!TestTrue(TEXT("气球起飞并成为仇恨目标"), Game.Support->FindBalloonTarget(Tower->GetActorLocation() + FVector(0, 0, 800), Id, Balloon))) { return false; }
	const FVector Below = Game.Board->GetGridWorldLocation(FIntVector(0, 0, 0));
	TArray<FCastleMassUnitHandle> Enemies;
	if (!TestTrue(TEXT("内外各一敌人"), Game.Enemies({FTransform(Below + FVector(0, 0, 150)), FTransform(Below + FVector(5000, 0, 0))}, Enemies, Reason))) { return false; }
	TestTrue(TEXT("非致命攻击不会破裂"), Game.Support->DamageBalloon(Id, 1.0f) && Game.Support->GetBalloonTarget(Id, Balloon));
	if (!TestTrue(TEXT("致命攻击使气球破裂"), Game.Support->DamageBalloon(Id, Definition->SupportData->BalloonHealth))) { return false; }
	TestFalse(TEXT("落下的货箱不可被锁定"), Game.Support->GetBalloonTarget(Id, Balloon));
	Game.Support->Tick(0.05f);
	TestEqual(TEXT("货箱还在空中，不结算"), Game.Vital(Enemies[0]).Health, 1000.0f);
	Game.Support->Tick(3.0f);
	TestEqual(TEXT("落地后只伤害范围内敌人"), Game.Vital(Enemies[0]).Health, 996.0f);
	TestEqual(TEXT("范围外敌人无伤害"), Game.Vital(Enemies[1]).Health, 1000.0f);
	TestTrue(TEXT("减速按卡面数字 4%"), FMath::IsNearlyEqual(Game.Vital(Enemies[0]).SupportMovementMultiplier, 0.96f));
	TestTrue(TEXT("黄气球按 4×2 给箭塔提速 8%"), FMath::IsNearlyEqual(Game.Support->GetArcherRateMultiplier(*Archer), 1.08f));
	TestEqual(TEXT("黄色不增加箭塔伤害"), Game.Support->GetTowerDamageMultiplier(*Archer), 1.0f);
	TestTrue(TEXT("结束战斗清理已有增减速"), Game.Support->EndBattleSupport());
	TestEqual(TEXT("不修改模板速度且不向下场带入减速"), Game.Vital(Enemies[0]).SupportMovementMultiplier, 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleHiveChainTest, "MovingCastle.Support.HiveReturnAndChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleHiveChainTest::RunTest(const FString& Parameters)
{
	using namespace CastleSupportTests;
	FFixture Game;
	FText Reason;
	const auto Color = ECastleTileColor::Red;
	if (!TestTrue(TEXT("生成直线房间链棋盘"), Game.Ground(8, 3, Color, Reason))) { return false; }
	auto* Hive = Game.Place(LoadDefinition(TEXT("BeeHive")), FIntVector(0, 0, 0), 3, Color, 8, Reason);
	if (!TestNotNull(TEXT("32 只的三级主巢"), Hive)) { AddError(Reason.ToString()); return false; }
	auto* Room = LoadDefinition(TEXT("NormalRoom"));
	const int32 Digits[] = {8, 7, 6, 4, 5};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Digits); ++Index)
	{
		if (!TestNotNull(TEXT("同色链路使用正式普通房间"), Game.Place(Room, FIntVector(Index + 2, 0, 0), 1, Color, Digits[Index], Reason)))
		{ AddError(Reason.ToString()); return false; }
	}
	TArray<FCastleMassUnitHandle> Enemies;
	const FVector Target = Hive->GetActorLocation() + FVector(600, 0, 650);
	if (!Game.Enemies({FTransform(Target)}, Enemies, Reason) || !Game.Support->QueueLaunch(*Hive)) { return false; }
	Game.Support->Tick(0.5f);
	TestEqual(TEXT("主巢首次只出 32 只"), Game.BeeCounts(), TArray<int32>({32}));
	Game.Support->Tick(10.0f);
	TestEqual(TEXT("到时开始返航，但返航途中没有备用蜂群"), Game.BeeCounts(), TArray<int32>({32}));
	Game.Support->Tick(1.0f);
	TestEqual(TEXT("主巢真正返回才出动 8 只"), Game.BeeCounts(), TArray<int32>({8}));
	Game.Vital(Enemies[0]).Health = 0.0f;
	Game.Support->Tick(1.0f);
	TestEqual(TEXT("8 返回后出动 7 只"), Game.BeeCounts(), TArray<int32>({7}));
	Game.Support->Tick(1.0f);
	TestEqual(TEXT("7 返回后出动 6 只"), Game.BeeCounts(), TArray<int32>({6}));
	Game.Support->Tick(1.0f);
	TestTrue(TEXT("4 打断链路，之后的 5 也不出蜂"), Game.BeeCounts().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleAirdropAmmunitionTest, "MovingCastle.Support.AirdropAmmunition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleAirdropAmmunitionTest::RunTest(const FString& Parameters)
{
	using namespace CastleSupportTests;
	FFixture Game;
	FText Reason;
	const auto Red = ECastleTileColor::Red;
	if (!TestTrue(TEXT("建立测试地面"), Game.Ground(7, 4, Red, Reason))) { return false; }
	auto* Tower = Game.Place(LoadDefinition(TEXT("BalloonTower")), FIntVector(0, 0, 0), 3, Red, 4, Reason);
	auto* Storage = Game.Place(LoadDefinition(TEXT("NormalRoom")), FIntVector(2, 0, 0), 1, Red, 4, Reason);
	auto* Cannon = Game.Place(LoadDefinition(TEXT("CannonTower")), FIntVector(3, 0, 0), 2, Red, 4, Reason);
	auto* Shield = Game.Place(LoadDefinition(TEXT("ShieldTower")), FIntVector(0, 2, 0), 1, Red, 5, Reason);
	if (!TestNotNull(TEXT("三级红色空投"), Tower) || !TestNotNull(TEXT("只把弹药库贴空投塔"), Storage)
		|| !TestNotNull(TEXT("炮塔离空投塔两格"), Cannon) || !TestNotNull(TEXT("范围内盾塔"), Shield)) { AddError(Reason.ToString()); return false; }
	TArray<FCastleMassUnitHandle> Enemies;
	const FVector Target = Game.Board->GetGridWorldLocation(FIntVector(0, 0, 0)) + FVector(0, 0, 410);
	if (!Game.Enemies({FTransform(Target)}, Enemies, Reason) || !Game.Support->QueueLaunch(*Tower)) { return false; }
	Game.Support->Tick(0.02f);
	uint64 Id = 0;
	FVector Location;
	if (!Game.Support->FindBalloonTarget(Target, Id, Location) || !Game.Support->PopBalloon(Id)) { return false; }
	Game.Support->Tick(3.0f);
	TestEqual(TEXT("仍按卡面 4 结算基础伤害"), Game.Vital(Enemies[0]).Health, 996.0f);
	TestTrue(TEXT("范围内防御塔取得四格总和 16% 的红色增伤"), FMath::IsNearlyEqual(Game.Support->GetTowerDamageMultiplier(*Shield), 1.16f));
	TestEqual(TEXT("范围外炮塔无红色增伤"), Game.Support->GetTowerDamageMultiplier(*Cannon), 1.0f);
	FMassEntityManager& Manager = Game.Manager();
	FMassEntityQuery Query(Manager.AsShared());
	Query.AddTagRequirement<FCastleMassProjectileTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassProjectileFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FCastleMassHomingProjectileFragment>(EMassFragmentAccess::ReadOnly);
	FMassExecutionContext Context(Manager, 0.0f, false);
	int32 Shots = 0;
	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk)
	{
		const auto Targets = Chunk.GetFragmentView<FCastleMassHomingProjectileFragment>();
		const auto ShotsData = Chunk.GetFragmentView<FCastleMassProjectileFragment>();
		const auto Positions = Chunk.GetFragmentView<FTransformFragment>();
		for (FMassExecutionContext::FEntityIterator It = Chunk.CreateEntityIterator(); It; ++It)
		{
			++Shots;
			TestEqual(TEXT("4 号弹药库精确提供一发 2 点炮击，不用炮塔总和"), ShotsData[It].Damage, 2.0f);
			TestTrue(TEXT("炮弹从气球落点而非炮塔处生成"), FVector::DistSquared2D(Positions[It].GetTransform().GetLocation(), Location) < 1.0);
			TestTrue(TEXT("炮弹锁定目标敌人"), Targets[It].Target == Enemies[0].Entity);
		}
	});
	TestEqual(TEXT("同座弹药库只发一枚炮弹"), Shots, 1);
	return true;
}

#endif
