#include "CastleGuideSubsystem.h"

#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Dialogue/CastleDialogueSubsystem.h"
#include "Castle/Guide/CastleGuideDataAsset.h"
#include "Castle/Guide/CastleGuideWidgets.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Moving_Castle.h"

namespace
{
	constexpr TCHAR GuideDataPath[] = TEXT("/Game/MovingCastle/Data/Tutorial/DA_CastleGuideEntries.DA_CastleGuideEntries");
	constexpr TCHAR GuidePopupClassPath[] = TEXT("/Game/MovingCastle/UI/Dialogue/WBP_CastleGuidePopup.WBP_CastleGuidePopup_C");
	constexpr TCHAR GuideBookClassPath[] = TEXT("/Game/MovingCastle/UI/Dialogue/WBP_CastleGuideBook.WBP_CastleGuideBook_C");

	/** 可选图像槽与词条文案独立，空贴图不占任何版面。 */
	void SetGuideIllustration(UImage& Image, UTexture2D* Texture)
	{
		Image.SetBrushFromTexture(Texture);
		Image.SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

bool UCastleGuideSubsystem::ShowEntryPopup(const FName EntryId, ACastlePlayerController* Controller, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (ActivePopup || ActiveBook || !IsValid(Controller) || Controller->GetWorld() != GetWorld())
	{
		OutReason = FText::FromString(FString::Printf(TEXT("百科弹窗 %s 无法打开：已有百科模态，或当前玩家控制器无效。"), *EntryId.ToString()));
	}
	else if (!EnsureGuideData(OutReason))
	{
	}
	const FCastleGuideEntry* Entry = OutReason.IsEmpty() ? GuideData->FindEntry(EntryId) : nullptr;
	if (OutReason.IsEmpty() && !Entry)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("百科资产 %s 不包含必需条目 %s。"),
			*GetPathNameSafe(GuideData), *EntryId.ToString()));
	}
	UClass* WidgetClass = OutReason.IsEmpty() ? LoadRequiredWidgetClass(GuidePopupClassPath, OutReason) : nullptr;
	UCastleGuidePopupWidget* Widget = WidgetClass ? CreateWidget<UCastleGuidePopupWidget>(Controller, WidgetClass) : nullptr;
	if (OutReason.IsEmpty() && (!IsValid(Widget) || !Widget->Configure(this, *Entry) || !Widget->AddToPlayerScreen(71)))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("百科条目 %s 的弹窗 WBP 创建、绑定或挂载失败。"), *EntryId.ToString()));
	}
	if (!OutReason.IsEmpty())
	{
		if (IsValid(Widget)) { Widget->RemoveFromParent(); }
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	ActivePopup = Widget;
	BindModalController(*Controller);
	PendingEntryId = EntryId;
	Controller->SetPresentationModalState(true, Widget);
	Widget->SetKeyboardFocus();
	return true;
}

