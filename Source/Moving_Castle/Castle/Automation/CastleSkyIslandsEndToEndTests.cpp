#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Tests/AutomationCommon.h"
#include "Castle/Dialogue/CastleDialogueScriptDataAsset.h"
#include "Castle/Dialogue/CastleDialogueSubsystem.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/SkyIslands/Gamble/CastleGambleSkyIsland.h"
#include "Castle/SkyIslands/Pinball/CastlePinballNPCConsoleComponent.h"
#include "Castle/SkyIslands/Pinball/CastlePinballNPCSimulationComponent.h"
#include "Castle/SkyIslands/Pinball/CastlePinballNPCSkyIsland.h"
#include "Castle/SkyIslands/Rest/CastleRestSkyIsland.h"
#include "Castle/SkyIslands/Shop/CastleShopSkyIsland.h"
#include "CastleBoard.h"
#include "CastleEconomyComponent.h"
#include "CastleMainTowerComponent.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "FileHelpers.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/Package.h"

namespace
{
	constexpr TCHAR MainMapPath[] = TEXT("/Game/MovingCastle/Maps/MainMap");
	constexpr TCHAR DirectorConfigPath[] = TEXT("/Game/MovingCastle/Data/Director/DA_DirectorConfig.DA_DirectorConfig");

	struct FIslandVisit
	{
		ECastleSkyIslandType Type;
		const TCHAR* Command;
		const TCHAR* ArtMeshName;
		UClass* (*GetClass)();
	};

	// 先验证首步没有空岛节点时也能直入，再让四次访问共用同一个正式单局。
	const FIslandVisit Visits[] = {
		{ECastleSkyIslandType::Rest, TEXT("CastleEnterSkyIsland Rest"), TEXT("SM_SkyIsland_Rest"), &ACastleRestSkyIsland::StaticClass},
		{ECastleSkyIslandType::Shop, TEXT("CastleEnterSkyIsland Shop"), TEXT("SM_SkyIsland_Shop"), &ACastleShopSkyIsland::StaticClass},
		{ECastleSkyIslandType::NPC, TEXT("CastleEnterSkyIsland NPC"), TEXT("SM_SkyIsland_PinballNPC"), &ACastlePinballNPCSkyIsland::StaticClass},
		{ECastleSkyIslandType::Gamble, TEXT("CastleEnterSkyIsland Gamble"), TEXT("SM_SkyIsland_Gamble"), &ACastleGambleSkyIsland::StaticClass}
	};
	// 三个奖励岛各重访一次，继续复用同一 GameInstance 验证已读标记。
	constexpr int32 FirstVisitCount = UE_ARRAY_COUNT(Visits);
	constexpr int32 TotalVisitCount = FirstVisitCount + 3;

	/** 通过实际控制台、碰撞射线和 PIE Tick 联调，测试只提供可重复的交互目标和按键。 */
	class FVisitSkyIslandsInPIE final : public IAutomationLatentCommand
	{
	public:
		explicit FVisitSkyIslandsInPIE(FAutomationTestBase& InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = GEditor->PlayWorld;
			auto* Director = World ? World->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
			if (!Director || !Director->IsRunning())
			{
				if (FPlatformTime::Seconds() < Deadline) { return false; }
				Test.AddError(TEXT("MainMap PIE 的正式导演没有启动。"));
				return true;
			}
			auto* Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
			TActorIterator<ACastleBoard> Board(World);
			if (!Test.TestTrue(TEXT("MainMap 有实际玩家、建造面、金币账户和商店"),
				Controller && Controller->GetPawn() && Controller->GetEconomy() && Controller->GetShop() && Board))
			{
				return true;
			}

			switch (Stage)
			{
			case EStage::Enter:
				return !Enter(*Director, *Controller, **Board);
			case EStage::PinballLaunch:
				return UpdatePinballLaunch(*Controller);
			case EStage::PinballExit:
				if (Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored())
				{
					return WaitForTick(TEXT("弹珠机未处理 E 键退出瞄准。"));
				}
				return !RequestDeparture(*Director, *Controller);
			case EStage::Return:
				if (Director->GetPhase() == ECastleDirectorPhase::SkyIsland)
				{
					return WaitForTick(TEXT("导演 Tick 未处理离开传送门请求。"));
				}
				VerifyReturn(*Director, *Controller, **Board);
				Stage = EStage::Enter;
				return ++VisitIndex == TotalVisitCount;
			}
			return true;
		}

