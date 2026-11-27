#include "CastleDialogueWidget.h"
#include "Audio/CastleAudioSubsystem.h"

#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Dialogue/CastleDialogueSubsystem.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Input/Reply.h"
#include "Moving_Castle.h"
#include "Blueprint/WidgetTree.h"

void UCastleDialogueChoiceButton::BindChoice(UCastleDialogueWidget& InDialogue, const FName InTarget)
{
	Dialogue = &InDialogue;
	Target = InTarget;
	OnClicked.AddUniqueDynamic(this, &UCastleDialogueChoiceButton::HandleClicked);
}

void UCastleDialogueChoiceButton::HandleClicked()
{
	if (UCastleDialogueWidget* Widget = Dialogue.Get()) { Widget->Choose(Target); }
}

bool UCastleDialogueWidget::HasRequiredBindings() const
{
	const bool bValid = IsValid(LeftPortrait) && IsValid(RightPortrait) && IsValid(SpeakerName)
		&& IsValid(DialogueBody) && IsValid(ChoiceContainer) && IsValid(ContinueButton) && IsValid(ChoiceButtonTemplate);
	if (!bValid)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("对话 WBP %s 缺少立绘、正文、分支容器、分支样式模板或继续按钮。"),
			*GetClass()->GetPathName());
	}
	return bValid;
}

bool UCastleDialogueWidget::ConfigureDialogue(UCastleDialogueSubsystem* InSubsystem,
	ACastlePlayerController* InController, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!HasRequiredBindings() || !IsValid(InSubsystem) || !IsValid(InController))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("对话 WBP %s 绑定或对话子系统/玩家控制器无效。"), *GetClass()->GetPathName()));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	DialogueSubsystem = InSubsystem;
	Controller = InController;
	ControlModeChangedHandle = InController->OnControlModeChanged.AddUObject(this, &UCastleDialogueWidget::HandleControlModeChanged);
	SetIsFocusable(true);
	return true;
}

void UCastleDialogueWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (!HasRequiredBindings()) { return; }
	ContinueButton->OnClicked.AddDynamic(this, &UCastleDialogueWidget::HandleContinueClicked);
	ChoiceContainer->SetVisibility(ESlateVisibility::Collapsed);
}

void UCastleDialogueWidget::DisplayLine(const FCastleDialogueLine& Line, const FText& ResolvedText,
	const float InCharactersPerSecond)
{
	if (!HasRequiredBindings()) { return; }
	CurrentLine = Line;
	CharactersPerSecond = InCharactersPerSecond;
	FullText = ResolvedText.ToString();
	RevealedCharacterCount = 0;
	RevealRemainder = 0.0f;
	SpeakerName->SetText(Line.SpeakerName);
	UpdateFocusTint();
	RebuildChoiceButtons();
	UpdatePresentedText();
}

void UCastleDialogueWidget::RefreshResolvedText(const FText& ResolvedText)
{
	const bool bWasFullyRevealed = IsCurrentLineFullyRevealed();
	FullText = ResolvedText.ToString();
	RevealedCharacterCount = bWasFullyRevealed ? FullText.Len() : FMath::Min(RevealedCharacterCount, FullText.Len());
	UpdatePresentedText();
}

void UCastleDialogueWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (IsCurrentLineFullyRevealed()) { return; }
	RevealRemainder += InDeltaTime * CharactersPerSecond;
	const int32 WholeCharacters = FMath::FloorToInt(RevealRemainder);
	if (WholeCharacters <= 0) { return; }
	RevealRemainder -= static_cast<float>(WholeCharacters);
	RevealedCharacterCount = FMath::Min(FullText.Len(), RevealedCharacterCount + WholeCharacters);
	UpdatePresentedText();
}

void UCastleDialogueWidget::CompleteCurrentLine()
{
	RevealedCharacterCount = FullText.Len();
	RevealRemainder = 0.0f;
	UpdatePresentedText();
}