bool UCastleGuideSubsystem::ShowGuideBook(ACastlePlayerController* Controller, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (ActiveBook && ActiveController.Get() == Controller)
	{
		CloseGuideBook();
		return true;
	}
	if (ActivePopup || ActiveBook || !IsValid(Controller) || Controller->GetWorld() != GetWorld())
	{
		OutReason = INVTEXT("图文百科无法打开：已有百科模态，或当前玩家控制器无效。");
	}
	else if (!EnsureGuideData(OutReason))
	{
	}
	UClass* WidgetClass = OutReason.IsEmpty() ? LoadRequiredWidgetClass(GuideBookClassPath, OutReason) : nullptr;
	TArray<FCastleGuideEntry> UnlockedEntries;
	if (OutReason.IsEmpty())
	{
		for (const FCastleGuideEntry& Entry : GuideData->Entries)
		{
			if ((TutorialController.Get() != Controller && Entry.bAvailableFromStart) || UnlockedEntryIds.Contains(Entry.EntryId))
			{
				UnlockedEntries.Add(Entry);
			}
		}
	}
	UCastleGuideBookWidget* Widget = WidgetClass ? CreateWidget<UCastleGuideBookWidget>(Controller, WidgetClass) : nullptr;
	if (OutReason.IsEmpty() && (!IsValid(Widget) || !Widget->Configure(this, UnlockedEntries) || !Widget->AddToPlayerScreen(71)))
	{
		OutReason = INVTEXT("图文百科 WBP 创建、必需控件绑定或挂载失败。");
	}
	if (!OutReason.IsEmpty())
	{
		if (IsValid(Widget)) { Widget->RemoveFromParent(); }
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	ActiveBook = Widget;
	// 只让书册的面板区域拦截点击，左下角问号仍可关闭书册。
	Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	BindModalController(*Controller);
	Controller->SetPresentationModalState(true, Widget);
	Widget->SetKeyboardFocus();
	return true;
}

bool UCastleGuideSubsystem::BeginTutorial(ACastlePlayerController& Controller, TConstArrayView<FName> RequiredIds, FText& OutReason)
{
	if (!EnsureGuideData(OutReason)) { return false; }
	for (const FName Id : RequiredIds)
	{
		if (!GuideData->FindEntry(Id))
		{
			OutReason = FText::Format(INVTEXT("百科资产 {0} 缺少必需教学词条 {1}。"),
				FText::FromString(GuideData->GetPathName()), FText::FromName(Id));
			return false;
		}
	}
	auto* PopupClass = LoadRequiredWidgetClass(GuidePopupClassPath, OutReason);
	if (!PopupClass) { return false; }
	auto* BookClass = LoadRequiredWidgetClass(GuideBookClassPath, OutReason);
	if (!BookClass) { return false; }
	const auto* Popup = CreateWidget<UCastleGuidePopupWidget>(&Controller, PopupClass);
	const auto* Book = CreateWidget<UCastleGuideBookWidget>(&Controller, BookClass);
	if (!Popup || !Book || !Popup->HasRequiredBindings() || !Book->HasRequiredBindings())
	{
		OutReason = INVTEXT("教学百科弹窗或书册无法绑定必需控件。");
		return false;
	}
	TutorialController = &Controller;
	UnlockedEntryIds.Reset();
	return true;
}

void UCastleGuideSubsystem::BindModalController(ACastlePlayerController& Controller)
{
	// 只在当前百科窗口存活期间刷新，文本模板保留在控件中供重算。
	ActiveController = &Controller;
	ControlModeChangedHandle = Controller.OnControlModeChanged.AddUObject(this, &UCastleGuideSubsystem::RefreshModalText);
}

void UCastleGuideSubsystem::ReleaseModalController()
{
	if (auto* Controller = ActiveController.Get()) { Controller->OnControlModeChanged.Remove(ControlModeChangedHandle); }
	ControlModeChangedHandle.Reset();
	ActiveController.Reset();
}

void UCastleGuideSubsystem::RefreshModalText(ECastleControlMode)
{
	if (ActivePopup) { ActivePopup->RefreshDescription(); }
	if (ActiveBook) { ActiveBook->RefreshEntries(); }
}

void UCastleGuideSubsystem::CloseCurrentModal()
{
	if (ActivePopup) { CloseEntryPopup(); }
	else if (ActiveBook) { CloseGuideBook(); }
}

void UCastleGuideSubsystem::CancelForController(const ACastlePlayerController& Controller)
{
	if (TutorialController.Get() == &Controller) { TutorialController.Reset(); }
	if (ActiveController.Get() != &Controller) { return; }
	if (ActiveBook) { CloseGuideBook(); }
	if (ActivePopup)
	{
		// 关卡退出不是确认阅读，不能解锁词条或继续旧教程。
		ActivePopup->RemoveFromParent();
		ActivePopup = nullptr;
		PendingEntryId = NAME_None;
		ReleaseModalController();
	}
}

bool UCastleGuideSubsystem::EnsureGuideData(FText& OutReason)
{
	if (!GuideData) { GuideData = LoadObject<UCastleGuideDataAsset>(nullptr, GuideDataPath); }
	if (!GuideData)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("无法加载必需图文百科资产 %s。"), GuideDataPath));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	return GuideData->ValidateEntries(OutReason);
}

