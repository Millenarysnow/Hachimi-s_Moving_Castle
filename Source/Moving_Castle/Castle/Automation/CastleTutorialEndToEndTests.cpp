#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Castle/Dialogue/CastleDialogueScriptDataAsset.h"
#include "Castle/Dialogue/CastleDialogueSubsystem.h"
#include "Castle/Dialogue/CastleDialogueWidget.h"
#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Guide/CastleGuideSubsystem.h"
#include "Castle/Guide/CastleGuideWidgets.h"
#include "Castle/Tutorial/CastleTutorialConfigDataAsset.h"
#include "Castle/Tutorial/CastleTutorialDirector.h"
#include "Castle/Tutorial/CastleTutorialWidgets.h"
#include "Castle/UI/Common/CastleHUDControls.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleNavigationSubsystem.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "CastleShopWidget.h"
#include "CastleTowerDispatchSubsystem.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Interaction/CastlePatrolRoute.h"
#include "MassEntityConfigAsset.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"

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

	bool StartPIE(FAutomationTestBase& Test, const TCHAR* Map)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(Map, FPackageName::GetMapPackageExtension());
		// 通用 PIE 测试可能把地图留在上一个用例中。原生地图加载会关闭该 PIE 后再重新创建实例。
		if (!Test.TestTrue(TEXT("载入磁盘上的正式地图"), FEditorFileUtils::LoadMap(Filename, false, false)))
		{
			return false;
		}
		auto* Settings = DuplicateObject(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
		Settings->SetPlayNetMode(PIE_Standalone);
		Settings->SetPlayNumberOfClients(1);
		Settings->SetRunUnderOneProcess(true);
		FRequestPlaySessionParams Params;
		Params.EditorPlaySettings = Settings;
		Params.bAllowOnlineSubsystem = false;
		GEditor->RequestPlaySession(Params);
		return true;
	}

	FFunctionLatentCommand StopPIE()
	{
		return FFunctionLatentCommand([bRequested = false]() mutable
		{
			if (!GEditor->PlayWorld)
			{
				if (!bRequested) { GEditor->CancelRequestPlaySession(); }
				return true;
			}
			if (!bRequested)
			{
				GEditor->RequestEndPlayMap();
				bRequested = true;
			}
			return false;
		});
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
		auto* Controller = World ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Controller || !Controller->GetPawn()) { return Wait(TEXT("教学地图 PIE 未就绪")); }
		TActorIterator<ACastleTutorialDirector> Tutorial(World);
		TActorIterator<ACastleBoard> Board(World);
		TActorIterator<ACastlePatrolRoute> Route(World);
		if (!Tutorial || !Board || !Route) { return Wait(TEXT("教学导演、棋盘或路线入口缺失")); }
		const int32 Step = Tutorial->GetCurrentStep();
		if (!Tutorial->IsActorTickEnabled() && !Tutorial->HasCompletedTutorial())
		{
			Test.AddError(FString::Printf(TEXT("教学导演在 S%d 停止。"), Step));
			return true;
		}
		if (Step == 0) { return Wait(TEXT("教学导演未初始化")); }
		if (Tutorial->HasCompletedTutorial())
		{
			Test.TestTrue(TEXT("S10 已实际派出城主并挂载气泡"), Tutorial->HasShownDispatchBubble());
			Test.TestEqual(TEXT("完成于 S14"), Step, 14);
			Test.TestEqual(TEXT("摆上农田后仍使用 S8 原始路点"),
				World->GetSubsystem<UCastleNavigationSubsystem>()->GetRequestedWaypoints(), OriginalWaypoints);
			return true;
		}
		if (Step != LastStep)
		{
			Test.TestEqual(TEXT("逐步推进，无跳步"), Step, LastStep + 1);
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
			auto* Widget = FindScreenWidget<UCastleDialogueWidget>(*World);
			auto* Choices = Widget ? Cast<UVerticalBox>(Widget->GetWidgetFromName(TEXT("ChoiceContainer"))) : nullptr;
			if (!Choices) { return Fail(TEXT("读取不到保存后的对话控件")); }
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
				auto* Dispatch = World->GetSubsystem<UCastleTowerDispatchSubsystem>();
				Test.TestFalse(TEXT("手动点塔前拒绝开启自动出兵"), Dispatch->SetAutoDispatchEnabled(true));
				Test.TestFalse(TEXT("拒绝自动出兵时尚未产生城主气泡"), Tutorial->HasShownDispatchBubble());
				if (!Dispatch->BeginManualDispatch(Reason)) { return Fail(Reason.ToString()); }
				Dispatch->StopManualDispatch();
			}
			break;
		case 11:
		{
			auto* Reward = FindScreenWidget<UCastleTutorialRewardWidget>(*World);
			if (!Reward) { return Fail(TEXT("教学三选一未挂载")); }
			if (Action == 0)
			{
				if (!Click(*Reward, TEXT("CopyButton"))) { return Fail(TEXT("找不到第三项奖励")); }
				++Action;
			}
			else if (Action == 1)
			{
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
			&& Left->GetVisibility() == ESlateVisibility::Visible && Right->GetVisibility() == ESlateVisibility::Collapsed)) { return true; }
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
		TestEqual(TEXT("旺财出场后右侧显示"), Right->GetVisibility(), ESlateVisibility::Visible);
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
