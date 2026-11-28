#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Audio/CastleAudioSubsystem.h"
#include "Castle/Automation/CastlePIEAutomation.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/CameraActor.h"
#include "Castle/Dialogue/CastleDialogueScriptDataAsset.h"
#include "Castle/Dialogue/CastleDialogueSubsystem.h"
#include "Castle/Dialogue/CastleDialogueWidget.h"
#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Guide/CastleGuideSubsystem.h"
#include "Castle/Guide/CastleGuideWidgets.h"
#include "Castle/Tutorial/CastleTutorialConfigDataAsset.h"
#include "Castle/Tutorial/CastleTutorialDirector.h"
#include "Castle/Tutorial/CastleTutorialWidgets.h"
#include "Castle/UI/BuildBar/CastleBuildBarWidget.h"
#include "Castle/UI/Cards/CastleShopCardWidget.h"
#include "Castle/UI/Common/CastleControlSettingsWidget.h"
#include "Castle/UI/Common/CastleHUDControls.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Castle/UI/Items/CastleBadgeBarWidget.h"
#include "Castle/UI/Items/CastleWildcardRoomPickerWidget.h"
#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleMassUnitSubsystem.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "CastleNavigationSubsystem.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "CastleShopWidget.h"
#include "CastleTowerDispatchSubsystem.h"
#include "Components/Image.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Items/CastleItemGrantLibrary.h"
#include "Layout/WidgetPath.h"
#include "MassEntityConfigAsset.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Slate/SceneViewport.h"
#include "Sound/SoundBase.h"
#include "Tests/AutomationCommon.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

using CastlePIEAutomation::StartPIE;
using CastlePIEAutomation::StopPIE;

namespace
{
	template<typename T>
	T* FindScreenWidget(UWorld& World)
	{
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&World, Widgets, T::StaticClass(), true);
		return Widgets.Num() == 1 ? Cast<T>(Widgets[0]) : nullptr;
	}

	bool Click(UUserWidget& Widget, const FName Name)
	{
		auto* Button = Cast<UButton>(Widget.GetWidgetFromName(Name));
		if (!Button) { return false; }
		Button->OnClicked.Broadcast();
		return true;
	}

	/** 从普通 PIE 窗口截图，保留 UMG 和游戏画面供验收。 */
	void CaptureTutorialScreenshot(const TCHAR* Filename)
	{
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("WorkerC/Screenshots");
		IFileManager::Get().MakeDirectory(*Directory, true);
		FScreenshotRequest::RequestScreenshot(Directory / Filename, true, false);
	}
}

/** 通过真实棋盘、商店、路径与战斗驱动 S1–S14，不直接改写编排器阶段。 */
class FCastleTutorialPIEDriver final : public IAutomationLatentCommand
{
public:
	explicit FCastleTutorialPIEDriver(FAutomationTestBase& InTest) : Test(InTest) {}

	bool Update() override
	{
		UWorld* World = GEditor->PlayWorld;
		// S14 已改为自动旅行，完成证据由实际进入主菜单关卡而不是旧教程 Actor 存活提供。
		if (World && LastStep == 14 && World->GetMapName().EndsWith(TEXT("StartMenu")))
		{
			Test.TestTrue(TEXT("完成 S14 结尾对话后已自动返回 StartMenu"), World->GetMapName().EndsWith(TEXT("StartMenu")));
			Test.TestTrue(TEXT("S10 已实际派出 Meshy 城主并挂载气泡"), bSawTutorialLordDispatch);
			return true;
		}
		auto* Controller = World ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Controller || !Controller->GetPawn()) { return Wait(TEXT("教学地图 PIE 未就绪")); }
		TActorIterator<ACastleTutorialDirector> Tutorial(World);
		TActorIterator<ACastleBoard> Board(World);
		TActorIterator<ACastlePatrolRoute> Route(World);
		if (!Tutorial || !Board || !Route) { return Wait(TEXT("教学导演、棋盘或路线入口缺失")); }
		const int32 Step = Tutorial->GetCurrentStep();
		if (Tutorial->HasShownDispatchBubble()) { bSawTutorialLordDispatch = true; }
		if (!Tutorial->IsActorTickEnabled() && !Tutorial->HasCompletedTutorial())
		{
			Test.AddError(FString::Printf(TEXT("教学导演在 S%d 停止。"), Step));
			return true;
		}
		if (Step == 0) { return Wait(TEXT("教学导演未初始化")); }
		const auto* Audio = UCastleAudioSubsystem::GetForWorld(World);
		if (!Audio) { return Fail(TEXT("教程没有音频子系统")); }
		if (Step == 1 && !bTutorialHealthLockChecked)
		{
			auto* MainTowerSubsystem = World->GetSubsystem<UCastleMainTowerSubsystem>();
			auto* MainTower = IsValid(MainTowerSubsystem) ? MainTowerSubsystem->GetRegisteredMainTower() : nullptr;
			if (!MainTower) { return Fail(TEXT("TutorialMap 未注册主塔生命组件")); }
			const float OneHeart = MainTower->MaxHealth / 20.0f;
			Test.TestTrue(TEXT("教程主塔超额受击可正常结算"), MainTower->ApplyDamage(MainTower->MaxHealth * 2.0f));
			Test.TestTrue(TEXT("教程主塔生命保持在一颗心"), FMath::IsNearlyEqual(MainTower->CurrentHealth, OneHeart, 0.01f));
			bTutorialHealthLockChecked = true;
		}
		if (Tutorial->HasCompletedTutorial())
		{
			// 完成标记先于地图旅行设置，用例继续等待真正载入 StartMenu。
			return Wait(TEXT("结尾对话已经结束，但尚未返回 StartMenu"));
		}
		if (Step != LastStep)
		{
			Test.TestEqual(TEXT("逐步推进，无跳步"), Step, LastStep + 1);
			Test.TestEqual(TEXT("仅 S10 战斗期间切换战斗 BGM"), Audio->GetCurrentBGMTrack(),
				Step == 10 ? ECastleBGMTrack::Battle : ECastleBGMTrack::Build);
			Test.TestFalse(TEXT("正式导演在 TutorialMap 保持关闭"), World->GetSubsystem<UCastleDirectorSubsystem>()->IsRunning());
			LastStep = Step;
			Action = 0;
			// 幽灵飞近、城主往返和箭矢飞行均消耗真实世界时间，战斗允许多轮巡逻。
			Deadline = FPlatformTime::Seconds() + (Step == 10 ? 90.0 : 35.0);
		}
		auto* Dialogue = World->GetGameInstance()->GetSubsystem<UCastleDialogueSubsystem>();
		auto* Guide = World->GetGameInstance()->GetSubsystem<UCastleGuideSubsystem>();
		if (Dialogue->IsDialoguePlaying())
		{
			// 在实际对话期间读取生产状态，验证交互既未提前开始，也没有覆盖子对话。
			if (Step == 10)
			{
				Test.TestFalse(TEXT("S10 对话期间战斗尚未开始"), World->GetSubsystem<UCastleCombatSubsystem>()->IsBattleActive());
				Test.TestFalse(TEXT("S10 对话期间城主待命会话尚未创建"), World->GetSubsystem<UCastleTowerDispatchSubsystem>()->IsSessionActive());
			}
			else if (Step == 11)
			{
				const auto* Reward = FindScreenWidget<UCastleTutorialRewardWidget>(*World);
				if (Action == 0)
				{
					Test.TestNull(TEXT("S11 开场对话期间三选一尚未挂载"), Reward);
				}
				else
				{
					Test.TestTrue(TEXT("复制奖励对话期间单卡领取层保持隐藏"),
						Reward && Reward->GetVisibility() == ESlateVisibility::Collapsed);
				}
			}
			else if (Step == 14 && Action == 2)
			{
				// 结尾台词播放时农田已放置，趁地图旅行前核对原始路径点。
				Test.TestEqual(TEXT("摆上农田后仍使用 S8 原始路点"),
					World->GetSubsystem<UCastleNavigationSubsystem>()->GetRequestedWaypoints(), OriginalWaypoints);
			}
			auto* Widget = FindScreenWidget<UCastleDialogueWidget>(*World);
			auto* Choices = Widget ? Cast<UVerticalBox>(Widget->GetWidgetFromName(TEXT("ChoiceContainer"))) : nullptr;
			if (!Choices) { return Fail(TEXT("读取不到保存后的对话控件")); }
			// 保留一帧当前句与游戏画面，下一轮再点击，避免截图前卸载对话。
			FName Screenshot;
			if (Step == 10) { Screenshot = TEXT("Tutorial-S10-Dialogue.png"); }
			else if (Step == 11)
			{
				Screenshot = Action == 0 ? TEXT("Tutorial-S11-Dialogue.png") : TEXT("Tutorial-S11-ClaimDialogue.png");
			}
			if (!Screenshot.IsNone() && !DialogueScreenshots.Contains(Screenshot))
			{
				DialogueScreenshots.Add(Screenshot);
				Widget->CompleteCurrentLine();
				CaptureTutorialScreenshot(*Screenshot.ToString());
				return false;
			}
			if (Choices->GetVisibility() == ESlateVisibility::Visible)
			{
				auto* Choice = Cast<UButton>(Choices->GetChildAt(1));
				if (!Choice) { return Fail(TEXT("S9 的 B 选项未生成")); }
				Choice->OnClicked.Broadcast();
			}
			else if (!Click(*Widget, TEXT("ContinueButton"))) { return Fail(TEXT("找不到对话推进按钮")); }
			return Wait(TEXT("对话未能推进"));
		}
		if (Guide->HasOpenModal())
		{
			auto* Popup = FindScreenWidget<UCastleGuidePopupWidget>(*World);
			if (!Popup || !Click(*Popup, TEXT("CloseButton"))) { return Fail(TEXT("图文弹窗未能关闭")); }
			return false;
		}