UClass* UCastleGuideSubsystem::LoadRequiredWidgetClass(const TCHAR* AssetPath, FText& OutReason) const
{
	UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, AssetPath);
	if (!WidgetClass)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("图文百科无法加载必需界面类 %s。"), AssetPath));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
	}
	return WidgetClass;
}

void UCastleGuideSubsystem::CloseEntryPopup()
{
	if (!ActivePopup) { return; }
	const FName LearnedEntryId = PendingEntryId;
	if (ACastlePlayerController* Controller = ActiveController.Get())
	{
		Controller->SetPresentationModalState(false, ActivePopup);
	}
	ActivePopup->RemoveFromParent();
	ActivePopup = nullptr;
	ReleaseModalController();
	PendingEntryId = NAME_None;
	UnlockedEntryIds.Add(LearnedEntryId);
	UE_LOG(LogMoving_Castle, Display, TEXT("图文百科已解锁条目 %s。"), *LearnedEntryId.ToString());
	OnGuideEntryLearned.Broadcast(LearnedEntryId);
}

void UCastleGuideSubsystem::CloseGuideBook()
{
	if (!ActiveBook) { return; }
	if (ACastlePlayerController* Controller = ActiveController.Get())
	{
		Controller->SetPresentationModalState(false, ActiveBook);
	}
	ActiveBook->RemoveFromParent();
	ActiveBook = nullptr;
	ReleaseModalController();
}

bool UCastleGuidePopupWidget::HasRequiredBindings() const
{
	const bool bValid = IsValid(Illustration) && IsValid(EntryTitle) && IsValid(EntryDescription) && IsValid(CloseButton);
	if (!bValid) { UE_LOG(LogMoving_Castle, Error, TEXT("百科弹窗 WBP %s 缺少插图、标题、说明或关闭按钮。"), *GetClass()->GetPathName()); }
	return bValid;
}

bool UCastleGuidePopupWidget::Configure(UCastleGuideSubsystem* InSubsystem, const FCastleGuideEntry& Entry)
{
	if (!HasRequiredBindings() || !IsValid(InSubsystem))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("百科弹窗 WBP %s 无法绑定子系统或条目 %s。"),
			*GetClass()->GetPathName(), *Entry.EntryId.ToString());
		return false;
	}
	GuideSubsystem = InSubsystem;
	EntryId = Entry.EntryId;
	SetGuideIllustration(*Illustration, Entry.Illustration);
	EntryTitle->SetText(Entry.Title);
	DescriptionTemplate = Entry.Description;
	RefreshDescription();
	return true;
}

void UCastleGuidePopupWidget::RefreshDescription()
{
	const auto* Controller = GetOwningPlayer<ACastlePlayerController>();
	check(Controller);
	EntryDescription->SetText(UCastleDialogueSubsystem::ResolveInputText(DescriptionTemplate, *Controller));
}

void UCastleGuidePopupWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 统一模态输入将键盘焦点交给当前百科界面。
	SetIsFocusable(true);
	if (HasRequiredBindings()) { CloseButton->OnClicked.AddDynamic(this, &UCastleGuidePopupWidget::HandleCloseClicked); }
}

void UCastleGuidePopupWidget::HandleCloseClicked()
{
	if (!IsValid(GuideSubsystem))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("百科条目 %s 关闭失败：图文百科子系统已失效。"), *EntryId.ToString());
		return;
	}
	GuideSubsystem->CloseCurrentModal();
}