FReply UCastleDialogueWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar)
	{
		// 长按确认键产生的重复事件不计为第二次输入。
		if (!InKeyEvent.IsRepeat()) { HandleContinueClicked(); }
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UCastleDialogueWidget::NativeDestruct()
{
	if (ACastlePlayerController* PlayerController = Controller.Get())
	{
		PlayerController->OnControlModeChanged.Remove(ControlModeChangedHandle);
	}
	ControlModeChangedHandle.Reset();
	Controller.Reset();
	DialogueSubsystem = nullptr;
	Super::NativeDestruct();
}

void UCastleDialogueWidget::HandleContinueClicked()
{
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Button);
	if (!IsValid(DialogueSubsystem))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("对话 WBP %s 点击推进失败：对话子系统已失效。"), *GetPathName());
		return;
	}
	if (!IsCurrentLineFullyRevealed())
	{
		CompleteCurrentLine();
		return;
	}
	if (!CurrentLine.Choices.IsEmpty()) { return; }
	DialogueSubsystem->AdvanceCurrentLine();
}

void UCastleDialogueWidget::HandleControlModeChanged(const ECastleControlMode)
{
	if (IsValid(DialogueSubsystem)) { RefreshResolvedText(DialogueSubsystem->GetResolvedCurrentLineText()); }
}

void UCastleDialogueWidget::Choose(const FName TargetLineId)
{
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Button);
	if (IsValid(DialogueSubsystem)) { DialogueSubsystem->SelectChoice(TargetLineId); }
}

void UCastleDialogueWidget::RebuildChoiceButtons()
{
	ChoiceContainer->ClearChildren();
	ChoiceContainer->SetVisibility(ESlateVisibility::Collapsed);
	if (CurrentLine.Choices.IsEmpty())
	{
		ChoiceContainer->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	for (const FCastleDialogueChoice& Choice : CurrentLine.Choices)
	{
		auto* ChoiceButton = WidgetTree->ConstructWidget<UCastleDialogueChoiceButton>();
		UTextBlock* ChoiceLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		if (!IsValid(ChoiceButton) || !IsValid(ChoiceLabel))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("对话 WBP %s 无法创建行 %s 的分支按钮。"),
				*GetPathName(), *CurrentLine.LineId.ToString());
			return;
		}
		ChoiceLabel->SetText(Choice.Text);
		ChoiceLabel->SetFont(DialogueBody->GetFont());
		ChoiceLabel->SetAutoWrapText(true);
		ChoiceLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.89f)));
		// 分支按钮直接继承资产中的三态切图，立绘结构和点击层保持原布局。
		ChoiceButton->SetStyle(ChoiceButtonTemplate->GetStyle());
		auto* LabelSlot = CastChecked<UButtonSlot>(ChoiceButton->AddChild(ChoiceLabel));
		LabelSlot->SetPadding(FMargin(18, 9));
		LabelSlot->SetHorizontalAlignment(HAlign_Fill);
		ChoiceButton->BindChoice(*this, Choice.TargetLineId);
		ChoiceContainer->AddChildToVerticalBox(ChoiceButton)->SetPadding(FMargin(0, 0, 0, 8));
	}
}

void UCastleDialogueWidget::UpdatePresentedText()
{
	if (!IsValid(DialogueBody)) { return; }
	DialogueBody->SetText(FText::FromString(FullText.Left(RevealedCharacterCount)));
	ChoiceContainer->SetVisibility(IsCurrentLineFullyRevealed() && !CurrentLine.Choices.IsEmpty()
		? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UCastleDialogueWidget::UpdateFocusTint()
{
	const auto ApplyPortrait = [](UImage* Image, UTexture2D* Texture, const ECastleDialogueFocusSide Focus,
		const ECastleDialogueFocusSide Side)
	{
		if (!Texture)
		{
			Image->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
		// 缩放框提供固定基准，原始贴图像素不参与布局，立绘也不拦截鼠标。
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		Image->SetBrushFromTexture(Texture, false);
		const float Brightness = Focus == Side ? 1.0f : 0.45f;
		Image->SetColorAndOpacity(FLinearColor(Brightness, Brightness, Brightness, 1.0f));
	};
	ApplyPortrait(LeftPortrait, CurrentLine.LeftPortrait, CurrentLine.FocusSide, ECastleDialogueFocusSide::Left);
	ApplyPortrait(RightPortrait, CurrentLine.RightPortrait, CurrentLine.FocusSide, ECastleDialogueFocusSide::Right);
}