		const FIntVector Center = Board->GetBoardCenterCoordinate();
		const FIntVector Front = Center + FIntVector(1, 0, 0);
		const FIntVector Rear = Center + FIntVector(-1, 0, 0);
		const FIntVector Archer = Center + FIntVector(1, 1, 0);
		auto* Backpack = Board->PrototypeBackpack.Get();
		auto* Shop = Controller->GetShop();
		FText Reason;

		// 卡牌来源为实际建筑栏，保留放置、匹配、合成与回收的全部生产校验。
		const auto Place = [&](const FIntVector Cell, const FIntVector Source,
			const ECastleGridFace Face, const ECastleBuildingType Type)
		{
			const int32 Index = Backpack->GetItems().IndexOfByPredicate([Type](const auto& Card) { return Card.GetBuildingType() == Type; });
			if (Index == INDEX_NONE) { Reason = INVTEXT("建筑栏不含当前练习需要的卡牌"); return false; }
			FCastleGridCursorTarget Target;
			Target.bValid = true;
			Target.Cell = Source;
			Target.AdjacentCell = Cell;
			Target.Face = Face;
			Target.Surface = Source == Center ? ECastleGridCursorSurface::MainTowerSide : ECastleGridCursorSurface::Building;
			return Board->PlaceBackpackBuildingAtCursor(Target, Index, Reason) != nullptr;
		};
		const auto Ready = [&](const FIntVector Cell)
		{
			const auto* Room = Board->GetBuildingAt(Cell);
			return Room && Room->CanReceiveBuildingTrigger();
		};
		const auto Waypoint = [&](const FIntVector Cell)
		{
			return Board->GetGridWorldLocation(Cell) - FVector(0, 0, Board->LayerHeight * 0.5);
		};
		switch (Step)
		{
		case 2:
			if (Action++ == 0 && !Place(Front, Center, ECastleGridFace::PositiveX, ECastleBuildingType::NormalRoom)) { return Fail(Reason.ToString()); }
			break;
		case 3:
		{
			const FIntVector Cells[] = {Archer, Center + FIntVector(0, 1, 0), Center + FIntVector(-1, 1, 0), Rear};
			const ECastleGridFace Faces[] = {ECastleGridFace::PositiveY, ECastleGridFace::NegativeX,
				ECastleGridFace::NegativeX, ECastleGridFace::NegativeY};
			const FIntVector Source = Action == 0 ? Front : Cells[Action - 1];
			if (Action < UE_ARRAY_COUNT(Cells) && Ready(Source))
			{
				if (!Place(Cells[Action], Source, Faces[Action], ECastleBuildingType::NormalRoom)) { return Fail(Reason.ToString()); }
				++Action;
			}
			break;
		}
		case 4: if (Action++ == 0) { Controller->ToggleShop(); } break;
		case 5:
			if (Action == 0)
			{
				// 先确认停留在商店的真实 Widget 可接收关闭台词后的快捷键。
				auto* ShopWidget = FindScreenWidget<UCastleShopWidget>(*World);
				if (!ShopWidget || !ShopWidget->HasUserFocus(Controller)) { return Wait(TEXT("商店未恢复键盘焦点")); }
				if (!Shop->TryBuyOffer(0, Reason)) { return Fail(Reason.ToString()); }
				++Action;
			}
			break;
		case 6:
			if (!Guide->IsEntryUnlocked(TEXT("Demolish"))) { break; }
			if (Action == 0)
			{
				auto* Room = Board->GetBuildingAt(Archer);
				const int32 Before = Backpack->GetItemCount();
				if (!Room || !Controller->OpenBuildingDemolishMenu(*Room, Archer, FVector2D(200, 200)))
				{
					return Fail(TEXT("正式拆除菜单打开失败"));
				}
				Controller->ConfirmBuildingDemolish(*Controller->GetBuildingDemolishMenu());
				if (!Test.TestEqual(TEXT("普通房回到建筑栏"), Backpack->GetItemCount(), Before + 1)) { return true; }
				++Action;
			}
			else if (Action == 1)
			{
				if (!Place(Archer, Front, ECastleGridFace::PositiveY, ECastleBuildingType::ArcherTower)) { return Fail(Reason.ToString()); }
				++Action;
			}
			break;
		case 7: if (Action++ == 0) { Route->ToggleEditing(); } break;
		case 8:
			if (Guide->IsEntryUnlocked(TEXT("Patrol")) && Action++ == 0)
			{
				if (!Route->TryAppendDraftWaypoint(Waypoint(Archer), Reason)
					|| !Route->TryAppendDraftWaypoint(Waypoint(Rear), Reason)) { return Fail(Reason.ToString()); }
				Route->ToggleEditing();
				if (Route->IsEditing()) { return Fail(TEXT("再次按 P 后仍在编辑")); }
				OriginalWaypoints = World->GetSubsystem<UCastleNavigationSubsystem>()->GetRequestedWaypoints();
			}
			break;
		case 10:
			if (Action++ == 0)
			{
				Test.TestTrue(TEXT("S10 对话关闭后才启动出兵战斗"), World->GetSubsystem<UCastleCombatSubsystem>()->IsBattleActive());
				Test.TestTrue(TEXT("S10 对话关闭后才创建城主待命会话"), World->GetSubsystem<UCastleTowerDispatchSubsystem>()->IsSessionActive());
				auto* Dispatch = World->GetSubsystem<UCastleTowerDispatchSubsystem>();
				Test.TestFalse(TEXT("手动点塔前拒绝开启自动出兵"), Dispatch->SetAutoDispatchEnabled(true));
				Test.TestFalse(TEXT("拒绝自动出兵时尚未产生城主气泡"), Tutorial->HasShownDispatchBubble());
				// 手动派兵立即广播实体句柄，只在本次调用范围内监听，供截图定位实际城主。
				const FDelegateHandle CaptureHandle = Dispatch->OnNPCDispatched.AddLambda(
					[this](const FCastleMassUnitHandle& Unit) { Lord = Unit; });
				const bool bDispatched = Dispatch->BeginManualDispatch(Reason);
				Dispatch->OnNPCDispatched.Remove(CaptureHandle);
				if (!bDispatched) { return Fail(Reason.ToString()); }
				Test.TestEqual(TEXT("城主派出气泡选择独立配音"), GetNameSafe(Audio->GetCurrentVoiceSound()),
					FString(TEXT("SW_Voice_CryBubble")));
				Dispatch->StopManualDispatch();
				// 出门后的第一段有房间墙体，等城主走到巡逻路线中段再拍摄。
				LordCaptureTime = World->GetTimeSeconds() + 6.0;
			}
			else if (Tutorial->HasShownDispatchBubble() && !bLordScreenshotSaved)
			{
				if (World->GetTimeSeconds() < LordCaptureTime) { return false; }
				// 临时相机随巡逻实体移动，留出 Mass 表现更新和视口绘制时间。
				FCastleMassUnitSnapshot Snapshot;
				if (!World->GetSubsystem<UCastleMassUnitSubsystem>()->GetUnitSnapshot(Lord, Snapshot))
				{
					return Fail(TEXT("截取城主出场时无法读取实际派出的 Mass 实体"));
				}
				const FVector Location = Snapshot.Transform.GetLocation();
				// 相机沿通道方向置于屋顶下，与城主共处房间内部。
				const FVector Eye = Snapshot.Transform.TransformPosition(FVector(185, 30, 150));
				const FRotator Look = (Location + FVector(0, 0, 85) - Eye).Rotation();
				if (!LordCamera.IsValid())
				{
					LordCamera = World->SpawnActor<ACameraActor>(Eye, Look);
					if (!LordCamera.IsValid()) { return Fail(TEXT("无法创建城主截图相机")); }
					PreviousViewTarget = Controller->GetViewTarget();
					Controller->SetViewTarget(LordCamera.Get());
					LordCameraReadyTime = World->GetTimeSeconds() + 1.0;
					return false;
				}
				if (!bLordScreenshotRequested)
				{
					LordCamera->SetActorLocationAndRotation(Eye, Look);
					if (World->GetTimeSeconds() < LordCameraReadyTime) { return false; }
					// 以 Meshy 实例和场景代理为证据，避免只凭城主的教学气泡判定已渲染。
					bool bFoundLord = false;
					for (TObjectIterator<UInstancedSkinnedMeshComponent> It; It; ++It)
					{
						if (It->GetWorld() != World || GetNameSafe(It->GetSkinnedAsset()) != TEXT("SK_TutorialLord")) { continue; }
						FTransform VisibleTransform;
						if (It->GetInstanceCount() == 1 && It->GetSceneProxy()
							&& It->GetInstanceTransform(It->GetInstanceId(0), VisibleTransform, true))
						{
							bFoundLord = true;
							Test.AddInfo(FString::Printf(TEXT("Meshy 城主实体位置 %s，渲染实例变换 %s"),
								*Location.ToString(), *VisibleTransform.ToHumanReadableString()));
						}
					}
					Test.TestTrue(TEXT("S10 实际渲染批次包含 Meshy 城主"), bFoundLord);
					CaptureTutorialScreenshot(TEXT("Tutorial-S10-LordDispatched.png"));
					bLordScreenshotRequested = true;
					return false;
				}
				Controller->SetViewTarget(PreviousViewTarget.Get());
				LordCamera->Destroy();
				bLordScreenshotSaved = true;
			}
			break;
		case 11:
		{
			auto* Reward = FindScreenWidget<UCastleTutorialRewardWidget>(*World);
			if (!Reward) { return Fail(TEXT("S11 对话关闭后教学三选一未挂载")); }
			if (Action == 0)
			{
				// 三选一先实际绘制，再点击复制并进入领取对话。
				if (!bRewardChoicesScreenshotSaved)
				{
					CaptureTutorialScreenshot(TEXT("Tutorial-S11-RewardChoices.png"));
					bRewardChoicesScreenshotSaved = true;
					return false;
				}
				if (!Click(*Reward, TEXT("CopyButton"))) { return Fail(TEXT("找不到第三项奖励")); }
				++Action;
			}
			else if (Action == 1)
			{
				// 点击对话推进后的下一轮由导演恢复奖励模态，测试随后才能真实点击单卡领取。
				if (Reward->GetVisibility() == ESlateVisibility::Collapsed) { return Wait(TEXT("等待复制奖励对话结束并恢复领取")); }
				if (!bRewardScreenshotSaved)
				{
					CaptureTutorialScreenshot(TEXT("Tutorial-S11-Reward.png"));
					bRewardScreenshotSaved = true;
					return false;
				}
				if (!Click(*Reward, TEXT("CardButton"))) { return Fail(TEXT("找不到复制箭塔卡")); }
				++Action;
			}
			break;
		}
		case 12:
			if (Action++ == 0 && !Place(Center + FIntVector(2, 1, 0), Archer, ECastleGridFace::PositiveX,
				ECastleBuildingType::ArcherTower)) { return Fail(Reason.ToString()); }
			break;
		case 13:
			if (Action == 0)
			{
				Controller->ToggleShop();
				if (!Test.TestTrue(TEXT("重新开店可见空货架"), Shop->IsShopOpen() && Shop->GetOffers().IsEmpty())) { return true; }
				++Action;
			}
			else if (Action == 1 && ++ShopPause > 3)
			{
				auto* ShopWidget = FindScreenWidget<UCastleShopWidget>(*World);
				if (!ShopWidget || !ShopWidget->HasUserFocus(Controller)) { return Wait(TEXT("缺货剧情关闭后商店未恢复焦点")); }
				const int32 Before = Backpack->GetItemCount();
				Test.TestFalse(TEXT("教学固定货架拒绝出售"), Shop->TrySellBackpackItem(0, Reason));
				Test.TestEqual(TEXT("禁售提示"), Reason.ToString(), FString(TEXT("新手教程期间暂不可出售房间卡")));
				Test.TestEqual(TEXT("拒绝出售后卡牌数量不变"), Backpack->GetItemCount(), Before);
				if (!Shop->TryRefreshOffers(Reason)) { return Fail(Reason.ToString()); }
				++Action;
			}
			else if (Action == 2 && !Shop->GetOffers().IsEmpty())
			{
				if (!Shop->TryBuyOffer(0, Reason)) { return Fail(Reason.ToString()); }
				++Action;
			}
			break;
		case 14:
		{
			const FIntVector Farm = Center + FIntVector(0, 1, 0);
			if (Action == 0)
			{
				auto* Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
				const auto Path = Navigation->AcquireSharedPath();
				if (!Path || !Path->bValid) { return Wait(TEXT("箭塔合成后尚未发布原双门路线")); }
				Test.TestTrue(TEXT("战后保留已提交的开环"), Path->bTowerDispatch);
				Test.TestEqual(TEXT("战后仍是原玩家路点"), Navigation->GetRequestedWaypoints(), OriginalWaypoints);
				// 将回路中段的普通房换成农田，保持两个已保存的路点不动。
				auto* Room = Board->GetBuildingAt(Farm);
				if (!Room || !Controller->OpenBuildingDemolishMenu(*Room, Farm, FVector2D(200, 200))) { return Fail(TEXT("无法回收中段普通房")); }
				Controller->ConfirmBuildingDemolish(*Controller->GetBuildingDemolishMenu());
				++Action;
			}
			else if (Action == 1)
			{
				if (!Place(Farm, Center, ECastleGridFace::PositiveY, ECastleBuildingType::Farm)) { return Fail(Reason.ToString()); }
				++Action;
			}
			break;
		}
		default: break;
		}
		return Wait(TEXT("真实玩法状态未达到步骤完成条件"));
	}

