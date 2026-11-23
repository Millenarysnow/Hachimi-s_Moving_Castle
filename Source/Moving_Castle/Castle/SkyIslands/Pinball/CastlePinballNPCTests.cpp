#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CastlePinballNPCConsoleComponent.h"
#include "CastlePinballNPCSimulationComponent.h"
#include "CastlePinballNPCSkyIsland.h"
#include "CastlePinballNPCStatusWidget.h"
#include "Castle/Components/CastleEconomyComponent.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastleBoard.h"
#include "CastlePlayerController.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

namespace
{
	void PinballAimAt(ACastlePlayerController& Controller, const USceneComponent& Target)
	{
		// 玩家可自由飞到物件正面。让视线水平，避免尚未刷新相机臂时从旧位置计算俯仰。
		const FVector Forward = Target.GetOwner()->GetActorForwardVector();
		Controller.GetPawn()->SetActorLocation(Target.GetComponentLocation() - Forward * 420.0 - FVector::UpVector * 64.0);
		Controller.SetControlRotation(Forward.Rotation());
	}

	void PinballPressKey(ACastlePlayerController& Controller, const FKey& Key)
	{
		Controller.InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.0f));
		Controller.InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.0f));
	}

	/** 在初始编辑阶段准备一枚真正由导演持有的 NPC 节点。只替换测试世界导演的配置引用。 */
	bool PreparePinballNPCNode(UCastleDirectorSubsystem& Director, FAutomationTestBase& Test)
	{
		UWorld* World = Director.GetWorld();
		const auto* Tower = World->GetSubsystem<UCastleMainTowerSubsystem>()->GetRegisteredMainTower();
		const TActorIterator<ACastleBoard> Board(World);
		auto* ActiveConfigProperty = FindFProperty<FObjectPropertyBase>(Director.GetClass(), TEXT("Config"));
		if (!Test.TestTrue(TEXT("导演配置属性、主塔和棋盘有效"), ActiveConfigProperty && Director.GetConfig() && Tower && Board))
		{
			return false;
		}
		auto* Config = DuplicateObject(Director.GetConfig(), World);
		Config->SetFlags(RF_Transient);
		Config->SkyIslandClasses.Reset();
		Config->SkyIslandClasses.Add(ECastleSkyIslandType::NPC, ACastlePinballNPCSkyIsland::StaticClass());
		// 导演按棋盘前弧的负角度向正角度发布节点，选择序号 0 并为它构造确定的 NPC 种子。
		TArray<ACastleDirectorNodeActor*> Nodes;
		for (TActorIterator<ACastleDirectorNodeActor> Node(World); Node; ++Node) { Nodes.Add(*Node); }
		if (!Test.TestTrue(TEXT("编辑阶段有正式节点"), !Nodes.IsEmpty())) { return false; }
		const FVector Center = Tower->GetTargetLocation();
		const double BoardYaw = Board->GetActorForwardVector().Rotation().Yaw;
		Nodes.Sort([Center, BoardYaw](const ACastleDirectorNodeActor& A, const ACastleDirectorNodeActor& B)
		{
			return FMath::FindDeltaAngleDegrees(BoardYaw, (A.GetActorLocation() - Center).Rotation().Yaw)
				< FMath::FindDeltaAngleDegrees(BoardYaw, (B.GetActorLocation() - Center).Rotation().Yaw);
		});
		int32 Seed = 0;
		for (;; ++Seed)
		{
			FRandomStream Choice(Seed);
			if (Choice.RandRange(0, 2) == static_cast<int32>(ECastleSkyIslandType::NPC)) { break; }
		}
		const uint32 StepSeed = static_cast<uint32>(Director.GetNextStep()) * 0x9e3779b9u;
		Config->RunSeed = static_cast<int32>(static_cast<uint32>(Seed) ^ StepSeed);
		if (!Test.TestTrue(TEXT("布置真实导演持有的 NPC 奖励节点"),
			Nodes[0]->Configure(*Config, Director.GetNextStep(), ECastleDirectorNodeKind::RewardIsland)))
		{
			return false;
		}
		ActiveConfigProperty->SetObjectPropertyValue_InContainer(&Director, Config);
		Director.HandleNodeTouched(Nodes[0]);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePinballSimulationTest, "MovingCastle.SkyIslands.Pinball.FixedStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePinballSimulationTest::RunTest(const FString&)
{
	using namespace CastlePinball;
	auto* Config = NewObject<UCastlePinballNPCSkyIslandDataAsset>();
	auto* Simulation = NewObject<UCastlePinballNPCSimulationComponent>();
	Config->Pegs.Reset();
	Config->MaxActiveCoins = 10;
	Config->MultiplierGates = {
		{{-420.0, 0.0}, 300.0, ECastlePinballGateMode::Multiply, 2},
		{{-390.0, 0.0}, 300.0, ECastlePinballGateMode::Add, 2}
	};
	FText Reason;
	if (!TestTrue(TEXT("初始化确定性测试场地"), Simulation->Initialize(*Config, 17, Reason))) { return false; }
	Simulation->CommitLaunch(0.0, 0.0);
	Simulation->Coins[0].Position.X = -421.0;
	const auto Step = [&]
	{
		if (!TestTrue(TEXT("斜面步进成功"), Simulation->Advance(FixedStep, Reason))) { AddError(Reason.ToString()); }
	};
	Step();
	TestEqual(TEXT("x2 只生成一枚额外金币"), Simulation->Coins.Num(), 2);
	TestTrue(TEXT("父金币标记当前门"), Simulation->Coins[0].TriggeredGates.Contains(0));
	TestTrue(TEXT("子金币继承当前门标记"), Simulation->Coins[1].TriggeredGates.Contains(0));

	Simulation->Coins[0].Position.X = -420.5;
	Simulation->Coins[1].Position.X = -420.5;
	Step();
	TestEqual(TEXT("父子重穿同一门均不增殖"), Simulation->Coins.Num(), 2);
	Simulation->Coins[0].Position.X = -391.0;
	Step();
	TestEqual(TEXT("+2 精确生成两枚额外金币"), Simulation->Coins.Num(), 4);
	Simulation->Coins[0].Position.X = -391.0;
	Step();
	TestEqual(TEXT("+2 门的同一金币也只增殖一次"), Simulation->Coins.Num(), 4);

	Config->MultiplierGates[1].Value = 20;
	Simulation->Coins[1].Position.X = -391.0;
	AddExpectedMessagePlain(TEXT("达到在场金币上限"), ELogVerbosity::Warning);
	Step();
	TestEqual(TEXT("触顶保留原有金币并止于上限"), Simulation->Coins.Num(), 10);
	TestTrue(TEXT("触顶前的第一枚金币仍在"), Simulation->Coins[0].Id == 1);
	TestTrue(TEXT("触顶的门也被标记为已触发"), Simulation->Coins[1].TriggeredGates.Contains(1));

	// 用相同初态比较低帧率与高帧率结果，包括门与柱。直接步进不需要渲染或 Chaos。
	Config->MaxActiveCoins = 200;
	Config->MultiplierGates[1].Value = 2;
	Config->Pegs = {{{-355.0, 11.0}, 19.0}};
	auto* LowRate = NewObject<UCastlePinballNPCSimulationComponent>();
	auto* HighRate = NewObject<UCastlePinballNPCSimulationComponent>();
	if (!LowRate->Initialize(*Config, 81, Reason) || !HighRate->Initialize(*Config, 81, Reason))
	{
		AddError(Reason.ToString());
		return false;
	}
	LowRate->CommitLaunch(0.0, 0.0);
	HighRate->CommitLaunch(0.0, 0.0);
	TestTrue(TEXT("单个低帧率大步"), LowRate->Advance(0.5, Reason));
	for (int32 Index = 0; Index < 60; ++Index)
	{
		if (!TestTrue(TEXT("高帧率小步"), HighRate->Advance(FixedStep, Reason))) { return false; }
	}
	if (!TestEqual(TEXT("帧分块不改变金币数量"), LowRate->GetCoins().Num(), HighRate->GetCoins().Num())) { return false; }
	TestTrue(TEXT("确实已触发过增殖"), LowRate->GetCoins().Num() > 1);
	TestTrue(TEXT("反弹钉让居中直投产生横向反弹"), FMath::Abs(LowRate->GetCoins()[0].Velocity.Y) > 10.0);
	for (int32 Index = 0; Index < LowRate->GetCoins().Num(); ++Index)
	{
		const FCastlePinballCoin& A = LowRate->GetCoins()[Index];
		const FCastlePinballCoin& B = HighRate->GetCoins()[Index];
		TestTrue(TEXT("帧分块不改变位置、旋转或速度"),
			A.Id == B.Id && A.Position.Equals(B.Position) && A.Velocity.Equals(B.Velocity) && A.Rotation.Equals(B.Rotation));
	}
	// 让一枚金币从边界内侧出发，验证步内越过回收线后立即消失。
	TestTrue(TEXT("重新初始化回收测试场地"), HighRate->Initialize(*Config, 81, Reason));
	HighRate->CommitLaunch(0.0, 0.0);
	HighRate->Coins[0].Position.X = HalfLength - 1.0;
	TestTrue(TEXT("推进到底端边界"), HighRate->Advance(FixedStep, Reason));
	TestTrue(TEXT("越过底端回收线立即销毁"), HighRate->GetCoins().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePinballIslandTest, "MovingCastle.SkyIslands.Pinball.IslandLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePinballIslandTest::RunTest(const FString&)
{
	// 明确启动单人 PIE。引擎通用地图等待器还要求编辑器窗口状态，就绪后的离屏 PIE 也可能被它判超时。
	const FString Map = FPackageName::LongPackageNameToFilename(TEXT("/Game/MovingCastle/Maps/MainMap"),
		FPackageName::GetMapPackageExtension());
	if (!TestTrue(TEXT("加载 MainMap"), FEditorFileUtils::LoadMap(Map, false, false))) { return false; }
	auto* PlaySettings = DuplicateObject(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
	PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
	PlaySettings->SetRunUnderOneProcess(true);
	PlaySettings->SetPlayNumberOfClients(1);
	FRequestPlaySessionParams PlayRequest;
	PlayRequest.EditorPlaySettings = PlaySettings;
	GEditor->RequestPlaySession(PlayRequest);
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Started = FPlatformTime::Seconds(), Stage = 0,
		ReturnLocation = FVector::ZeroVector, BeforeStep = 0, BeforeNPCCount = 0,
		VisitedIsland = TWeakObjectPtr<ACastlePinballNPCSkyIsland>(), CoinBody = TWeakObjectPtr<UStaticMeshComponent>()]() mutable
	{
		UWorld* World = GEditor->PlayWorld;
		auto* Director = World ? World->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
		auto* Controller = World ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Director || !Director->IsRunning() || !Controller)
		{
			if (FPlatformTime::Seconds() - Started < 30.0) { return false; }
			AddError(TEXT("MainMap 的正式导演或玩家控制器未能开始运行。"));
			return true;
		}
		FText Reason;
		const FKey InteractKey = Director->GetConfig()->SkyIslandInteractKey;
		const TActorIterator<ACastlePinballNPCSkyIsland> IslandIterator(World);
		auto* Island = IslandIterator ? *IslandIterator : nullptr;
		if (Stage == 0)
		{
			ReturnLocation = Controller->GetPawn()->GetActorLocation();
			BeforeStep = Director->GetClearedSteps();
			TestTrue(TEXT("控制台找到弹珠机验收命令"), IConsoleManager::Get().ProcessUserConsoleInput(
				TEXT("CastleTestNPCPinballIsland"), *GLog, World));
			++Stage;
			return false;
		}
		if (Stage == 1)
		{
			if (!TestNotNull(TEXT("命令生成验收岛"), Island)) { return true; }
			TestTrue(TEXT("入口命令真实传送玩家"), Controller->GetPawn()->GetActorLocation().Equals(
				Island->GetPlayerArrivalTransform().GetLocation(), 5.0));
			PinballAimAt(*Controller, *Island->Console);
			++Stage;
			return false;
		}
		if (Stage == 2 || Stage == 4 || Stage == 7 || Stage == 10)
		{
			PinballPressKey(*Controller, InteractKey);
			++Stage;
			return false;
		}
		if (Stage == 3)
		{
			if (!TestTrue(TEXT("命令传送后 E 键打开实际 WBP"), Island && Island->StatusWidget && Island->AcceptanceInput))
			{
				return true;
			}
			// 先退出瞄准，再飞到传送门视线。命令自己的输入层继续处理下一次 E。
			PinballPressKey(*Controller, InteractKey);
			auto* Portal = Island->FindComponentByClass<UCastleSkyIslandDepartureComponent>();
			if (!TestNotNull(TEXT("验收岛的真实传送门"), Portal)) { return true; }
			PinballAimAt(*Controller, *Portal);
			++Stage;
			return false;
		}
		if (Stage == 5)
		{
			if (Island && FPlatformTime::Seconds() - Started < 30.0) { return false; }
			TestFalse(TEXT("命令离岛销毁验收岛"), IsValid(Island));
			TestTrue(TEXT("命令离岛返回原位置"), Controller->GetPawn()->GetActorLocation().Equals(ReturnLocation, 5.0));
			TestEqual(TEXT("命令离岛不消耗导演关数"), Director->GetClearedSteps(), BeforeStep);
			TestFalse(TEXT("命令离岛恢复飞行观察"), Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored());
			if (!PreparePinballNPCNode(*Director, *this)) { return true; }
			++Stage;
			return false;
		}
		if (Stage == 6)
		{
			if (!Island && FPlatformTime::Seconds() - Started < 30.0) { return false; }
			if (!TestNotNull(TEXT("真实导演生成 NPC 空岛"), Island)) { return true; }
			TestEqual(TEXT("导演进入正式空岛阶段"), Director->GetPhase(), ECastleDirectorPhase::SkyIsland);
			TestTrue(TEXT("正式导演传送玩家"), Controller->GetPawn()->GetActorLocation().Equals(
				Island->GetPlayerArrivalTransform().GetLocation(), 5.0));
			VisitedIsland = Island;
			PinballAimAt(*Controller, *Island->Console);
			++Stage;
			return false;
		}
		if (Stage == 9)
		{
			TestFalse(TEXT("退出投币模式移除 HUD"), Island && Island->StatusWidget);
			auto* Portal = Island ? Island->FindComponentByClass<UCastleSkyIslandDepartureComponent>() : nullptr;
			if (!TestNotNull(TEXT("正式空岛的真实传送门"), Portal)) { return true; }
			PinballAimAt(*Controller, *Portal);
			++Stage;
			return false;
		}
		if (Stage == 11)
		{
			if (VisitedIsland.IsValid() && FPlatformTime::Seconds() - Started < 30.0) { return false; }
			TestFalse(TEXT("导演离岛后自动销毁空岛"), VisitedIsland.IsValid());
			TestTrue(TEXT("导演离岛回收金币组件"), !CoinBody.IsValid() || !CoinBody->IsRegistered());
			TestEqual(TEXT("导演离岛恢复编辑并提交一步"), Director->GetPhase(), ECastleDirectorPhase::Editing);
			TestEqual(TEXT("导演离岛仅提交一步"), Director->GetClearedSteps(), BeforeStep + 1);
			TestEqual(TEXT("离岛保留所有已招募的 NPC"), Director->GetRunNPCCount(), BeforeNPCCount);
			TestTrue(TEXT("导演离岛返回入岛前位置"), Controller->GetPawn()->GetActorLocation().Equals(ReturnLocation, 5.0));
			TestTrue(TEXT("导演离岛恢复移动、观察并生成后续节点"),
				!Controller->IsMoveInputIgnored() && !Controller->IsLookInputIgnored()
				&& static_cast<bool>(TActorIterator<ACastleDirectorNodeActor>(World)));
			return true;
		}
		if (!TestTrue(TEXT("正式 E 键交互显示 WBP 并接管输入"),
			Island && Island->StatusWidget && Controller->IsMoveInputIgnored() && Controller->IsLookInputIgnored()))
		{
			return true;
		}
		auto* Economy = Controller->GetEconomy();
		auto* Simulation = Island->Simulation.Get();
		const int32 Cost = Island->Config->CoinLaunchCost;
		const int32 StartGold = Economy->GetGold();
		TestTrue(TEXT("准备一笔投币余额"), StartGold >= Cost || Economy->TryAddGold(Cost - StartGold, Reason));
		const int32 Funded = Economy->GetGold();
		TestTrue(TEXT("扣费并发射"), Island->TryLaunchCoin(Reason));
		TestEqual(TEXT("账户恰好扣除一笔"), Economy->GetGold(), Funded - Cost);
		TestEqual(TEXT("扣费只发射一枚"), Simulation->Coins.Num(), 1);
		TestTrue(TEXT("耗尽余额以测试拒绝"), Economy->TrySpendGold(Economy->GetGold(), Reason));
		AddExpectedMessagePlain(TEXT("拒绝投币"), ELogVerbosity::Warning);
		TestFalse(TEXT("余额不足拒绝投币"), Island->TryLaunchCoin(Reason));
		TestFalse(TEXT("余额不足有提示"), Reason.IsEmpty());
		TestEqual(TEXT("余额不足不会透支"), Economy->GetGold(), 0);
		TestEqual(TEXT("余额不足不生成金币"), Simulation->Coins.Num(), 1);

		// 金币放在目标圆前，后续只通过正式的斜面步进触发命中。
		const int32 InitialNPCCount = Director->GetRunNPCCount();
		const auto AimHit = [&](int32 Index)
		{
			Simulation->CommitLaunch(0.0, Island->LauncherAngle);
			FCastlePinballCoin& Coin = Simulation->Coins.Last();
			Coin.Position = Simulation->NPCs[Index].Position - FVector2D(CastlePinball::NPCRadius + CastlePinball::CoinRadius + 1.0, 0.0);
			Coin.Velocity = FVector2D(380.0, 0.0);
			TestTrue(TEXT("推进金币命中 NPC"), Simulation->Advance(CastlePinball::FixedStep, Reason));
		};
		for (int32 Hit = 1; Hit <= Island->Config->CoinsRequiredPerNPC; ++Hit)
		{
			AimHit(0);
			TestEqual(TEXT("命中消耗对应金币"), Simulation->Coins.Num(), 1);
			TestEqual(TEXT("命中计数恰好增加一次"), Simulation->NPCs[0].Hits, Hit);
			TestEqual(TEXT("仅达阈值时增加待命人数"), Director->GetRunNPCCount(),
				InitialNPCCount + (Hit == Island->Config->CoinsRequiredPerNPC ? 1 : 0));
		}
		AimHit(0);
		TestEqual(TEXT("已招募 NPC 再遇金币不重复结算"), Director->GetRunNPCCount(), InitialNPCCount + 1);
		TestEqual(TEXT("已招募 NPC 不再消耗金币"), Simulation->Coins.Num(), 2);

		for (int32 Index = 1; Index < Simulation->NPCs.Num(); ++Index)
		{
			for (int32 Hit = 0; Hit < Island->Config->CoinsRequiredPerNPC; ++Hit) { AimHit(Index); }
		}
		TestTrue(TEXT("全员招募后仍为玩家准备投币余额"), Economy->TryAddGold(Cost, Reason));
		TestFalse(TEXT("全招满后拒绝投币"), Island->TryLaunchCoin(Reason));
		TestEqual(TEXT("全招满后余额不变"), Economy->GetGold(), Cost);
		TestEqual(TEXT("所有 NPC 各结算一次"), Director->GetRunNPCCount(), InitialNPCCount + Island->Config->InitialNPCCount);
		BeforeNPCCount = Director->GetRunNPCCount();

		Island->SyncScene();
		TestTrue(TEXT("销毁前仍有金币组件"), !Island->CoinBodies.IsEmpty());
		if (!Island->CoinBodies.IsEmpty()) { CoinBody = Island->CoinBodies.CreateConstIterator().Value().Get(); }
		PinballPressKey(*Controller, InteractKey);
		++Stage;
		return false;
	}));
	// 等待 PIE 完整退出后再交给同一次运行中的下一项测试。
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([bRequested = false]() mutable
	{
		if (!GEditor->PlayWorld) { return true; }
		if (!bRequested)
		{
			GEditor->RequestEndPlayMap();
			bRequested = true;
		}
		return false;
	}));
	return true;
}

#endif
