#include "CastleGambleRouletteWidget.h"
#include "Audio/CastleAudioSubsystem.h"

#include "CastleGambleSkyIsland.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Input/Events.h"

bool UCastleGambleRouletteWidget::BindIsland(ACastleGambleSkyIsland* InIsland, FText& OutReason)
{
	if (!IsValid(InIsland) || !ReturnAllButton || !ReturnButton || !BlackButton
		|| !RedButton || !GreenButton || !SpinButton || !CloseButton || !BlackLabel || !RedLabel || !GreenLabel
		|| !BetLabel || !CountLabel || !ResultLabel)
	{
		OutReason = INVTEXT("轮盘面板缺少必需的 BindWidget 控件或空岛实例。");
		return false;
	}
	Island = InIsland;
	SetIsFocusable(true);
	ReturnAllButton->OnClicked.AddUniqueDynamic(this, &UCastleGambleRouletteWidget::HandleReturnAll);
	ReturnButton->OnClicked.AddUniqueDynamic(this, &UCastleGambleRouletteWidget::HandleReturn);
	BlackButton->OnClicked.AddUniqueDynamic(this, &UCastleGambleRouletteWidget::HandleBlack);
	RedButton->OnClicked.AddUniqueDynamic(this, &UCastleGambleRouletteWidget::HandleRed);
	GreenButton->OnClicked.AddUniqueDynamic(this, &UCastleGambleRouletteWidget::HandleGreen);
	SpinButton->OnClicked.AddUniqueDynamic(this, &UCastleGambleRouletteWidget::HandleSpin);
	CloseButton->OnClicked.AddUniqueDynamic(this, &UCastleGambleRouletteWidget::HandleClose);
	const UCastleGambleSkyIslandDataAsset& Config = *InIsland->GetGambleConfig();
	BlackLabel->SetText(FText::Format(INVTEXT("黑 ×{0}"), FText::AsNumber(Config.RouletteRedBlackMultiplier)));
	RedLabel->SetText(FText::Format(INVTEXT("红 ×{0}"), FText::AsNumber(Config.RouletteRedBlackMultiplier)));
	GreenLabel->SetText(FText::Format(INVTEXT("绿 ×{0}"), FText::AsNumber(Config.RouletteGreenMultiplier)));
	BetLabel->SetText(INVTEXT("请选择押注颜色。"));
	ResultLabel->SetText(InIsland->IsRouletteSpinning() ? INVTEXT("轮盘转动中…") : InIsland->GetRouletteResult());
	RefreshActions();
	return true;
}

void UCastleGambleRouletteWidget::RefreshActions()
{
	const bool bReady = !Island->IsRouletteSpinning();
	const int32 Count = Island->GetMiniBoard().GetPlacements().Num();
	CountLabel->SetText(FText::Format(INVTEXT("已下注 {0} 张房间卡"), FText::AsNumber(Count)));
	ReturnAllButton->SetIsEnabled(bReady && Count > 0);
	ReturnButton->SetIsEnabled(bReady && Count > 0);
	BlackButton->SetIsEnabled(bReady);
	RedButton->SetIsEnabled(bReady);
	GreenButton->SetIsEnabled(bReady);
	SpinButton->SetIsEnabled(bReady && Bet.IsSet());
	CloseButton->SetIsEnabled(bReady);
}

void UCastleGambleRouletteWidget::SelectBet(const ECastleRouletteColor Color)
{
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Button);
	Bet = Color;
	const FText Labels[] = {BlackLabel->GetText(), RedLabel->GetText(), GreenLabel->GetText()};
	BetLabel->SetText(FText::Format(INVTEXT("已押 {0}"), Labels[static_cast<uint8>(Color)]));
	RefreshActions();
}

void UCastleGambleRouletteWidget::HandleReturnAll()
{
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Button);
	if (!Island.IsValid()) { return; }
	FText Reason;
	const bool bReturned = Island->TryReturnAllCards(Reason);
	if (bReturned) { RefreshActions(); }
	ResultLabel->SetText(bReturned ? INVTEXT("未开转的房间卡已全部退回。") : Reason);
}

void UCastleGambleRouletteWidget::HandleReturn()
{
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Button);
	if (!Island.IsValid()) { return; }
	FText Reason;
	const bool bReturned = Island->TryReturnLastCard(Reason);
	if (bReturned) { RefreshActions(); }
	ResultLabel->SetText(bReturned ? INVTEXT("最后一张房间卡已撤回。") : Reason);
}

void UCastleGambleRouletteWidget::HandleBlack() { SelectBet(ECastleRouletteColor::Black); }
void UCastleGambleRouletteWidget::HandleRed() { SelectBet(ECastleRouletteColor::Red); }
void UCastleGambleRouletteWidget::HandleGreen() { SelectBet(ECastleRouletteColor::Green); }

void UCastleGambleRouletteWidget::HandleSpin()
{
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Button);
	if (!Island.IsValid() || !Bet.IsSet()) { return; }
	FText Reason;
	const bool bStarted = Island->TrySpinRoulette(Bet.GetValue(), Reason);
	if (bStarted) { RefreshActions(); }
	ResultLabel->SetText(bStarted ? INVTEXT("轮盘转动中…") : Reason);
}

void UCastleGambleRouletteWidget::FinishSpin(const FText& Result)
{
	RefreshActions();
	ResultLabel->SetText(Result);
}

void UCastleGambleRouletteWidget::HandleClose()
{
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Button);
	FText Reason;
	if (Island.IsValid() && !Island->TryCloseRoulettePanel(Reason)) { ResultLabel->SetText(Reason); }
}

FReply UCastleGambleRouletteWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		// 开转期间也消费 Esc，保留轮盘模态直到结算动画结束。
		HandleClose();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}
