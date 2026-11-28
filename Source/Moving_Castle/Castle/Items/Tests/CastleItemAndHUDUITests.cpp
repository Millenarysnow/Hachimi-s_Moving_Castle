#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "Castle/Items/CastleBadgeDataAsset.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/Items/CastleItemGrantLibrary.h"
#include "Castle/SkyIslands/Shop/CastleShopSkyIslandDataAsset.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Castle/UI/Items/CastleBadgeBarWidget.h"
#include "Castle/UI/Items/CastleBadgeReplaceModalWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Units/Navigation/Interaction/CastlePatrolRoute.h"

namespace
{
	/** 每个测试使用独立的游戏实例，避开主地图启动和其他测试的库存。 */
	struct FCastleHUDTestGame
	{
		UGameInstance* Game = NewObject<UGameInstance>(GEngine);

		FCastleHUDTestGame()
		{
			Game->InitializeStandalone();
		}

		~FCastleHUDTestGame()
		{
			if (UWorld* World = Game->GetWorld())
			{
				Game->Shutdown();
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBadgeIconUITest, "MovingCastle.Items.UI.BadgeIcons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleBadgeIconUITest::RunTest(const FString&)
{
	const FCastleHUDTestGame TestGame;
	UGameInstance* Game = TestGame.Game;
	const auto BarClass = LoadClass<UCastleBadgeBarWidget>(nullptr,
		TEXT("/Game/MovingCastle/UI/Items/WBP_BadgeBar.WBP_BadgeBar_C"));
	const auto ModalClass = LoadClass<UCastleBadgeReplaceModalWidget>(nullptr,
		TEXT("/Game/MovingCastle/UI/Items/WBP_BadgeReplaceModal.WBP_BadgeReplaceModal_C"));
	if (!TestNotNull(TEXT("徽章栏蓝图类"), BarClass) || !TestNotNull(TEXT("替换弹窗蓝图类"), ModalClass)) { return false; }
	auto* Bar = CreateWidget<UCastleBadgeBarWidget>(Game, BarClass);
	auto* Modal = CreateWidget<UCastleBadgeReplaceModalWidget>(Game, ModalClass);
	auto* Badges = Game->GetSubsystem<UCastleBadgeSubsystem>();
	if (!TestNotNull(TEXT("创建正式徽章栏"), Bar) || !TestNotNull(TEXT("创建正式替换弹窗"), Modal)
		|| !TestNotNull(TEXT("游戏实例徽章子系统"), Badges)) { return false; }
	if (!TestTrue(TEXT("徽章栏六槽绑定"), Bar->BindBadges(Badges))
		|| !TestTrue(TEXT("替换弹窗图标与按钮绑定"), Modal->BindBadges(Badges))) { return false; }

	auto* BarRows = Cast<UPanelWidget>(Bar->GetWidgetFromName(TEXT("BadgeSlots")));
	auto* ModalRows = Cast<UPanelWidget>(Modal->GetWidgetFromName(TEXT("ReplaceSlots")));
	auto* ModalIcons = Cast<UPanelWidget>(Modal->GetWidgetFromName(TEXT("ReplaceIcons")));
	auto* PendingIcon = Cast<UImage>(Modal->GetWidgetFromName(TEXT("PendingBadgeIcon")));
	auto* PendingLabel = Cast<UTextBlock>(Modal->GetWidgetFromName(TEXT("PendingBadgeLabel")));
	auto* Discard = Cast<UButton>(Modal->GetWidgetFromName(TEXT("DiscardButton")));
	if (!TestNotNull(TEXT("徽章栏容器"), BarRows) || !TestNotNull(TEXT("替换按钮容器"), ModalRows)
		|| !TestNotNull(TEXT("替换图标层"), ModalIcons) || !TestNotNull(TEXT("候选徽章图标"), PendingIcon)
		|| !TestNotNull(TEXT("候选徽章文案"), PendingLabel) || !TestNotNull(TEXT("舍弃按钮"), Discard)) { return false; }

	const int32 SlotCount = UCastleBadgeSubsystem::MaxBadges;
	auto* FrameTexture = LoadObject<UTexture2D>(nullptr,
		TEXT("/Game/MovingCastle/UI/Textures/T_UI_BadgeSlot.T_UI_BadgeSlot"));
	if (!TestNotNull(TEXT("正式六边形槽底图"), FrameTexture)) { return false; }
	TestEqual(TEXT("栏根允许子槽接收悬停"), Bar->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
	TestEqual(TEXT("商店外徽章栏固定六槽"), BarRows->GetChildrenCount(), 6);
	TestEqual(TEXT("弹窗固定六个替换按钮"), ModalRows->GetChildrenCount(), 6);
	TestEqual(TEXT("每个替换按钮有对应图标"), ModalIcons->GetChildrenCount(), 6);
	TestEqual(TEXT("图标层不截获替换按钮点击"), ModalIcons->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("没有候选徽章时弹窗折叠"), Modal->GetVisibility(), ESlateVisibility::Collapsed);

	TArray<UImage*> BarSlotIcons;
	TArray<UImage*> BarSlotFrames;
	TArray<UOverlay*> BarSlots;
	TArray<UImage*> ModalSlotIcons;
	TArray<UButton*> ModalSlotButtons;
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		auto* Slot = Cast<UOverlay>(BarRows->GetChildAt(Index));
		if (!TestNotNull(TEXT("栏内叠层槽"), Slot)) { return false; }
		auto* Frame = Cast<UImage>(Slot->GetChildAt(0));
		auto* Icon = Cast<UImage>(Slot->GetChildAt(1));
		auto* ModalIcon = Cast<UImage>(ModalIcons->GetChildAt(Index));
		auto* Button = Cast<UButton>(ModalRows->GetChildAt(Index));
		if (!TestNotNull(TEXT("栏内图标"), Icon) || !TestNotNull(TEXT("栏内底框"), Frame)
			|| !TestNotNull(TEXT("弹窗槽图标"), ModalIcon) || !TestNotNull(TEXT("弹窗槽按钮"), Button)) { return false; }
		BarSlotIcons.Add(Icon);
		BarSlotFrames.Add(Frame);
		BarSlots.Add(Slot);
		ModalSlotIcons.Add(ModalIcon);
		ModalSlotButtons.Add(Button);
		TestEqual(TEXT("空槽隐藏图标"), Icon->GetVisibility(), ESlateVisibility::Hidden);
		TestNull(TEXT("空栏槽无图标资源"), Icon->GetBrush().GetResourceObject());
		TestTrue(TEXT("空槽正确显示六边形底框"), Frame->GetBrush().GetResourceObject() == FrameTexture);
		TestEqual(TEXT("空槽底框为 58 像素"), FVector2D(Frame->GetBrush().GetImageSize()), FVector2D(58, 58));
		TestEqual(TEXT("空槽不截获鼠标"), Slot->GetVisibility(), ESlateVisibility::HitTestInvisible);
		TestTrue(TEXT("空槽无提示"), Slot->GetToolTipText().IsEmpty());
		TestEqual(TEXT("空弹窗槽隐藏图标"), ModalIcon->GetVisibility(), ESlateVisibility::Hidden);
	}

	const auto* Catalog = LoadObject<UCastleShopSkyIslandDataAsset>(nullptr,
		TEXT("/Game/MovingCastle/Data/SkyIslands/DA_ShopSkyIsland.DA_ShopSkyIsland"));
	if (!TestNotNull(TEXT("正式商店目录"), Catalog)
		|| !TestEqual(TEXT("正式目录固定 25 枚徽章"), Catalog->BadgePool.Num(), 25)) { return false; }
	// 用正式资产逐一进入库存。真实 PNG 的对象身份可以检出空图标、串槽和候选错绑。
	TArray<FCastleBadgeDefinition> Awards;
	FText Reason;
	for (int32 Index = 0; Index <= SlotCount; ++Index)
	{
		const auto* Asset = Catalog->BadgePool[Index].Get();
		bool bNeedsReplacement = false;
		if (!TestTrue(GetPathNameSafe(Asset) + TEXT(" 通过正式入口发放"),
			UCastleItemGrantLibrary::TryGrantBadge(Asset, Badges, bNeedsReplacement, Reason)))
		{
			AddError(Reason.ToString());
			return false;
		}
		Awards.Add(Asset->Badge);
		TestEqual(TEXT("只有第七枚进入替换状态"), bNeedsReplacement, Awards.Num() > SlotCount);
	}

	TestEqual(TEXT("第七枚进入候选区"), Modal->GetVisibility(), ESlateVisibility::Visible);
	const FCastleBadgeDefinition& Candidate = Awards.Last();
	TestTrue(TEXT("候选笔刷引用第七张纹理"), PendingIcon->GetBrush().GetResourceObject() == Candidate.Icon.Get());
	TestEqual(TEXT("候选图标处于显示状态"), PendingIcon->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestTrue(TEXT("候选区显示名称"), PendingLabel->GetText().ToString().Contains(Candidate.GetDisplayName().ToString()));
	TestTrue(TEXT("候选区显示效果"), PendingLabel->GetText().ToString().Contains(Candidate.GetEffectSummary().ToString()));
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		const FCastleBadgeDefinition& Owned = Awards[Index];
		TestTrue(TEXT("徽章栏笔刷对应正确槽位"), BarSlotIcons[Index]->GetBrush().GetResourceObject() == Owned.Icon.Get());
		TestEqual(TEXT("占用槽显示图标"), BarSlotIcons[Index]->GetVisibility(), ESlateVisibility::HitTestInvisible);
		TestTrue(TEXT("替换弹窗笔刷与徽章栏同步"), ModalSlotIcons[Index]->GetBrush().GetResourceObject() == Owned.Icon.Get());
		TestEqual(TEXT("替换槽显示图标"), ModalSlotIcons[Index]->GetVisibility(), ESlateVisibility::HitTestInvisible);
		const UTextBlock* ModalLabel = Cast<UTextBlock>(ModalSlotButtons[Index]->GetContent());
		if (!TestNotNull(TEXT("原替换按钮保留文本子控件"), ModalLabel)) { return false; }
		TestEqual(TEXT("占用槽接收鼠标悬停"), BarSlots[Index]->GetVisibility(), ESlateVisibility::Visible);
		for (const FText& SlotText : {BarSlots[Index]->GetToolTipText(), ModalLabel->GetText()})
		{
			TestTrue(TEXT("槽显示该徽章的名称"), SlotText.ToString().Contains(Owned.GetDisplayName().ToString()));
			TestTrue(TEXT("槽显示该徽章的效果"), SlotText.ToString().Contains(Owned.GetEffectSummary().ToString()));
		}
	}

	// 点击末槽可覆盖索引边界。下一笔待替换弹出时应立即反映已完成的替换。
	ModalSlotButtons.Last()->OnClicked.Broadcast();
	TestEqual(TEXT("替换提交后弹窗折叠"), Modal->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("按钮只替换选中的末槽"), BarSlotIcons.Last()->GetBrush().GetResourceObject() == Candidate.Icon.Get());
	TestEqual(TEXT("末槽提示同步替换后的名称和效果"), BarSlots.Last()->GetToolTipText().ToString(),
		Candidate.GetDisplayName().ToString() + TEXT("\n") + Candidate.GetEffectSummary().ToString());
	TestTrue(TEXT("首槽保持原图标"), BarSlotIcons[0]->GetBrush().GetResourceObject() == Awards[0].Icon.Get());
	bool bNeedsReplacement = false;
	if (!TestTrue(TEXT("满槽时可打开下一笔替换"),
		UCastleItemGrantLibrary::TryGrantBadge(Catalog->BadgePool[1], Badges, bNeedsReplacement, Reason))) { return false; }
	TestTrue(TEXT("弹窗同步已经替换的末槽"), ModalSlotIcons.Last()->GetBrush().GetResourceObject() == Candidate.Icon.Get());
	Discard->OnClicked.Broadcast();
	TestEqual(TEXT("舍弃候选后弹窗折叠"), Modal->GetVisibility(), ESlateVisibility::Collapsed);
	TestNull(TEXT("候选关闭后清除预览资源"), PendingIcon->GetBrush().GetResourceObject());
	TestTrue(TEXT("舍弃候选保留持有图标"), BarSlotIcons.Last()->GetBrush().GetResourceObject() == Candidate.Icon.Get());
	for (int32 Index = SlotCount + 1; Index < Catalog->BadgePool.Num(); ++Index)
	{
		const auto* Asset = Catalog->BadgePool[Index].Get();
		if (!TestTrue(GetPathNameSafe(Asset) + TEXT(" 发放到满槽候选"),
			UCastleItemGrantLibrary::TryGrantBadge(Asset, Badges, bNeedsReplacement, Reason) && bNeedsReplacement)) { return false; }
		TestTrue(Asset->GetName() + TEXT(" 候选笔刷使用该资产的 PNG"), PendingIcon->GetBrush().GetResourceObject() == Asset->Badge.Icon.Get());
		ModalSlotButtons.Last()->OnClicked.Broadcast();
		TestTrue(Asset->GetName() + TEXT(" 正式替换后徽章栏显示该 PNG"), BarSlotIcons.Last()->GetBrush().GetResourceObject() == Asset->Badge.Icon.Get());
	}
	// 破坏副本资源，验证发放边界报错且界面仍持有已提交的正式库存。
	auto* MissingIcon = DuplicateObject(Catalog->BadgePool[0].Get(), GetTransientPackage());
	MissingIcon->Badge.Icon = nullptr;
	AddExpectedError(TEXT("徽章缺少有效的 Badge.Icon PNG"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("缺图标拒绝发放且不创建替换候选"),
		UCastleItemGrantLibrary::TryGrantBadge(MissingIcon, Badges, bNeedsReplacement, Reason));
	TestEqual(TEXT("拒绝后替换弹窗仍折叠"), Modal->GetVisibility(), ESlateVisibility::Collapsed);
	Badges->ClearAllBadgesForTesting();
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		TestEqual(TEXT("重置徽章后隐藏图标"), BarSlotIcons[Index]->GetVisibility(), ESlateVisibility::Hidden);
		TestNull(TEXT("重置徽章后释放画刷资源"), BarSlotIcons[Index]->GetBrush().GetResourceObject());
		TestTrue(TEXT("重置徽章后清除提示"), BarSlots[Index]->GetToolTipText().IsEmpty());
		TestEqual(TEXT("重置徽章后不截获鼠标"), BarSlots[Index]->GetVisibility(), ESlateVisibility::HitTestInvisible);
	}

	// 破坏本测试实例的控件树，实际资产保持原样。缺失绑定必须拒绝重新绑定。
	BarSlotFrames[0]->SetBrushResourceObject(nullptr);
	AddExpectedError(TEXT("缺少有效底框纹理 T_UI_BadgeSlot"), EAutomationExpectedErrorFlags::Contains, 2);
	Badges->ClearAllBadgesForTesting();
	TestEqual(TEXT("刷新发现底框破损时折叠整栏"), Bar->GetVisibility(), ESlateVisibility::Collapsed);
	TestFalse(TEXT("没有底框纹理时拒绝绑定"), Bar->BindBadges(Badges));
	BarSlotFrames[0]->SetBrushResourceObject(FrameTexture);
	BarRows->RemoveChildAt(SlotCount - 1);
	AddExpectedError(TEXT("需要含六个子项的 BadgeSlots"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("缺少一个徽章槽时拒绝绑定"), Bar->BindBadges(Badges));

	// 绕过正式发放库，向隔离库存注入坏徽章来覆盖徽章栏自身的资源校验。
	auto* InvalidBadgeSource = NewObject<UCastleBadgeSubsystem>(Game);
	auto* InvalidBadgeBar = CreateWidget<UCastleBadgeBarWidget>(Game, BarClass);
	if (!TestNotNull(TEXT("创建隔离的徽章栏"), InvalidBadgeBar)) { return false; }
	AddExpectedError(TEXT("缺少有效 Icon"), EAutomationExpectedErrorFlags::Contains, 1);
	if (!TestTrue(TEXT("隔离库存可以接收测试徽章"),
		InvalidBadgeSource->TryAcquireBadge(MissingIcon->Badge, bNeedsReplacement, Reason))) { return false; }
	TestFalse(TEXT("持有缺图标徽章时拒绝绑定"), InvalidBadgeBar->BindBadges(InvalidBadgeSource));
	TestEqual(TEXT("缺图标徽章导致整栏折叠"), InvalidBadgeBar->GetVisibility(), ESlateVisibility::Collapsed);

	ModalIcons->RemoveChildAt(SlotCount - 1);
	AddExpectedError(TEXT("绑定失败：需要徽章源、候选图标和说明"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("缺少一个替换槽图标时拒绝绑定"), Modal->BindBadges(Badges));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePathEditWaypointHUDTest, "MovingCastle.Items.UI.PathEditWaypointCounter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePathEditWaypointHUDTest::RunTest(const FString&)
{
	const FCastleHUDTestGame TestGame;
	UWorld* World = TestGame.Game->GetWorld();
	const auto HUDClass = LoadClass<UCastleDirectorHUDWidget>(nullptr,
		TEXT("/Game/MovingCastle/UI/Director/WBP_DirectorHUD.WBP_DirectorHUD_C"));
	if (!TestNotNull(TEXT("独立游戏世界"), World) || !TestNotNull(TEXT("正式导演 HUD 蓝图类"), HUDClass)) { return false; }
	auto* HUD = CreateWidget<UCastleDirectorHUDWidget>(TestGame.Game, HUDClass);
	auto* Route = World->SpawnActor<ACastlePatrolRoute>();
	auto* Tower = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("创建正式导演 HUD"), HUD) || !TestNotNull(TEXT("创建路线编辑 Actor"), Route)
		|| !TestNotNull(TEXT("创建测试主塔 Actor"), Tower)) { return false; }
	auto* Counter = Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("RemainingWaypointText")));
	if (!TestNotNull(TEXT("WBP 内的独立计数文字"), Counter)
		|| !TestTrue(TEXT("WBP 计数文字及其余绑定完整"), HUD->HasRequiredBindings())) { return false; }
	auto* Health = NewObject<UCastleMainTowerComponent>(Tower);
	auto* Towers = World->GetSubsystem<UCastleMainTowerSubsystem>();
	if (!TestTrue(TEXT("初始化测试主塔生命"), Health->InitializeHealth())
		|| !TestTrue(TEXT("主塔登记到世界子系统"), Towers && Towers->RegisterMainTower(Health))
		|| !TestTrue(TEXT("通过正式入口绑定 HUD"), HUD->BindMainTowerHealth(Health))) { return false; }

	// 进入正式路线 Actor 的 BeginPlay，再走 P 键调用的函数。草稿变动后立即读取，不推进一帧。
	Route->DispatchBeginPlay();
	TestEqual(TEXT("起始是建造模式"), HUD->GetCurrentScreenMode(), ECastleHUDScreenMode::Build);
	TestEqual(TEXT("建造模式隐藏计数"), Counter->GetVisibility(), ESlateVisibility::Collapsed);
	Route->ToggleEditing();
	TestEqual(TEXT("P 键入口切到路径编辑"), HUD->GetCurrentScreenMode(), ECastleHUDScreenMode::PathEdit);
	TestEqual(TEXT("进入路径编辑立即显示非交互计数"), Counter->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("进编辑时读现有上限"), Counter->GetText().ToString(), FString(TEXT("剩余路径点：2 / 2")));

	FText Reason;
	if (!TestTrue(TEXT("投影后的真实放点入口"), Route->TryAppendDraftWaypoint(FVector(0, 0, 0), Reason))) { return false; }
	TestEqual(TEXT("放点后实时扣减"), Counter->GetText().ToString(), FString(TEXT("剩余路径点：1 / 2")));
	Route->UndoDraftPoint();
	TestEqual(TEXT("Backspace 共用撤回后实时恢复"), Counter->GetText().ToString(), FString(TEXT("剩余路径点：2 / 2")));
	if (!TestTrue(TEXT("放置另一个可撤回点"), Route->TryAppendDraftWaypoint(FVector(100, 0, 0), Reason))) { return false; }
	TestTrue(TEXT("升级共用上限接口"), Route->AddMaxPlayerWaypoints(1));
	TestEqual(TEXT("上限增加仍保留已用点"), Counter->GetText().ToString(), FString(TEXT("剩余路径点：2 / 3")));
	Route->ClearPatrolRoute();
	TestEqual(TEXT("Delete 共用清空后实时刷新"), Counter->GetText().ToString(), FString(TEXT("剩余路径点：3 / 3")));
	Route->ToggleEditing();
	TestEqual(TEXT("取消路径编辑返回建造模式"), HUD->GetCurrentScreenMode(), ECastleHUDScreenMode::Build);
	TestEqual(TEXT("返回建造模式立即隐藏"), Counter->GetVisibility(), ESlateVisibility::Collapsed);
	Route->ToggleEditing();
	TestEqual(TEXT("重新进入时显示真实的新上限"), Counter->GetText().ToString(), FString(TEXT("剩余路径点：3 / 3")));

	// 独立战斗用正式战斗配置发起，保留路径 Actor 的编辑态以验证战斗在 HUD 中的优先级。
	auto* Combat = World->GetSubsystem<UCastleCombatSubsystem>();
	auto* Config = LoadObject<UCastleCombatConfigDataAsset>(nullptr, TEXT("/Game/MovingCastle/Data/Combat/DA_CombatConfig.DA_CombatConfig"));
	if (!TestNotNull(TEXT("正式战斗配置"), Config) || !TestNotNull(TEXT("世界战斗子系统"), Combat)
		|| !TestTrue(TEXT("初始化独立战斗"), Combat->InitializeCombat(Config, Reason))
		|| !TestTrue(TEXT("发起独立战斗"), Combat->StartBattle(ECastleBattleKind::Normal, 1, Reason)))
	{
		AddError(Reason.ToString());
		return false;
	}
	HUD->NativeTick(FGeometry(), 0.0f);
	TestEqual(TEXT("战斗优先于仍打开的路径编辑"), HUD->GetCurrentScreenMode(), ECastleHUDScreenMode::Battle);
	TestEqual(TEXT("战斗模式隐藏剩余点数"), Counter->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("隐藏时仍可收到上限广播"), Route->AddMaxPlayerWaypoints(1));
	TestEqual(TEXT("战斗状态下广播不能显示计数"), Counter->GetVisibility(), ESlateVisibility::Collapsed);
	return true;
}

#endif