	private:
		enum class EStage { Enter, PinballLaunch, PinballExit, Return };
		const FIslandVisit& GetVisit() const
		{
			return Visits[VisitIndex < FirstVisitCount ? VisitIndex : VisitIndex - FirstVisitCount + 1];
		}

		bool Enter(UCastleDirectorSubsystem& Director, ACastlePlayerController& Controller, ACastleBoard& Board)
		{
			UWorld* World = Director.GetWorld();
			const auto& Visit = GetVisit();
			auto* Dialogue = World->GetGameInstance()->GetSubsystem<UCastleDialogueSubsystem>();
			if (!Test.TestNotNull(TEXT("实际对话子系统"), Dialogue)) { return false; }
			Test.TestEqual(TEXT("PIE 世界是正式 MainMap"), UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), FString(MainMapPath));
			if (!Test.TestTrue(TEXT("导演使用磁盘正式配置"), Director.GetConfig()
				&& Director.GetConfig()->GetPathName() == DirectorConfigPath))
			{
				return false;
			}
			Test.TestEqual(TEXT("每次从正常编辑阶段进入"), Director.GetPhase(), ECastleDirectorPhase::Editing);
			Test.TestEqual(TEXT("前序访问各提交一次"), Director.GetClearedSteps(), VisitIndex);
			BeforeLocation = Controller.GetPawn()->GetActorLocation();
			BeforeView = Controller.GetControlRotation();
			PreviousNodes.Reset();
			for (TActorIterator<ACastleDirectorNodeActor> Node(World); Node; ++Node) { PreviousNodes.Add(*Node); }
			Test.TestTrue(TEXT("当前关有正式节点"), PreviousNodes.Num() >= 2);