private:
	bool Fail(const FString& Reason)
	{
		Test.AddError(FString::Printf(TEXT("教程 S%d：%s"), LastStep, *Reason));
		return true;
	}

	bool Wait(const TCHAR* Reason) { return FPlatformTime::Seconds() >= Deadline && Fail(Reason); }
	FAutomationTestBase& Test;
	int32 LastStep = 0;
	int32 Action = 0;
	int32 ShopPause = 0;
	bool bTutorialHealthLockChecked = false;
	bool bSawTutorialLordDispatch = false;
	bool bLordScreenshotSaved = false;
	bool bLordScreenshotRequested = false;
	bool bRewardChoicesScreenshotSaved = false;
	bool bRewardScreenshotSaved = false;
	TSet<FName> DialogueScreenshots;
	double LordCaptureTime = 0.0;
	double LordCameraReadyTime = 0.0;
	FCastleMassUnitHandle Lord;
	TWeakObjectPtr<ACameraActor> LordCamera;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	TArray<FVector> OriginalWaypoints;
	double Deadline = FPlatformTime::Seconds() + 30.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleTutorialEndToEndTest, "MovingCastle.Tutorial.EndToEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleTutorialEndToEndTest::RunTest(const FString&)
{
	// 确认保存后的教程资产引用含追击与攻击的正式幽灵模板。
	const auto* Battle = LoadObject<UCastleCombatConfigDataAsset>(nullptr,
		TEXT("/Game/MovingCastle/Data/Tutorial/DA_TutorialGhostBattle.DA_TutorialGhostBattle"));
	if (!TestTrue(TEXT("教学战斗使用正式 ShadowSwarm 幽灵"),
		Battle && Battle->Roster.Num() == 1 && GetNameSafe(Battle->Roster[0].Configuration.Get()) == TEXT("DA_MassEnemy_ShadowSwarm")))
	{
		return false;
	}
	if (!StartPIE(*this, TEXT("/Game/MovingCastle/Maps/TutorialMap"))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FCastleTutorialPIEDriver(*this));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(StopPIE()));
	return true;
}

