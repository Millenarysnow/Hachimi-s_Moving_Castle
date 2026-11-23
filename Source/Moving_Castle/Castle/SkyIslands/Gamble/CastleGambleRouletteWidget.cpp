#include "CastleGambleRouletteWidget.h"

#include "CastleBackpackComponent.h"
#include "CastleBuildingDefinition.h"
#include "CastleGambleSkyIsland.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"

bool UCastleGambleRouletteWidget::BindIsland(ACastleGambleSkyIsland* InIsland, FText& OutReason)
{
	if (!IsValid(InIsland) || !CardOptions || !CellOptions || !PlaceButton || !ReturnButton || !BlackButton
		|| !RedButton || !GreenButton || !SpinButton || !CloseButton || !BlackLabel || !RedLabel || !GreenLabel
		|| !BetLabel || !CountLabel || !ResultLabel)
	{
		OutReason = INVTEXT("轮盘面板缺少必需的 BindWidget 控件或空岛实例。");
		return false;
	}
	Island = InIsland;
	CardOptions->OnSelectionChanged.AddUniqueDynamic(this, &UCastleGambleRouletteWidget::HandleCardSelected);
	CellOptions->OnSelectionChanged.AddUniqueDynamic(this, &UCastleGambleRouletteWidget::HandleCellSelected);
	PlaceButton->OnClicked.AddUniqueDynamic(this, &UCastleGambleRouletteWidget::HandlePlace);
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
	RefreshBackpack();
	return true;
}

void UCastleGambleRouletteWidget::RefreshBackpack()
{
	// 背包消费时主栏可能重排并补位，旧索引无法代表旧牌，要求玩家重新选择实际槽位。
	TGuardValue<bool> Refresh(bRefreshingOptions, true);
	CardOptions->ClearOptions();
	const TArray<FCastleBuildingDescriptor> Cards = Island->GetBackpack()->GetItems();
	for (int32 Index = 0; Index < Cards.Num(); ++Index)
	{
		const FCastleBuildingDescriptor& Card = Cards[Index];
		const FText Color = StaticEnum<ECastleTileColor>()->GetDisplayNameTextByValue(static_cast<int64>(Card.Color));
		const FText Label = FText::Format(INVTEXT("{0}. {1} · {2} {3}"), FText::AsNumber(Index + 1),
			Card.Definition->DisplayName, Color, FText::AsNumber(Card.Number));
		CardOptions->AddOption(Label.ToString());
	}
	RefreshCells();
}

void UCastleGambleRouletteWidget::RefreshCells()
{
	TGuardValue<bool> Refresh(bRefreshingOptions, true);
	CellOptions->ClearOptions();
	LegalCells.Reset();
	FCastleBuildingDescriptor Card;
	if (Island->GetBackpack()->TryGetItemAt(CardOptions->GetSelectedIndex(), Card))
	{
		LegalCells = Island->GetMiniBoard().GetLegalCells(Card);
		for (const FIntVector& Cell : LegalCells)
		{
			CellOptions->AddOption(FString::Printf(TEXT("(%d, %d, %d)"), Cell.X, Cell.Y, Cell.Z));
		}
		if (LegalCells.IsEmpty()) { ResultLabel->SetText(INVTEXT("该房间没有同色或同数字的可用相邻格。")); }
	}
	RefreshActions();
}

void UCastleGambleRouletteWidget::RefreshActions()
{
	const bool bReady = !Island->IsRouletteSpinning();
	const int32 Count = Island->GetMiniBoard().GetPlacements().Num();
	CountLabel->SetText(FText::Format(INVTEXT("微缩区 {0} 张 · 以底板原点为 (0, 0, 0)"), FText::AsNumber(Count)));
	CardOptions->SetIsEnabled(bReady);
	CellOptions->SetIsEnabled(bReady && !LegalCells.IsEmpty());
	PlaceButton->SetIsEnabled(bReady && LegalCells.IsValidIndex(CellOptions->GetSelectedIndex()));
	ReturnButton->SetIsEnabled(bReady && Count > 0);
	BlackButton->SetIsEnabled(bReady);
	RedButton->SetIsEnabled(bReady);
	GreenButton->SetIsEnabled(bReady);
	SpinButton->SetIsEnabled(bReady && Count > 0 && Bet.IsSet());
}

void UCastleGambleRouletteWidget::SelectBet(const ECastleRouletteColor Color)
{
	Bet = Color;
	const FText Labels[] = {BlackLabel->GetText(), RedLabel->GetText(), GreenLabel->GetText()};
	BetLabel->SetText(FText::Format(INVTEXT("已押 {0}"), Labels[static_cast<uint8>(Color)]));
	RefreshActions();
}

void UCastleGambleRouletteWidget::HandleCardSelected(FString Item, ESelectInfo::Type SelectionType)
{
	if (!bRefreshingOptions && Island.IsValid()) { RefreshCells(); }
}

void UCastleGambleRouletteWidget::HandleCellSelected(FString Item, ESelectInfo::Type SelectionType)
{
	if (!bRefreshingOptions && Island.IsValid()) { RefreshActions(); }
}

void UCastleGambleRouletteWidget::HandlePlace()
{
	const int32 CellIndex = CellOptions->GetSelectedIndex();
	if (!Island.IsValid() || !LegalCells.IsValidIndex(CellIndex)) { return; }
	FText Reason;
	const bool bPlaced = Island->TryPlaceCard(CardOptions->GetSelectedIndex(), LegalCells[CellIndex], Reason);
	if (bPlaced) { RefreshBackpack(); }
	ResultLabel->SetText(bPlaced ? INVTEXT("房间已放入微缩区。") : Reason);
}

void UCastleGambleRouletteWidget::HandleReturn()
{
	if (!Island.IsValid()) { return; }
	FText Reason;
	const bool bReturned = Island->TryReturnLastCard(Reason);
	if (bReturned) { RefreshBackpack(); }
	ResultLabel->SetText(bReturned ? INVTEXT("最后一张房间卡已撤回。") : Reason);
}

void UCastleGambleRouletteWidget::HandleBlack() { SelectBet(ECastleRouletteColor::Black); }
void UCastleGambleRouletteWidget::HandleRed() { SelectBet(ECastleRouletteColor::Red); }
void UCastleGambleRouletteWidget::HandleGreen() { SelectBet(ECastleRouletteColor::Green); }

void UCastleGambleRouletteWidget::HandleSpin()
{
	if (!Island.IsValid() || !Bet.IsSet()) { return; }
	FText Reason;
	const bool bStarted = Island->TrySpinRoulette(Bet.GetValue(), Reason);
	if (bStarted) { RefreshBackpack(); }
	ResultLabel->SetText(bStarted ? INVTEXT("轮盘转动中…") : Reason);
}

void UCastleGambleRouletteWidget::FinishSpin(const FText& Result)
{
	RefreshBackpack();
	ResultLabel->SetText(Result);
}

void UCastleGambleRouletteWidget::HandleClose()
{
	FText Reason;
	if (Island.IsValid() && !Island->TryCloseRoulettePanel(Reason)) { ResultLabel->SetText(Reason); }
}