			if (VisitIndex == 0)
			{
				Controller.ConsoleCommand(TEXT("CastleResetSkyIslandDialogues"));
				Test.AddExpectedError(TEXT("用法：CastleEnterSkyIsland"), EAutomationExpectedErrorFlags::Contains, 1);
				RunCommand(*World, TEXT("CastleEnterSkyIsland Invalid"));
				Test.TestEqual(TEXT("非法参数保留编辑阶段"), Director.GetPhase(), ECastleDirectorPhase::Editing);
				Test.TestFalse(TEXT("非法参数没有生成岛"), static_cast<bool>(TActorIterator<ACastleSkyIslandBase>(World)));
			}
			RunCommand(*World, Visit.Command);
			TActorIterator<ACastleSkyIslandBase> Island(World);
			if (!Test.TestTrue(FString::Printf(TEXT("%s 生成正式空岛"), Visit.Command), static_cast<bool>(Island)))
			{
				return false;
			}
			ActiveIsland = *Island;
			if (!Test.TestEqual(TEXT("场上岛屿属于指定正式类"), Island->GetClass(), Visit.GetClass())) { return false; }
			// 从正式生成的 Actor 读取可见网格，捕捉岛型配错、重复换算厘米及装饰挡住射线的回归。
			auto* Art = FindObjectFast<UStaticMeshComponent>(ActiveIsland.Get(), TEXT("IslandArt"));
			if (!Test.TestTrue(TEXT("岛体挂载了对应的可见美术网格"), Art && Art->IsVisible() && Art->GetStaticMesh()
				&& Art->GetStaticMesh()->GetName() == Visit.ArtMeshName)) { return false; }
			const FVector Span = Art->GetStaticMesh()->GetBoundingBox().GetSize() * Art->GetRelativeScale3D();
			Test.TestTrue(TEXT("岛面直径约 24 米且中心与岛根对齐"), FMath::IsNearlyEqual(Span.X, 2400.0, 20.0)
				&& FMath::IsNearlyEqual(Span.Y, 2400.0, 20.0) && Art->GetRelativeLocation().IsNearlyZero());
			Test.TestEqual(TEXT("整岛装饰让交互组件接收射线"), Art->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
			++Island;
			Test.TestFalse(TEXT("重复或上次空岛没有残留"), static_cast<bool>(Island));
			Test.TestEqual(TEXT("进入空岛阶段"), Director.GetPhase(), ECastleDirectorPhase::SkyIsland);
			Test.TestEqual(TEXT("入岛尚未提交步数"), Director.GetClearedSteps(), VisitIndex);
			Test.TestTrue(TEXT("玩家已传送至空岛锚点"), Controller.GetPawn()->GetActorLocation().Equals(
				ActiveIsland->GetPlayerArrivalTransform().GetLocation(), 5.0f));
			Test.TestTrue(TEXT("建造面已隐藏"), Board.IsPlacementSurfacePresentationHidden());
			Test.TestFalse(TEXT("旧节点已从世界移除"), static_cast<bool>(TActorIterator<ACastleDirectorNodeActor>(World)));

			if (VisitIndex == 0)
			{
				Test.AddExpectedError(TEXT("只有可操作的编辑阶段"), EAutomationExpectedErrorFlags::Contains, 1);
				RunCommand(*World, TEXT("CastleEnterSkyIsland Shop"));
				Test.TestTrue(TEXT("岛上不能开启第二次访问"), ActiveIsland.IsValid()
					&& Director.GetPhase() == ECastleDirectorPhase::SkyIsland && Director.GetClearedSteps() == 0);
			}
			if (Visit.Type == ECastleSkyIslandType::Rest || VisitIndex >= FirstVisitCount)
			{
				if (!Test.TestFalse(TEXT("休息岛或重访没有对话"), Dialogue->IsDialoguePlaying())) { return false; }
				if (VisitIndex >= FirstVisitCount) { return RequestDeparture(Director, Controller); }
			}
			else
			{
				const auto* Expected = Director.GetConfig()->SkyIslandArrivalDialogues.FindRef(Visit.Type).Get();
				if (!Test.TestTrue(TEXT("首次进岛播放对应剧本并锁定输入"), Expected
					&& Dialogue->GetCurrentScript() == Expected && Controller.IsModalInputActive())) { return false; }
				for (int32 Line = 0; Line < Expected->Lines.Num(); ++Line) { Dialogue->AdvanceDialogueForAutomation(); }
				if (!Test.TestFalse(TEXT("完整推进剧情后恢复交互"), Dialogue->IsDialoguePlaying() || Controller.IsModalInputActive()))
				{
					return false;
				}
			}
			return InteractWithIsland(Visit.Type, Director, Controller);
		}