/** 在真实 PIE 视口中读取 UMG 缓存几何，验证相对锚点及教程 HUD 的挂载和显隐。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleTutorialDialogueViewportTest, "MovingCastle.Dialogue.TutorialViewportLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleTutorialDialogueViewportTest::RunTest(const FString&)
{
	if (!StartPIE(*this, TEXT("/Game/MovingCastle/Maps/TutorialMap"), FIntPoint(1280, 720))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Deadline = FPlatformTime::Seconds() + 45.0,
		bBothPortraits = false, FramesSinceResize = 0]() mutable
	{
		UWorld* World = GEditor->PlayWorld;
		auto* Player = World ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
		auto* Widget = World ? FindScreenWidget<UCastleDialogueWidget>(*World) : nullptr;
		auto* BuildBar = World ? FindScreenWidget<UCastleBuildBarWidget>(*World) : nullptr;
		if (!Player || !Widget || !BuildBar)
		{
			if (FPlatformTime::Seconds() < Deadline) { return false; }
			// 区分 PIE 尚未初始化、剧本未播放和控件未挂载，保留失败现场的信息。
			TArray<UUserWidget*> DialogueWidgets;
			TArray<UUserWidget*> AllDialogueWidgets;
			TArray<UUserWidget*> BuildBarWidgets;
			if (World)
			{
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, DialogueWidgets, UCastleDialogueWidget::StaticClass(), true);
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, AllDialogueWidgets, UCastleDialogueWidget::StaticClass(), false);
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, BuildBarWidgets, UCastleBuildBarWidget::StaticClass(), true);
			}
			const auto* DialogueSubsystem = World && World->GetGameInstance()
				? World->GetGameInstance()->GetSubsystem<UCastleDialogueSubsystem>() : nullptr;
			AddError(FString::Printf(TEXT("教程 PIE 未挂载预期界面，玩家=%s、对话播放=%s、顶级/全部对话控件=%d/%d、建造栏控件=%d，地图=%s。"),
				*GetNameSafe(Player), DialogueSubsystem && DialogueSubsystem->IsDialoguePlaying() ? TEXT("是") : TEXT("否"),
				DialogueWidgets.Num(), AllDialogueWidgets.Num(), BuildBarWidgets.Num(), *GetNameSafe(World)));
			return true;
		}
		auto* Dialogue = World->GetGameInstance()->GetSubsystem<UCastleDialogueSubsystem>();
		auto* RightImage = Widget->GetWidgetFromName(TEXT("RightPortrait"));
		if (!TestTrue(TEXT("教程开场剧本与正式右侧立绘绑定有效"), Dialogue && Dialogue->IsDialoguePlaying() && RightImage))
		{
			return true;
		}
		auto* Continue = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ContinueButton")));
		auto* ContinueSlot = Continue ? Cast<UCanvasPanelSlot>(Continue->Slot) : nullptr;
		auto* Choice = Widget->GetWidgetFromName(TEXT("ChoiceContainer"));
		auto* ChoiceSlot = Choice ? Cast<UCanvasPanelSlot>(Choice->Slot) : nullptr;
		TestTrue(TEXT("对话推进按钮锚定并覆盖整个视口"), ContinueSlot
			&& ContinueSlot->GetAnchors() == FAnchors(0, 0, 1, 1)
			&& ContinueSlot->GetOffsets() == FMargin(0) && !ContinueSlot->GetAutoSize());
		TestTrue(TEXT("分支选项绘制层高于全屏推进层"), ContinueSlot && ChoiceSlot
			&& ContinueSlot->GetZOrder() < ChoiceSlot->GetZOrder());

		if (!bBothPortraits)
		{
			// 桌面工作区较小时，窗口边框会挤占请求尺寸。把真实 PIE 渲染目标固定在验收像素。
			auto* ViewportClient = World->GetGameViewport();
			auto* Viewport = ViewportClient ? ViewportClient->GetGameViewport() : nullptr;
			const TSharedPtr<SWindow> Window = ViewportClient ? ViewportClient->GetWindow() : nullptr;
			if (!TestNotNull(TEXT("可固定实际 PIE 渲染视口"), Viewport) || !TestTrue(TEXT("PIE 窗口存在"), Window.IsValid()))
			{
				return true;
			}
			Window->Resize(FVector2D(1280, 720));
			Viewport->SetViewportSize(1280, 720);
			Viewport->SetFixedViewportSize(1280, 720);
			// 经过真实的剧本行和揭示逻辑推进到双立绘，不给视口注入伪造数据。
			for (int32 Step = 0; Step < 6 && RightImage->GetVisibility() == ESlateVisibility::Collapsed; ++Step)
			{
				Dialogue->AdvanceDialogueForAutomation();
			}
			if (!TestEqual(TEXT("已推进到双立绘的开场台词"), RightImage->GetVisibility(), ESlateVisibility::HitTestInvisible))
			{
				return true;
			}
			Widget->CompleteCurrentLine();
			bBothPortraits = true;
			return false;
		}
		// 缩放框需要经过布局和绘制两轮才能把初始几何换成新尺寸，避免采到旧窗口的子控件缓存。
		if (++FramesSinceResize <= 4) { return false; }

		const auto Measure = [&](const FName Name, FVector2D& OutMin, FVector2D& OutMax)
		{
			const UWidget* Child = Widget->GetWidgetFromName(Name);
			if (!Child) { return false; }
			const FGeometry& Geometry = Child->GetCachedGeometry();
			if (Geometry.GetLocalSize().IsNearlyZero()) { return false; }
			FVector2D Unused;
			USlateBlueprintLibrary::LocalToViewport(World, Geometry, FVector2D::ZeroVector, OutMin, Unused);
			USlateBlueprintLibrary::LocalToViewport(World, Geometry, Geometry.GetLocalSize(), OutMax, Unused);
			return true;
		};
		FVector2D PanelMin, PanelMax, LeftSlotMin, LeftSlotMax, RightSlotMin, RightSlotMax, LeftMin, LeftMax, RightMin, RightMax;
		if (!Measure(TEXT("DialoguePanel"), PanelMin, PanelMax)
			|| !Measure(TEXT("LeftPortraitSlot"), LeftSlotMin, LeftSlotMax)
			|| !Measure(TEXT("RightPortraitSlot"), RightSlotMin, RightSlotMax)
			|| !Measure(TEXT("LeftPortrait"), LeftMin, LeftMax) || !Measure(TEXT("RightPortrait"), RightMin, RightMax))
		{
			if (FPlatformTime::Seconds() < Deadline) { return false; }
			AddError(TEXT("对话面板或两侧立绘没有取得 PIE 视口几何。"));
			return true;
		}
		int32 ViewportWidth, ViewportHeight;
		Player->GetViewportSize(ViewportWidth, ViewportHeight);
		const FVector2D Panel = PanelMax - PanelMin, LeftSlot = LeftSlotMax - LeftSlotMin, RightSlot = RightSlotMax - RightSlotMin;
		const FVector2D Left = LeftMax - LeftMin, Right = RightMax - RightMin;
		AddInfo(FString::Printf(TEXT("FORTRESS_BOUNDS WBP_CastleDialogue [%0.2f,%0.2f]–[%0.2f,%0.2f] %.2fx%.2f"),
			PanelMin.X, PanelMin.Y, PanelMax.X, PanelMax.Y, Panel.X, Panel.Y));
		TestTrue(TEXT("双立绘槽位有实测尺寸"), LeftSlot.X > 0 && LeftSlot.Y > 0 && RightSlot.X > 0 && RightSlot.Y > 0);
		TestTrue(TEXT("两张立绘均已绘制"), Left.X > 0 && Left.Y > 0 && Right.X > 0 && Right.Y > 0);
		TestEqual(TEXT("独立 PIE 视口为 1280×720"), FIntPoint(ViewportWidth, ViewportHeight), FIntPoint(1280, 720));
		TestTrue(TEXT("面板实际覆盖超过视口宽度的 75%"), Panel.X > ViewportWidth * 0.75);
		TestTrue(TEXT("两张立绘底边均贴合对话面板上沿"), FMath::IsNearlyEqual(LeftMax.Y, PanelMin.Y, 2.0)
			&& FMath::IsNearlyEqual(RightMax.Y, PanelMin.Y, 2.0));
		// 离屏窗口合成到固定渲染目标时会应用各轴像素缩放，正方形基准从实际生成的 UMG 盒校验。
		const auto* LeftBox = Cast<USizeBox>(Widget->GetWidgetFromName(TEXT("LeftPortrait"))->GetParent());
		const auto* RightBox = Cast<USizeBox>(RightImage->GetParent());
		TestTrue(TEXT("左右立绘均使用 380×380 的 UMG 设计基准"),
			LeftBox && RightBox && LeftBox->GetWidthOverride() == 380.0f && LeftBox->GetHeightOverride() == 380.0f
			&& RightBox->GetWidthOverride() == 380.0f && RightBox->GetHeightOverride() == 380.0f);
		TestEqual(TEXT("实际对话期收起建造栏"), BuildBar->GetVisibility(), ESlateVisibility::Collapsed);
		TestNull(TEXT("教程视口没有正式徽章栏"), FindScreenWidget<UCastleBadgeBarWidget>(*World));
		// 从实际 Slate 窗口取命中路径，验证对话面板上方的空白也落在继续按钮上。
		const TSharedPtr<SWindow> Window = World->GetGameViewport()->GetWindow();
		const TSharedPtr<SWidget> ContinueSlate = Continue ? Continue->GetCachedWidget() : nullptr;
		if (TestTrue(TEXT("全屏推进按钮已有 Slate 控件和 PIE 窗口"), ContinueSlate.IsValid() && Window.IsValid()))
		{
			const FGeometry& Geometry = Continue->GetCachedGeometry();
			const FVector2D BlankSpace = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.15f);
			const FWidgetPath Hit = FSlateApplication::Get().LocateWindowUnderMouse(BlankSpace, {Window.ToSharedRef()});
			TestTrue(TEXT("实际 Slate 命中路径包含空白区的全屏推进按钮"), Hit.ContainsWidget(ContinueSlate.Get()));
		}
		// 完成本段剧本后由子系统发出活动状态变更，检查无需等待 UMG Tick 的即时恢复。
		const int32 OpeningLineCount = Dialogue->GetCurrentScript()->Lines.Num();
		for (int32 Step = 0; Step < OpeningLineCount && Dialogue->IsDialoguePlaying(); ++Step)
		{
			Dialogue->AdvanceDialogueForAutomation();
		}
		TestFalse(TEXT("开场对话已播放完成"), Dialogue->IsDialoguePlaying());
		TestEqual(TEXT("对话结束立即恢复可穿透根容器的底部建造栏"),
			BuildBar->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
		return true;
	}));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(StopPIE()));
	return true;
}

/** 在与教程对话相同的真实 PIE 渲染目标中测量三个全屏弹窗。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleFortressPopupViewportTest, "MovingCastle.UI.FortressKit.MainViewportLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleFortressPopupViewportTest::RunTest(const FString&)
{
	if (!StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"), FIntPoint(1280, 720))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Deadline = FPlatformTime::Seconds() + 45.0,
		Frames = 0, RewardPreview = TWeakObjectPtr<UCastleRewardChoiceWidget>()]() mutable
	{
		UWorld* World = GEditor->PlayWorld;
		auto* Player = World ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Player || !Player->GetPawn())
		{
			if (FPlatformTime::Seconds() < Deadline) { return false; }
			AddError(TEXT("主地图没有完成 PIE 初始化。"));
			return true;
		}
		if (Frames++ == 0)
		{
			auto* Client = World->GetGameViewport();
			auto* Viewport = Client ? Client->GetGameViewport() : nullptr;
			const auto Window = Client ? Client->GetWindow() : nullptr;
			if (!Viewport || !Window)
			{
				AddError(TEXT("主地图缺少真实视口窗口。"));
				return true;
			}
			Window->Resize(FVector2D(1280, 720));
			Viewport->SetViewportSize(1280, 720);
			Viewport->SetFixedViewportSize(1280, 720);
			Player->ToggleControlSettings();
			// 该回归检查工坊在真实视口的布局，直接用通用万能卡入口构造选项。
			const TActorIterator<ACastleBoard> Board(World);
			FText Reason;
			UCastlePendingRoomReward* Pending = nullptr;
			if (!TestTrue(TEXT("创建工坊布局测试的待领奖励"), Board && UCastleItemGrantLibrary::TryCreateWildcardReward(
				INVTEXT("可选房间"), FCastleWildcardRoomSpec{}, Board->PrototypeBackpack, Board->BuildingDefinitions, Pending, Reason)))
			{
				return true;
			}
			const auto PickerClass = LoadClass<UCastleWildcardRoomPickerWidget>(nullptr,
				TEXT("/Game/MovingCastle/UI/Items/WBP_WildcardRoomPicker.WBP_WildcardRoomPicker_C"));
			auto* Picker = PickerClass ? CreateWidget<UCastleWildcardRoomPickerWidget>(Player, PickerClass) : nullptr;
			if (!TestTrue(TEXT("绑定工坊正式界面"), Picker && Picker->BindReward(Pending, Reason))) { return true; }
			Picker->AddToViewport(55);
			if (!TestTrue(TEXT("待领奖励展开工坊"), Click(*Picker, TEXT("ClaimButton")))) { return true; }
			const auto Select = [&](FName Name, int32 Index)
			{
				const auto* Choices = Cast<UPanelWidget>(Picker->GetWidgetFromName(Name));
				auto* Button = Choices && Index < Choices->GetChildrenCount() ? Cast<UButton>(Choices->GetChildAt(Index)) : nullptr;
				if (!TestNotNull(Name.ToString(), Button)) { return false; }
				Button->OnClicked.Broadcast();
				return true;
			};
			auto* CardSlot = Cast<USizeBox>(Picker->GetWidgetFromName(TEXT("PreviewCardSlot")));
			if (!TestNotNull(TEXT("工坊卡牌槽"), CardSlot)) { return true; }
			TestNull(TEXT("完全自定义房打开时没有默认预览"), CardSlot->GetContent());
			if (!Select(TEXT("DefinitionChoices"), 0) || !Select(TEXT("ColorChoices"), 1)) { return true; }
			TestNull(TEXT("仍缺数字时保持线稿"), CardSlot->GetContent());
			if (!Select(TEXT("NumberChoices"), 2)) { return true; }
			const auto* Card = Cast<UCastleShopCardWidget>(CardSlot->GetContent());
			if (!TestNotNull(TEXT("完全自定义房三项选齐后挂载实际卡牌"), Card)) { return true; }
			TestEqual(TEXT("完全自定义房显示黄色"), Card->GetDescriptor().Color, ECastleTileColor::Yellow);
			TestEqual(TEXT("完全自定义房显示选中数字"), Card->GetDescriptor().Number, 3);
			const auto Class = LoadClass<UCastleRewardChoiceWidget>(nullptr,
				TEXT("/Game/MovingCastle/UI/Director/WBP_RewardChoice.WBP_RewardChoice_C"));
			// 独立挂载同一个正式奖励界面来测几何，奖励事务仍由导演端到端用例覆盖。
			auto* Choice = Class ? CreateWidget<UCastleRewardChoiceWidget>(Player, Class) : nullptr;
			if (!TestNotNull(TEXT("可测量正式奖励界面"), Choice)) { return true; }
			Choice->AddToViewport(58);
			RewardPreview = Choice;
			return false;
		}
		if (Frames <= 6) { return false; }

		const auto Measure = [&](const UUserWidget* Widget, FName Name, const TCHAR* Label)
		{
			const UWidget* Panel = Widget ? Widget->GetWidgetFromName(Name) : nullptr;
			if (!TestNotNull(FString::Printf(TEXT("%s 的面板"), Label), Panel)) { return; }
			const FGeometry& Geometry = Panel->GetCachedGeometry();
			FVector2D Min, Max, Unused;
			USlateBlueprintLibrary::LocalToViewport(World, Geometry, FVector2D::ZeroVector, Min, Unused);
			USlateBlueprintLibrary::LocalToViewport(World, Geometry, Geometry.GetLocalSize(), Max, Unused);
			const FVector2D Size = Max - Min;
			AddInfo(FString::Printf(TEXT("FORTRESS_BOUNDS %s [%.2f,%.2f]–[%.2f,%.2f] %.2fx%.2f"),
				Label, Min.X, Min.Y, Max.X, Max.Y, Size.X, Size.Y));
			TestTrue(FString::Printf(TEXT("%s 在视口内使用完整弹窗空间"), Label),
				Size.X > 400 && Size.Y > 350 && Min.X >= 0 && Min.Y >= 0 && Max.X <= 1281 && Max.Y <= 721);
		};
		int32 Width, Height;
		Player->GetViewportSize(Width, Height);
		TestEqual(TEXT("主地图使用 1280×720 渲染目标"), FIntPoint(Width, Height), FIntPoint(1280, 720));
		Measure(FindScreenWidget<UCastleWildcardRoomPickerWidget>(*World), TEXT("PickerPanel"), TEXT("WBP_WildcardRoomPicker"));
		Measure(FindScreenWidget<UCastleControlSettingsWidget>(*World), TEXT("FortressPanel"), TEXT("WBP_ControlSettings"));
		Measure(RewardPreview.Get(), TEXT("FortressPanel"), TEXT("WBP_RewardChoice"));
		if (auto* Reward = RewardPreview.Get()) { Reward->RemoveFromParent(); }
		return true;
	}));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(StopPIE()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDialoguePresentationTest, "MovingCastle.Dialogue.PresentationAndGuide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDialoguePresentationTest::RunTest(const FString&)
{
	if (!StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Deadline = FPlatformTime::Seconds() + 30.0]()
	{
		UWorld* World = GEditor->PlayWorld;
		auto* Controller = World ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
		auto* HUD = World ? FindScreenWidget<UCastleDirectorHUDWidget>(*World) : nullptr;
		if (!Controller || !HUD)
		{
			if (FPlatformTime::Seconds() < Deadline) { return false; }
			AddError(TEXT("MainMap 正式 HUD 未挂载"));
			return true;
		}
		auto* Dialogue = World->GetGameInstance()->GetSubsystem<UCastleDialogueSubsystem>();
		auto* Guide = World->GetGameInstance()->GetSubsystem<UCastleGuideSubsystem>();
		auto* Opening = LoadObject<UCastleDialogueScriptDataAsset>(nullptr,
			TEXT("/Game/MovingCastle/Data/Tutorial/DA_Tutorial_S01.DA_Tutorial_S01"));
		auto* Branch = LoadObject<UCastleDialogueScriptDataAsset>(nullptr,
			TEXT("/Game/MovingCastle/Data/Tutorial/DA_Tutorial_S09.DA_Tutorial_S09"));
		FText Reason;
		Controller->SetControlMode(ECastleControlMode::Classic, false);
		if (!TestTrue(TEXT("保存后的开场剧本可以播放"), Dialogue->PlayScript(Opening, Controller, Reason))) { return true; }
		auto* Widget = FindScreenWidget<UCastleDialogueWidget>(*World);
		if (!TestNotNull(TEXT("存在对话 WBP"), Widget)) { return true; }
		auto* Left = Cast<UImage>(Widget->GetWidgetFromName(TEXT("LeftPortrait")));
		auto* Right = Cast<UImage>(Widget->GetWidgetFromName(TEXT("RightPortrait")));
		if (!TestTrue(TEXT("左右槽都已绑定且首行只显示城主"), Left && Right
			&& Left->GetVisibility() == ESlateVisibility::HitTestInvisible && Right->GetVisibility() == ESlateVisibility::Collapsed)) { return true; }
		TestTrue(TEXT("模态阻止飞行和转视角"), Controller->IsModalInputActive() && Controller->IsLookInputIgnored()
			&& Controller->IsMoveInputIgnored() && Controller->bShowMouseCursor);
		const FString FirstLine = Dialogue->GetResolvedCurrentLineText().ToString();
		TestFalse(TEXT("首行从未揭示完成开始"), Widget->IsCurrentLineFullyRevealed());
		Click(*Widget, TEXT("ContinueButton"));
		TestEqual(TEXT("第一次点击仍是同一句"), Dialogue->GetResolvedCurrentLineText().ToString(), FirstLine);
		TestTrue(TEXT("第一次点击补全文字"), Widget->IsCurrentLineFullyRevealed());
		Click(*Widget, TEXT("ContinueButton"));
		TestNotEqual(TEXT("第二次点击才换到下一行"), Dialogue->GetResolvedCurrentLineText().ToString(), FirstLine);
		Dialogue->AdvanceDialogueForAutomation();
		TestEqual(TEXT("旺财出场后右侧显示且不截获点击"), Right->GetVisibility(), ESlateVisibility::HitTestInvisible);
		TestEqual(TEXT("当前发言侧高亮"), Right->GetColorAndOpacity().R, 1.0f);
		TestEqual(TEXT("非发言侧降低明度"), Left->GetColorAndOpacity().R, 0.45f);
		const FText Keys = INVTEXT("{PlaceKey}|{DemolishKey}");
		TestEqual(TEXT("经典动态按键"), UCastleDialogueSubsystem::ResolveInputText(Keys, *Controller).ToString(), FString(TEXT("鼠标右键|鼠标左键")));
		Controller->SetControlMode(ECastleControlMode::Design, false);
		TestEqual(TEXT("设计动态按键"), UCastleDialogueSubsystem::ResolveInputText(Keys, *Controller).ToString(), FString(TEXT("鼠标左键|鼠标右键")));
		while (Dialogue->IsDialoguePlaying()) { Dialogue->AdvanceDialogueForAutomation(); }
		TestTrue(TEXT("结束后恢复设计模式鼠标"), !Controller->IsModalInputActive()
			&& !Controller->IsLookInputIgnored() && !Controller->IsMoveInputIgnored() && Controller->bShowMouseCursor);
		for (int32 Option = 0; Option < 2; ++Option)
		{
			if (!TestTrue(TEXT("分支剧本可以播放"), Dialogue->PlayScript(Branch, Controller, Reason))) { return true; }
			Dialogue->AdvanceDialogueForAutomation();
			Widget = FindScreenWidget<UCastleDialogueWidget>(*World);
			if (!Widget || !Click(*Widget, TEXT("ContinueButton"))) { AddError(TEXT("分支界面未挂载")); return true; }
			auto* Choices = Cast<UVerticalBox>(Widget->GetWidgetFromName(TEXT("ChoiceContainer")));
			auto* Choice = Choices ? Cast<UButton>(Choices->GetChildAt(Option)) : nullptr;
			if (!TestTrue(TEXT("正文揭示后显示两项"), Choices && Choice && Choices->GetVisibility() == ESlateVisibility::Visible))
			{
				return true;
			}
			// 分支已可见时，再点全屏推进层不能跳过当前行。
			const FString ChoiceLine = Dialogue->GetResolvedCurrentLineText().ToString();
			Click(*Widget, TEXT("ContinueButton"));
			TestEqual(TEXT("选择前点击空白不跳过分支行"), Dialogue->GetResolvedCurrentLineText().ToString(), ChoiceLine);
			Choice->OnClicked.Broadcast();
			if (Option == 1)
			{
				TestTrue(TEXT("B 先跳转到旺财"), Dialogue->GetResolvedCurrentLineText().ToString().StartsWith(TEXT("让小姐去吧")));
				Dialogue->AdvanceDialogueForAutomation();
			}
			TestTrue(TEXT("A 与 B 汇合至城主应答"), Dialogue->GetResolvedCurrentLineText().ToString().StartsWith(TEXT("什么，竟然")));
			while (Dialogue->IsDialoguePlaying()) { Dialogue->AdvanceDialogueForAutomation(); }
		}
		Controller->SetControlMode(ECastleControlMode::Classic, false);
		if (!TestTrue(TEXT("正式图文说明可挂载"), Guide->ShowEntryPopup(TEXT("Demolish"), Controller, Reason))) { return true; }
		auto* Popup = FindScreenWidget<UCastleGuidePopupWidget>(*World);
		auto* Illustration = Popup ? Cast<UImage>(Popup->GetWidgetFromName(TEXT("Illustration"))) : nullptr;
		TestTrue(TEXT("可选配图未交付时折叠"), Illustration && Illustration->GetVisibility() == ESlateVisibility::Collapsed);
		const auto* PopupDescription = Popup ? Cast<UTextBlock>(Popup->GetWidgetFromName(TEXT("EntryDescription"))) : nullptr;
		if (!TestNotNull(TEXT("说明正文绑定"), PopupDescription)) { return true; }
		TestTrue(TEXT("经典模式弹窗解析拆除键"), PopupDescription->GetText().ToString().Contains(TEXT("鼠标左键")));
		Controller->SetControlMode(ECastleControlMode::Design, false);
		TestTrue(TEXT("打开期间弹窗刷新拆除键"), PopupDescription->GetText().ToString().Contains(TEXT("鼠标右键")));
		Controller->SetControlMode(ECastleControlMode::Classic, false);
		TestTrue(TEXT("关闭前未解锁拆除"), !Guide->IsEntryUnlocked(TEXT("Demolish")) && Controller->IsModalInputActive());
		if (!Popup || !Click(*Popup, TEXT("CloseButton"))) { AddError(TEXT("图文说明关闭失败")); return true; }
		TestTrue(TEXT("关闭后入册并恢复经典模式鼠标"), Guide->IsEntryUnlocked(TEXT("Demolish"))
			&& !Controller->IsModalInputActive() && !Controller->bShowMouseCursor);
		// 问号是带皮肤的 UUserWidget，走正式图标按钮的点击委托。
		auto* GuideButton = Cast<UCastleHUDIconButtonWidget>(HUD->GetWidgetFromName(TEXT("GuideButton")));
		if (!TestNotNull(TEXT("正式问号图标按钮"), GuideButton)) { return true; }
		GuideButton->OnButtonClicked.Broadcast();
		if (!TestTrue(TEXT("通过正式 HUD 的问号打开百科"), Guide->HasOpenModal())) { return true; }
		auto* Book = FindScreenWidget<UCastleGuideBookWidget>(*World);
		const auto* BookDescription = Book ? Cast<UTextBlock>(Book->GetWidgetFromName(TEXT("EntryDescription"))) : nullptr;
		if (!TestNotNull(TEXT("百科正文绑定"), BookDescription)) { return true; }
		TestTrue(TEXT("MainMap 基础建造词条解析放置键"), BookDescription->GetText().ToString().Contains(TEXT("鼠标右键")));
		if (!Book || !Click(*Book, TEXT("NextButton"))) { AddError(TEXT("百科翻页控件未挂载")); return true; }
		const auto* Title = Cast<UTextBlock>(Book->GetWidgetFromName(TEXT("EntryTitle")));
		TestTrue(TEXT("百科展示已收录词条"), Title && Title->GetText().ToString() == TEXT("拆除与回收"));
		Controller->SetControlMode(ECastleControlMode::Design, false);
		TestTrue(TEXT("书册打开期间刷新拆除键"), BookDescription->GetText().ToString().Contains(TEXT("鼠标右键"))
			&& !BookDescription->GetText().ToString().Contains(TEXT("{DemolishKey}")));
		GuideButton->OnButtonClicked.Broadcast();
		TestFalse(TEXT("再次点击问号关闭百科并释放模态"), Guide->HasOpenModal() || Controller->IsModalInputActive());
		return true;
	}));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(StopPIE()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDialogueTerminationTest, "MovingCastle.Dialogue.ScriptTermination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleDialogueTerminationTest::RunTest(const FString&)
{
	auto* Script = NewObject<UCastleDialogueScriptDataAsset>();
	for (const FName Id : {FName(TEXT("Start")), FName(TEXT("Branch")), FName(TEXT("End"))})
	{
		auto& Line = Script->Lines.AddDefaulted_GetRef();
		Line.LineId = Id;
		Line.SpeakerName = INVTEXT("旺财");
		Line.TextTemplate = INVTEXT("台词");
	}
	Script->Lines.Last().bEndsScript = true;
	Script->Lines[0].Choices = {{INVTEXT("继续"), TEXT("Branch")}, {INVTEXT("结束"), TEXT("End")}};
	Script->Lines[1].Choices = {{INVTEXT("返回"), TEXT("Start")}};
	AddExpectedError(TEXT("存在无法保证结束的循环分支"), EAutomationExpectedErrorFlags::Contains, 1);
	FText Reason;
	TestFalse(TEXT("可选择结束仍不能掩盖循环选项"), Script->ValidateScript(Reason));
	Script->Lines[1].Choices[0].TargetLineId = TEXT("End");
	TestTrue(TEXT("所有分支可汇合到末行"), Script->ValidateScript(Reason));
	return true;
}
#endif
