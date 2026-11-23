#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleGambleSkyIslandDataAsset.h"
#include "CastleGambleRouletteWidget.generated.h"

class ACastleGambleSkyIsland;
class UButton;
class UTextBlock;

/** WBP 提供布局，押色、退牌与开转均通过微缩棋盘的唯一交易入口。 */
UCLASS(Abstract)
class UCastleGambleRouletteWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	bool BindIsland(ACastleGambleSkyIsland* InIsland, FText& OutReason);
	/** 成功开奖后，等待指针停稳才展示结果并重新开放本轮操作。 */
	void FinishSpin(const FText& Result);

protected:
	/** 在按钮等子控件拥有键盘焦点时，仍可用 Esc 关闭面板。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void RefreshActions();
	void SelectBet(ECastleRouletteColor Color);

	UFUNCTION() void HandleReturn();
	UFUNCTION() void HandleReturnAll();
	UFUNCTION() void HandleBlack();
	UFUNCTION() void HandleRed();
	UFUNCTION() void HandleGreen();
	UFUNCTION() void HandleSpin();
	UFUNCTION() void HandleClose();

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ReturnAllButton;
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
	TOptional<ECastleRouletteColor> Bet;
};