		bool InteractWithIsland(ECastleSkyIslandType Type, UCastleDirectorSubsystem& Director, ACastlePlayerController& Controller)
		{
			auto& Island = *ActiveIsland.Get();
			auto* Economy = Controller.GetEconomy();
			const int32 GoldBefore = Economy->GetGold();
			switch (Type)
			{
			case ECastleSkyIslandType::Rest:
			{
				auto* Gold = FindObjectFast<UCastleRestSkyIslandStation>(&Island, TEXT("GoldStation"));
				auto* Shop = FindObjectFast<UCastleRestSkyIslandStation>(&Island, TEXT("ShopStation"));
				if (!Interact(Director, Gold, TEXT("休息岛金币台"))
					|| !Interact(Director, Shop, TEXT("休息岛免费商店台"))) { return false; }
				Test.TestTrue(TEXT("领取金币写入本局账户"), Economy->GetGold() > GoldBefore);
				Test.TestTrue(TEXT("休息岛打开真实房间商店"), Controller.GetShop()->IsShopOpen());
				Controller.GetShop()->CloseShop();
				break;
			}
			case ECastleSkyIslandType::Shop:
			{
				// 生产抽样保证第二台是徽章，徽章直接结算且能通过本局库存观察购买结果。
				auto* Badges = UCastleBadgeSubsystem::GetForWorld(Island.GetWorld());
				auto* Offer = FindObjectFast<UCastleShopSkyIslandOffer>(&Island, TEXT("Offer2"));
				if (!Test.TestNotNull(TEXT("正式徽章子系统"), Badges)) { return false; }
				const int32 Before = Badges->GetBadges().Num();
				if (!Interact(Director, Offer, TEXT("商店岛徽章货位"))) { return false; }
				Test.TestEqual(TEXT("购买发放一枚徽章"), Badges->GetBadges().Num(), Before + 1);
				Test.TestTrue(TEXT("购买已扣除金币"), Economy->GetGold() < GoldBefore);
				break;
			}
			case ECastleSkyIslandType::NPC:
				if (!Interact(Director, Island.FindComponentByClass<UCastlePinballNPCConsoleComponent>(),
					TEXT("NPC 岛弹珠机控制台"))) { return false; }
				Test.TestTrue(TEXT("瞄准实际锁定移动和视角"), Controller.IsMoveInputIgnored() && Controller.IsLookInputIgnored());
				PinballStartingGold = GoldBefore;
				// 按键由玩家控制器的正常输入栈和下一帧 PIE Tick 消费。
				SendKey(Controller, EKeys::SpaceBar, IE_Pressed);
				Stage = EStage::PinballLaunch;
				Deadline = FPlatformTime::Seconds() + 5.0;
				return true;
			case ECastleSkyIslandType::Gamble:
			{
				const auto* Gamble = CastChecked<ACastleGambleSkyIsland>(&Island);
				auto* Recycler = FindObjectFast<UCastleGambleMachineComponent>(&Island, TEXT("BalloonRecycler"));
				auto* Tower = Island.GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>()->GetRegisteredMainTower();
				if (!Test.TestNotNull(TEXT("赌博岛绑定正式主塔"), Tower)) { return false; }
				int32 CountBefore = 0;
				int32 Maximum = 0;
				Tower->GetBalloonCounts(CountBefore, Maximum);
				if (!Interact(Director, Recycler, TEXT("赌博岛气球回收机"))) { return false; }
				int32 CountAfter = 0;
				Tower->GetBalloonCounts(CountAfter, Maximum);
				Test.TestEqual(TEXT("回收消耗主塔一个气球"), CountAfter, CountBefore - 1);
				Test.TestEqual(TEXT("回收金币进入本局账户"), Economy->GetGold(), GoldBefore + Gamble->GetGambleConfig()->BalloonRecycleGold);
				break;
			}
			}
			return RequestDeparture(Director, Controller);
		}

		bool UpdatePinballLaunch(ACastlePlayerController& Controller)
		{
			if (Controller.GetEconomy()->GetGold() == PinballStartingGold)
			{
				return WaitForTick(TEXT("弹珠机未处理空格投币。"));
			}
			auto* Simulation = ActiveIsland.IsValid() ? ActiveIsland->FindComponentByClass<UCastlePinballNPCSimulationComponent>() : nullptr;
			Test.TestTrue(TEXT("投币已扣款并生成一枚实际弹珠"),
				Controller.GetEconomy()->GetGold() < PinballStartingGold && Simulation && Simulation->GetCoins().Num() == 1);
			SendKey(Controller, EKeys::SpaceBar, IE_Released);
			SendKey(Controller, EKeys::E, IE_Pressed);
			Stage = EStage::PinballExit;
			Deadline = FPlatformTime::Seconds() + 5.0;
			return false;
		}

