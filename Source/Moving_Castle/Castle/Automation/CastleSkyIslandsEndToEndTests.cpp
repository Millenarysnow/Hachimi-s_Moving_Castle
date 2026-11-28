#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Tests/AutomationCommon.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Castle/Dialogue/CastleDialogueScriptDataAsset.h"
#include "Castle/Automation/CastleTowerOverviewTestUtilities.h"
#include "Castle/Dialogue/CastleDialogueSubsystem.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/Rendering/CastleHoverHighlightSubsystem.h"
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
#include "Components/PrimitiveComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Blueprint/UserWidget.h"
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
		const TCHAR* DepartureMeshName;
		UClass* (*GetClass)();
	};

	// 先验证首步没有空岛节点时也能直入，再让四次访问共用同一个正式单局。
	const FIslandVisit Visits[] = {
		{ECastleSkyIslandType::Rest, TEXT("CastleEnterSkyIsland Rest"), TEXT("SM_SkyIsland_Rest"),
			TEXT("SM_SkyIsland_Departure_Rest"), &ACastleRestSkyIsland::StaticClass},
		{ECastleSkyIslandType::Shop, TEXT("CastleEnterSkyIsland Shop"), TEXT("SM_SkyIsland_Shop"),
			TEXT("SM_SkyIsland_Departure_Shop"), &ACastleShopSkyIsland::StaticClass},
		{ECastleSkyIslandType::NPC, TEXT("CastleEnterSkyIsland NPC"), TEXT("SM_SkyIsland_PinballNPC"),
			TEXT("SM_SkyIsland_Departure_PinballNPC"), &ACastlePinballNPCSkyIsland::StaticClass},
		{ECastleSkyIslandType::Gamble, TEXT("CastleEnterSkyIsland Gamble"), TEXT("SM_SkyIsland_Gamble"),
			TEXT("SM_SkyIsland_Departure_Gamble"), &ACastleGambleSkyIsland::StaticClass}
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
			case EStage::HoverTargets:
				return UpdateHoverTargets(*Director, *Controller);
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
		enum class EStage { Enter, HoverTargets, PinballLaunch, PinballExit, Return };
		enum class EHoverStage { AwaitTarget, AwaitModalClear, AwaitModalRestore, AwaitMiss };
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
			auto* Departure = FindObjectFast<UCastleSkyIslandDepartureComponent>(ActiveIsland.Get(), TEXT("DeparturePortal"));
			if (!Test.TestTrue(TEXT("离开门使用独立可见网格且不影响导航"), Departure && Departure->IsVisible()
				&& Departure->GetStaticMesh() && Departure->GetStaticMesh()->GetName() == Visit.DepartureMeshName
				&& !Departure->CanEverAffectNavigation())) { return false; }
			Test.TestEqual(TEXT("离开门使用可见性查询碰撞"), Departure->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
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
			if (!StartHoverTargetChecks(Controller, *ActiveIsland.Get())) { return false; }
			Stage = EStage::HoverTargets;
			return true;
		}

		bool StartHoverTargetChecks(ACastlePlayerController& Controller, ACastleSkyIslandBase& Island)
		{
			HoverTargets.Reset();
			const auto AddTarget = [this](UPrimitiveComponent* Component, const TCHAR* Name)
			{
				if (!Test.TestNotNull(FString::Printf(TEXT("悬停验收目标 %s"), Name), Component)) { return false; }
				HoverTargets.Add(Component);
				return true;
			};
			const auto AddComponentTargets = [&Island, &AddTarget](const TCHAR* NamePrefix)
			{
				TArray<UPrimitiveComponent*> Components;
				Island.GetComponents<UPrimitiveComponent>(Components);
				for (UPrimitiveComponent* Component : Components)
				{
					if (Component->GetName().StartsWith(NamePrefix)
						&& Cast<ICastleSkyIslandInteractable>(Component)
						&& !AddTarget(Component, *Component->GetName()))
					{
						return false;
					}
				}
				return true;
			};

			if (!AddTarget(Island.FindComponentByClass<UCastleSkyIslandDepartureComponent>(), TEXT("离开传送门")))
			{
				return false;
			}
			switch (GetVisit().Type)
			{
			case ECastleSkyIslandType::Rest:
				if (!AddTarget(FindObjectFast<UCastleRestSkyIslandStation>(&Island, TEXT("HealStation")), TEXT("回血台"))
					|| !AddTarget(FindObjectFast<UCastleRestSkyIslandStation>(&Island, TEXT("GoldStation")), TEXT("金币台"))
					|| !AddTarget(FindObjectFast<UCastleRestSkyIslandStation>(&Island, TEXT("ShopStation")), TEXT("免费商店台")))
				{
					return false;
				}
				break;
			case ECastleSkyIslandType::Shop:
				if (!AddComponentTargets(TEXT("Offer"))) { return false; }
				if (!Test.TestEqual(TEXT("商店空岛生成四个独立货位"), HoverTargets.Num(), 5)) { return false; }
				break;
			case ECastleSkyIslandType::NPC:
				if (!AddTarget(Island.FindComponentByClass<UCastlePinballNPCConsoleComponent>(), TEXT("弹珠机投币台")))
				{
					return false;
				}
				break;
			case ECastleSkyIslandType::Gamble:
				if (!AddComponentTargets(TEXT("Roulette")) || !AddComponentTargets(TEXT("SlotMachine"))
					|| !AddComponentTargets(TEXT("BalloonRecycler")))
				{
					return false;
				}
				if (!Test.TestEqual(TEXT("赌博岛有三台独立机台和离开门"), HoverTargets.Num(), 4)) { return false; }
				break;
			}

			UWorld* World = Island.GetWorld();
			if (!Test.TestNotNull(TEXT("悬停测试世界子系统"), World->GetSubsystem<UCastleHoverHighlightSubsystem>()))
			{
				return false;
			}
			HoverCamera = World->SpawnActor<ACameraActor>();
			if (!Test.TestNotNull(TEXT("悬停验收临时相机"), HoverCamera.Get())) { return false; }
			PreviousViewTarget = Controller.GetViewTarget();
			if (!Test.TestNotNull(TEXT("保存进入悬停验收前的视图目标"), PreviousViewTarget.Get())) { return false; }
			Controller.SetControlMode(ECastleControlMode::Classic, false);
			if (GetVisit().Type == ECastleSkyIslandType::Shop || GetVisit().Type == ECastleSkyIslandType::Gamble)
			{
				// 商店货位和赌博机台即使账户余额为零仍可交互，本轮射线检查在缺少金币时执行。
				HoverSavedGold = Controller.GetEconomy()->GetGold();
				FText Reason;
				if (HoverSavedGold > 0 && !Controller.GetEconomy()->TrySpendGold(HoverSavedGold, Reason))
				{
					Test.AddError(FString::Printf(TEXT("悬停验收暂时扣至零金币失败：%s"), *Reason.ToString()));
					return false;
				}
				bRestoreHoverGold = HoverSavedGold > 0;
			}

			HoverTargetIndex = 0;
			HoverStage = EHoverStage::AwaitTarget;
			AimAtHoverTarget(Controller);
			return true;
		}

		bool UpdateHoverTargets(UCastleDirectorSubsystem& Director, ACastlePlayerController& Controller)
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				const UPrimitiveComponent* Target = HoverTargets.IsValidIndex(HoverTargetIndex)
					? HoverTargets[HoverTargetIndex].Get() : nullptr;
				const FString CameraLocation = IsValid(HoverCamera.Get())
					? HoverCamera->GetActorLocation().ToCompactString() : TEXT("<无效>");
				const FString TargetLocation = IsValid(Target)
					? Target->Bounds.Origin.ToCompactString() : TEXT("<无效>");
				Test.AddError(FString::Printf(TEXT("空岛悬停目标射线等待超时；相机位置 %s，目标 %s 位置 %s。"),
					*CameraLocation, Target ? *Target->GetPathName() : TEXT("<无效>"), *TargetLocation));
				return true;
			}
			if (HoverFramesToWait > 0)
			{
				--HoverFramesToWait;
				return false;
			}

			UCastleHoverHighlightSubsystem* Highlight = Controller.GetWorld()->GetSubsystem<UCastleHoverHighlightSubsystem>();
			if (!Test.TestNotNull(TEXT("悬停子系统持续有效"), Highlight)) { return true; }
			switch (HoverStage)
			{
			case EHoverStage::AwaitTarget:
				if (!CheckCurrentHoverTarget(Controller, *Highlight)) { return true; }
				if (!bModalSuppressionChecked)
				{
					TArray<UUserWidget*> Widgets;
					UWidgetBlueprintLibrary::GetAllWidgetsOfClass(
						&Controller, Widgets, UUserWidget::StaticClass(), true);
					if (!Test.TestTrue(TEXT("MainMap 提供模态焦点控件"), !Widgets.IsEmpty())) { return true; }
					ModalFocusWidget = Widgets[0];
					Controller.SetPresentationModalState(true, ModalFocusWidget.Get());
					HoverStage = EHoverStage::AwaitModalClear;
					HoverFramesToWait = 2;
					return false;
				}
				return AdvanceHoverTarget(Director, Controller);
			case EHoverStage::AwaitModalClear:
				if (!Test.TestTrue(TEXT("模态界面打开后立即清空空岛悬停描边"),
					Highlight->GetHighlightedPrimitives().IsEmpty())) { return true; }
				Controller.SetPresentationModalState(false, ModalFocusWidget.Get());
				HoverStage = EHoverStage::AwaitModalRestore;
				HoverFramesToWait = 2;
				return false;
			case EHoverStage::AwaitModalRestore:
				if (!CheckCurrentHoverTarget(Controller, *Highlight)) { return true; }
				bModalSuppressionChecked = true;
				return AdvanceHoverTarget(Director, Controller);
			case EHoverStage::AwaitMiss:
				if (!Test.TestTrue(TEXT("瞄准空处后当前白边立即清除"),
					Highlight->GetHighlightedPrimitives().IsEmpty())) { return true; }
				return FinishHoverTargetChecks(Director, Controller);
			}
			return true;
		}

		bool CheckCurrentHoverTarget(ACastlePlayerController& Controller,
			const UCastleHoverHighlightSubsystem& Highlight)
		{
			// 读取正常 Tick 留给渲染帧的集合。诊断射线只在断言失败时使用，不更新高亮状态。
			UPrimitiveComponent* Target = HoverTargets.IsValidIndex(HoverTargetIndex)
				? HoverTargets[HoverTargetIndex].Get() : nullptr;
			const TArray<UPrimitiveComponent*> Current = Highlight.GetHighlightedPrimitives();
			if (Target && Current.Num() == 1 && Current[0] == Target) { return true; }
			FString CurrentNames;
			for (const UPrimitiveComponent* Component : Current)
			{
				CurrentNames += FString::Printf(TEXT("%s "), *GetPathNameSafe(Component));
			}
			FString FirstHitName = TEXT("<射线未命中>");
			FVector RayOrigin;
			FVector RayDirection;
			if (Controller.TryGetAimWorldRay(RayOrigin, RayDirection))
			{
				FHitResult FirstHit;
				const FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleSkyIslandHoverDiagnostic), false,
					Controller.GetPawn());
				if (Controller.GetWorld()->LineTraceSingleByChannel(
					FirstHit, RayOrigin, RayOrigin + RayDirection * 50000.0f, ECC_Visibility, Query))
				{
					FirstHitName = GetPathNameSafe(FirstHit.GetComponent());
				}
			}
			const FString CameraLocation = IsValid(HoverCamera.Get())
				? HoverCamera->GetActorLocation().ToCompactString() : TEXT("<无效>");
			const FString TargetLocation = IsValid(Target)
				? Target->Bounds.Origin.ToCompactString() : TEXT("<无效>");
			Test.AddError(FString::Printf(TEXT("可交互目标 %s 未独立高亮。相机位置 %s，目标包围盒中心 %s，射线首个命中 %s，当前高亮集合 %s。"),
					Target ? *Target->GetName() : TEXT("<无效>"), *CameraLocation, *TargetLocation,
					*FirstHitName, *CurrentNames));
			return false;
		}

		bool AdvanceHoverTarget(UCastleDirectorSubsystem& Director, ACastlePlayerController& Controller)
		{
			++HoverTargetIndex;
			if (HoverTargetIndex < HoverTargets.Num())
			{
				HoverStage = EHoverStage::AwaitTarget;
				AimAtHoverTarget(Controller);
				return false;
			}

			// 从岛体上方沿水平方向看向空处，避免让测试相机进入近垂直视角。
			HoverCamera->SetActorLocationAndRotation(
				ActiveIsland->GetActorLocation() + FVector::UpVector * 3000.0f, FRotator::ZeroRotator);
			HoverStage = EHoverStage::AwaitMiss;
			HoverFramesToWait = 2;
			Deadline = FPlatformTime::Seconds() + 10.0;
			return false;
		}

		bool FinishHoverTargetChecks(UCastleDirectorSubsystem& Director, ACastlePlayerController& Controller)
		{
			Controller.ClearHoverHighlight();
			Controller.SetViewTarget(PreviousViewTarget.Get());
			if (IsValid(HoverCamera.Get())) { HoverCamera->Destroy(); }
			if (bRestoreHoverGold)
			{
				FText Reason;
				if (!Controller.GetEconomy()->TryAddGold(HoverSavedGold, Reason))
				{
					Test.AddError(FString::Printf(TEXT("恢复悬停验收前金币余额失败：%s"), *Reason.ToString()));
					return true;
				}
				bRestoreHoverGold = false;
			}
			return !InteractWithIsland(GetVisit().Type, Director, Controller);
		}

		void AimAtHoverTarget(ACastlePlayerController& Controller)
		{
			UPrimitiveComponent* Target = HoverTargets[HoverTargetIndex].Get();
			// 交互网格的原点常在岛面底部。将视线落在其真实可见包围盒中心，避免测试射线止于地板平面。
			const FVector TargetLocation = Target->Bounds.Origin;
			FVector CameraOffset(-350.0f, -350.0f, 900.0f);
			if (Target->IsA<UCastleShopSkyIslandOffer>())
			{
				// 沿中央通道拍摄货位正面，使售卖品与岛体棚顶在画面中分开。
				const double TowardCenter = Target->GetRelativeLocation().Y < 0.0 ? 1.0 : -1.0;
				CameraOffset = FVector(-120.0, TowardCenter * 460.0, 115.0);
			}
			else if (Target->IsA<UCastlePinballNPCConsoleComponent>())
			{
				// 盘面偏航 180 度，投币台朝向 -X 到达点。沿该方向拍摄可避开盘面底板。
				CameraOffset = FVector(-460.0, -180.0, 310.0);
			}
			else if (Target->IsA<UCastleGambleMachineComponent>() || Target->IsA<UCastleSkyIslandDepartureComponent>())
			{
				CameraOffset = FVector(-600.0, -200.0, 225.0);
			}
			const FVector CameraLocation = TargetLocation + CameraOffset;
			const FRotator CameraRotation = (TargetLocation - CameraLocation).Rotation();
			HoverCamera->SetActorLocationAndRotation(CameraLocation, CameraRotation);
			HoverCamera->GetCameraComponent()->SetFieldOfView(60.0f);
			FViewTargetTransitionParams Transition;
			Transition.BlendTime = 0.0f;
			Controller.SetViewTarget(HoverCamera.Get(), Transition);
			// 等待引擎通过正常视图 Tick 更新相机管理器，避免在自动化游戏线程强制零时长计算矩阵。
			HoverFramesToWait = 8;
			Deadline = FPlatformTime::Seconds() + 10.0;
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
				FHitResult ClaimedGoldHit;
				ClaimedGoldHit.Component = Gold;
				const FCastleSkyIslandInteractContext ClaimedGoldContext{&Island, &Controller, ClaimedGoldHit};
				Test.TestFalse(TEXT("已领取的休息岛金币台不再具备悬停交互"), Gold->CanHighlightInteract(ClaimedGoldContext));
				FHitResult ClaimedShopHit;
				ClaimedShopHit.Component = Shop;
				const FCastleSkyIslandInteractContext ClaimedShopContext{&Island, &Controller, ClaimedShopHit};
				Test.TestFalse(TEXT("已领取的休息岛商店台不再具备悬停交互"), Shop->CanHighlightInteract(ClaimedShopContext));
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
				FHitResult SoldOfferHit;
				SoldOfferHit.Component = Offer;
				const FCastleSkyIslandInteractContext SoldOfferContext{&Island, &Controller, SoldOfferHit};
				Test.TestFalse(TEXT("已售罄的商店货位不再具备悬停交互"), Offer->CanHighlightInteract(SoldOfferContext));
				break;
			}
			case ECastleSkyIslandType::NPC:
			{
				auto* Console = Island.FindComponentByClass<UCastlePinballNPCConsoleComponent>();
				if (!Test.TestTrue(TEXT("弹珠机投币台使用独立可见静态网格"), Console && Console->IsVisible()
					&& Console->GetStaticMesh() && Console->GetStaticMesh()->GetName() == TEXT("SM_Pinball_Console")
					&& !Console->CanEverAffectNavigation())) { return false; }
				if (!Interact(Director, Console,
					TEXT("NPC 岛弹珠机控制台"))) { return false; }
				const auto* Highlight = Island.GetWorld()->GetSubsystem<UCastleHoverHighlightSubsystem>();
				Test.TestTrue(TEXT("进入弹珠机瞄准锁定时立即清除投币台描边"),
					Highlight && Highlight->GetHighlightedPrimitives().IsEmpty());
				Test.TestTrue(TEXT("瞄准实际锁定移动和视角"), Controller.IsMoveInputIgnored() && Controller.IsLookInputIgnored());
				PinballStartingGold = GoldBefore;
				// 按键由玩家控制器的正常输入栈和下一帧 PIE Tick 消费。
				SendKey(Controller, EKeys::SpaceBar, IE_Pressed);
				Stage = EStage::PinballLaunch;
				Deadline = FPlatformTime::Seconds() + 5.0;
				return true;
			}
			case ECastleSkyIslandType::Gamble:
			{
				const auto* Gamble = CastChecked<ACastleGambleSkyIsland>(&Island);
				const TCHAR* MachineNames[] = {TEXT("Roulette"), TEXT("SlotMachine"), TEXT("BalloonRecycler")};
				const TCHAR* MeshNames[] = {TEXT("SM_Gamble_Roulette"), TEXT("SM_Gamble_SlotMachine"),
					TEXT("SM_Gamble_BalloonRecycler")};
				for (int32 MachineIndex = 0; MachineIndex < UE_ARRAY_COUNT(MachineNames); ++MachineIndex)
				{
					auto* Machine = FindObjectFast<UCastleGambleMachineComponent>(&Island, MachineNames[MachineIndex]);
					if (!Test.TestTrue(TEXT("赌博机台使用独立可见静态网格"), Machine && Machine->IsVisible()
						&& Machine->GetStaticMesh() && Machine->GetStaticMesh()->GetName() == MeshNames[MachineIndex]
						&& !Machine->CanEverAffectNavigation())) { return false; }
					if (!VerifyVisibilityRayHit(Machine, MachineNames[MachineIndex])) { return false; }
				}
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
			CastleTowerOverviewTests::Check(Test, Board, Controller);
			if (VisitIndex == 0)
			{
				CastleTowerOverviewTests::Capture(TEXT("04-island-return-tower-overview.png"));
			}
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

		bool VerifyVisibilityRayHit(const UPrimitiveComponent* Target, const TCHAR* Name)
		{
			if (!Test.TestNotNull(Name, Target)) { return false; }
			UWorld* World = Target->GetWorld();
			const FVector Location = Target->GetComponentLocation();
			FHitResult Hit;
			const FCollisionQueryParams Params(SCENE_QUERY_STAT(CastleSkyIslandMeshAcceptance), false);
			const bool bHit = World && World->LineTraceSingleByChannel(
				Hit, Location + FVector::UpVector * 450.0, Location, ECC_Visibility, Params);
			return Test.TestTrue(FString::Printf(TEXT("Visibility 射线命中独立网格 %s"), Name),
				bHit && Hit.GetComponent() == Target);
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
		int32 HoverTargetIndex = INDEX_NONE;
		int32 HoverFramesToWait = 0;
		int32 HoverSavedGold = 0;
		bool bRestoreHoverGold = false;
		bool bModalSuppressionChecked = false;
		EHoverStage HoverStage = EHoverStage::AwaitTarget;
		double Deadline = FPlatformTime::Seconds() + 30.0;
		TWeakObjectPtr<ACastleSkyIslandBase> ActiveIsland;
		TWeakObjectPtr<ACameraActor> HoverCamera;
		TWeakObjectPtr<AActor> PreviousViewTarget;
		TWeakObjectPtr<UUserWidget> ModalFocusWidget;
		TArray<TWeakObjectPtr<UPrimitiveComponent>> HoverTargets;
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
