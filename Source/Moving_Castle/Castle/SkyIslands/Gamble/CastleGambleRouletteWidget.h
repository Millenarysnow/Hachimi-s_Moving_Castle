#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleGambleSkyIslandDataAsset.h"
#include "Components/ComboBoxString.h"
#include "CastleGambleRouletteWidget.generated.h"

class ACastleGambleSkyIsland;
class UButton;
class UTextBlock;

/** 占位 WBP 提供静态布局，C++ 负责把每个选项映射到实际背包槽位和微缩格。 */
UCLASS(Abstract)
class UCastleGambleRouletteWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	bool BindIsland(ACastleGambleSkyIsland* InIsland, FText& OutReason);
	/** 成功开奖后，等待指针停稳才展示结果并重新开放本轮操作。 */
	void FinishSpin(const FText& Result);

private:
	void RefreshBackpack();
	void RefreshCells();
	void RefreshActions();
	void SelectBet(ECastleRouletteColor Color);

	UFUNCTION() void HandleCardSelected(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleCellSelected(FString Item, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandlePlace();
	UFUNCTION() void HandleReturn();
	UFUNCTION() void HandleBlack();
	UFUNCTION() void HandleRed();
	UFUNCTION() void HandleGreen();
	UFUNCTION() void HandleSpin();
	UFUNCTION() void HandleClose();

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> CardOptions;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> CellOptions;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> PlaceButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ReturnButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BlackButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> RedButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> GreenButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SpinButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CloseButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> BlackLabel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> RedLabel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> GreenLabel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> BetLabel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> CountLabel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ResultLabel;
	UPROPERTY(Transient) TWeakObjectPtr<ACastleGambleSkyIsland> Island;
	TArray<FIntVector> LegalCells;
	TOptional<ECastleRouletteColor> Bet;
	bool bRefreshingOptions = false;
};