		bool RequestDeparture(UCastleDirectorSubsystem& Director, ACastlePlayerController& Controller)
		{
			if (GetVisit().Type == ECastleSkyIslandType::NPC) { SendKey(Controller, EKeys::E, IE_Released); }
			if (!Test.TestTrue(TEXT("交互离开门时正式空岛仍存在"), ActiveIsland.IsValid())) { return false; }
			if (!Interact(Director, ActiveIsland->FindComponentByClass<UCastleSkyIslandDepartureComponent>(),
				TEXT("离开传送门"))) { return false; }
			Test.TestEqual(TEXT("交互回调返回时岛仍存活，销毁延迟至 Tick"), Director.GetPhase(), ECastleDirectorPhase::SkyIsland);
			Stage = EStage::Return;
			Deadline = FPlatformTime::Seconds() + 5.0;
			return true;
		}

		void VerifyReturn(UCastleDirectorSubsystem& Director, ACastlePlayerController& Controller, ACastleBoard& Board)
		{
			UWorld* World = Director.GetWorld();
			Test.TestEqual(TEXT("离岛恢复编辑阶段"), Director.GetPhase(), ECastleDirectorPhase::Editing);
			Test.TestEqual(TEXT("离岛恰好推进一关"), Director.GetClearedSteps(), VisitIndex + 1);
			Test.TestTrue(TEXT("返回入岛前的位置和视角"), Controller.GetPawn()->GetActorLocation().Equals(BeforeLocation, 5.0f)
				&& Controller.GetControlRotation().Equals(BeforeView, 1.0f));
			Test.TestFalse(TEXT("建造面恢复可见"), Board.IsPlacementSurfacePresentationHidden());
			Test.TestFalse(TEXT("玩家输入和临时商店已关闭"), Controller.IsMoveInputIgnored()
				|| Controller.IsLookInputIgnored() || Controller.GetShop()->IsShopOpen());
			FText Reason;
			Test.TestTrue(TEXT("离岛后恢复主塔常驻商店入口"), Director.IsShopAccessAllowed(Reason));
			Test.TestFalse(TEXT("上次空岛 Actor 及世界中的空岛实例均已清除"), ActiveIsland.IsValid()
				|| static_cast<bool>(TActorIterator<ACastleSkyIslandBase>(World)));
			TArray<ECastleDirectorNodeKind> Expected = Director.GetNodeKindsForStep(*Director.GetConfig(), Director.GetNextStep());
			for (TActorIterator<ACastleDirectorNodeActor> Node(World); Node; ++Node)
			{
				Test.TestTrue(TEXT("下一关节点已激活且有正式标签"), !Node->IsHidden() && Node->GetActorEnableCollision() && Node->IsLabelReady());
				Test.TestFalse(TEXT("下一关不复用已销毁节点"), PreviousNodes.Contains(*Node));
				Test.TestTrue(TEXT("节点类型符合正式关数与种子"), Expected.RemoveSingle(Node->GetKind()) == 1);
			}
			Test.TestTrue(TEXT("下一关的完整节点批次已生成"), Expected.IsEmpty());
		}

		bool Interact(UCastleDirectorSubsystem& Director, const USceneComponent* Target, const TCHAR* Name)
		{
			if (!Test.TestNotNull(Name, Target)) { return false; }
			// 玩家可以在岛上飞行，离屏时从目标上方模拟准星射线，避免其它机台挡在离开门前。
			FText Reason;
			const FVector Location = Target->GetComponentLocation();
			const bool bInteracted = Director.TryInteractWithSkyIsland(Location + FVector::UpVector * 450.0, Location, Reason);
			return Test.TestTrue(FString::Printf(TEXT("交互 %s %s"), Name, *Reason.ToString()), bInteracted);
		}

		void RunCommand(UWorld& World, const TCHAR* Command)
		{
			Test.TestTrue(Command, IConsoleManager::Get().ProcessUserConsoleInput(Command, *GLog, &World));
		}