bool UCastleGuideBookWidget::HasRequiredBindings() const
{
	const bool bValid = IsValid(Illustration) && IsValid(EntryTitle) && IsValid(EntryDescription)
		&& IsValid(PageStatus) && IsValid(EmptyMessage) && IsValid(PreviousButton) && IsValid(NextButton) && IsValid(CloseButton);
	if (!bValid) { UE_LOG(LogMoving_Castle, Error, TEXT("百科面板 WBP %s 缺少图文、页码、空状态或导航控件。"), *GetClass()->GetPathName()); }
	return bValid;
}

bool UCastleGuideBookWidget::Configure(UCastleGuideSubsystem* InSubsystem, const TArray<FCastleGuideEntry>& InEntries)
{
	if (!HasRequiredBindings() || !IsValid(InSubsystem))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("百科面板 WBP %s 无法绑定百科子系统。"), *GetClass()->GetPathName());
		return false;
	}
	GuideSubsystem = InSubsystem;
	Entries = InEntries;
	CurrentEntryIndex = Entries.IsEmpty() ? INDEX_NONE : 0;
	RefreshEntries();
	return true;
}

void UCastleGuideBookWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (!HasRequiredBindings()) { return; }
	PreviousButton->OnClicked.AddDynamic(this, &UCastleGuideBookWidget::HandlePreviousClicked);
	NextButton->OnClicked.AddDynamic(this, &UCastleGuideBookWidget::HandleNextClicked);
	CloseButton->OnClicked.AddDynamic(this, &UCastleGuideBookWidget::HandleCloseClicked);
	DisplayCurrentEntry();
}

void UCastleGuideBookWidget::HandlePreviousClicked()
{
	if (CurrentEntryIndex <= 0) { return; }
	--CurrentEntryIndex;
	DisplayCurrentEntry();
}

void UCastleGuideBookWidget::HandleNextClicked()
{
	if (!Entries.IsValidIndex(CurrentEntryIndex + 1)) { return; }
	++CurrentEntryIndex;
	DisplayCurrentEntry();
}

void UCastleGuideBookWidget::HandleCloseClicked()
{
	if (!IsValid(GuideSubsystem))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("图文百科面板 %s 关闭失败：百科子系统已失效。"), *GetPathName());
		return;
	}
	GuideSubsystem->CloseCurrentModal();
}

void UCastleGuideBookWidget::DisplayCurrentEntry()
{
	if (Entries.IsEmpty())
	{
		Illustration->SetVisibility(ESlateVisibility::Collapsed);
		EntryTitle->SetVisibility(ESlateVisibility::Collapsed);
		EntryDescription->SetVisibility(ESlateVisibility::Collapsed);
		PageStatus->SetText(FText::FromString(TEXT("0 / 0")));
		EmptyMessage->SetVisibility(ESlateVisibility::Visible);
		PreviousButton->SetIsEnabled(false);
		NextButton->SetIsEnabled(false);
		return;
	}
	if (!Entries.IsValidIndex(CurrentEntryIndex))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("百科面板 %s 的当前页索引 %d 超出已解锁条目数 %d。"),
			*GetPathName(), CurrentEntryIndex, Entries.Num());
		return;
	}
	const FCastleGuideEntry& Entry = Entries[CurrentEntryIndex];
	EntryTitle->SetVisibility(ESlateVisibility::Visible);
	EntryDescription->SetVisibility(ESlateVisibility::Visible);
	EmptyMessage->SetVisibility(ESlateVisibility::Collapsed);
	SetGuideIllustration(*Illustration, Entry.Illustration);
	EntryTitle->SetText(Entry.Title);
	const auto* Controller = GetOwningPlayer<ACastlePlayerController>();
	check(Controller);
	EntryDescription->SetText(UCastleDialogueSubsystem::ResolveInputText(Entry.Description, *Controller));
	PageStatus->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), CurrentEntryIndex + 1, Entries.Num())));
	PreviousButton->SetIsEnabled(CurrentEntryIndex > 0);
	NextButton->SetIsEnabled(CurrentEntryIndex + 1 < Entries.Num());
}

void UCastleGuideBookWidget::RefreshEntries()
{
	if (!HasRequiredBindings()) { return; }
	DisplayCurrentEntry();
}