		static void SendKey(ACastlePlayerController& Controller, const FKey& Key, EInputEvent Event)
		{
			Controller.InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Released ? 0.0f : 1.0f));
		}

		/** 仅等待实际世界推进。超时写入节点或输入链的具体失败原因。 */
		bool WaitForTick(const TCHAR* Failure)
		{
			if (FPlatformTime::Seconds() < Deadline) { return false; }
			Test.AddError(Failure);
			return true;
		}

		FAutomationTestBase& Test;
		EStage Stage = EStage::Enter;
		int32 VisitIndex = 0;
		int32 PinballStartingGold = 0;
		double Deadline = FPlatformTime::Seconds() + 30.0;
		FVector BeforeLocation;
		FRotator BeforeView;
		TWeakObjectPtr<ACastleSkyIslandBase> ActiveIsland;
		TArray<TWeakObjectPtr<ACastleDirectorNodeActor>> PreviousNodes;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleSkyIslandsEndToEndTest, "MovingCastle.SkyIslands.EndToEndAllFour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleSkyIslandsEndToEndTest::RunTest(const FString&)
{
	// 从正式对象路径加载资产，验证保存的类引用和配置合同。测试期间不替换导演配置。
	const auto* Config = LoadObject<UCastleDirectorConfigDataAsset>(nullptr, DirectorConfigPath);
	if (!TestNotNull(TEXT("可加载正式 DA_DirectorConfig"), Config)) { return false; }
	TestFalse(TEXT("正式配置包没有未保存改动"), Config->GetOutermost()->IsDirty());
	TestEqual(TEXT("正式配置恰好登记四类空岛"), Config->SkyIslandClasses.Num(), static_cast<int32>(UE_ARRAY_COUNT(Visits)));
	TestEqual(TEXT("正式配置登记三类首访剧情"), Config->SkyIslandArrivalDialogues.Num(), 3);
	for (const auto& Visit : Visits) { TestEqual(Visit.Command, Config->SkyIslandClasses.FindRef(Visit.Type).Get(), Visit.GetClass()); }
	FText Reason;
	const bool bValid = Config->ValidateConfig(Reason);
	if (!TestTrue(FString::Printf(TEXT("正式配置通过 ValidateConfig %s"), *Reason.ToString()), bValid)) { return false; }
	const FString MapFilename = FPackageName::LongPackageNameToFilename(MainMapPath, FPackageName::GetMapPackageExtension());
	if (!TestFalse(TEXT("开始全链测试前没有活动 PIE 世界"), GEditor->PlayWorld != nullptr)
		|| !TestTrue(TEXT("载入正式 MainMap"), FEditorFileUtils::LoadMap(MapFilename, false, false))) { return false; }

	// 仅此用例使用单机进程内 PIE。等待条件由导演状态提供，避免通用地图助手等待无关的世界就绪信号。
	auto* PlaySettings = DuplicateObject(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
	PlaySettings->SetPlayNetMode(PIE_Standalone);
	PlaySettings->SetPlayNumberOfClients(1);
	PlaySettings->SetRunUnderOneProcess(true);
	FRequestPlaySessionParams Params;
	Params.EditorPlaySettings = PlaySettings;
	Params.bAllowOnlineSubsystem = false;
	GEditor->RequestPlaySession(Params);

	ADD_LATENT_AUTOMATION_COMMAND(FVisitSkyIslandsInPIE(*this));
	// 无论断言通过与否都退出 PIE，保证后续地图用例不会复用本轮会话。
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Deadline = 0.0]() mutable
	{
		if (!GEditor->PlayWorld)
		{
			if (Deadline == 0.0) { GEditor->CancelRequestPlaySession(); }
			return true;
		}
		if (Deadline == 0.0)
		{
			GEditor->RequestEndPlayMap();
			Deadline = FPlatformTime::Seconds() + 10.0;
		}
		if (FPlatformTime::Seconds() >= Deadline)
		{
			AddError(TEXT("等待本次 MainMap PIE 世界退出超时。"));
			return true;
		}
		return false;
	}));
	return true;
}
#endif
