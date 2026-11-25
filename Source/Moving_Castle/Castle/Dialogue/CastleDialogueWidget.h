#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Castle/Input/CastleControlMode.h"
#include "Castle/Dialogue/CastleDialogueScriptDataAsset.h"
#include "CastleDialogueWidget.generated.h"

class ACastlePlayerController;
class UCastleDialogueWidget;
class UCastleDialogueSubsystem;
class UImage;
class UTextBlock;
class UVerticalBox;

/** 每个动态按钮持有自己的目标行，UMG 动态委托只负责转发一次点击。 */
UCLASS()
class MOVING_CASTLE_API UCastleDialogueChoiceButton : public UButton
{
	GENERATED_BODY()
public:
	void BindChoice(UCastleDialogueWidget& InDialogue, FName InTarget);
private:
	UFUNCTION() void HandleClicked();
	TWeakObjectPtr<UCastleDialogueWidget> Dialogue;
	FName Target;
};

/** 对话视觉容器；布局由 WBP 提供，打字机、分支和键盘输入全部由 C++ 驱动。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleDialogueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 校验 WBP 需要的左右立绘、姓名、正文、选项和整句推进点击区。 */
	bool HasRequiredBindings() const;
	bool ConfigureDialogue(UCastleDialogueSubsystem* InSubsystem, ACastlePlayerController* InController, FText& OutReason);
	void DisplayLine(const FCastleDialogueLine& Line, const FText& ResolvedText, float CharactersPerSecond);
	void RefreshResolvedText(const FText& ResolvedText);
	void CompleteCurrentLine();
	void Choose(FName TargetLineId);
	bool IsCurrentLineFullyRevealed() const { return RevealedCharacterCount >= FullText.Len(); }
	int32 GetRevealedCharacterCount() const { return RevealedCharacterCount; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> LeftPortrait;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> RightPortrait;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SpeakerName;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> DialogueBody;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UVerticalBox> ChoiceContainer;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ContinueButton;
	/** 动态分支按钮使用同一套常态、悬停与按下切图。 */
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ChoiceButtonTemplate;

private:
	UFUNCTION() void HandleContinueClicked();
	void HandleControlModeChanged(ECastleControlMode NewMode);
	void RebuildChoiceButtons();
	void UpdatePresentedText();
	void UpdateFocusTint();

	UPROPERTY(Transient) TObjectPtr<UCastleDialogueSubsystem> DialogueSubsystem;
	TWeakObjectPtr<ACastlePlayerController> Controller;
	UPROPERTY(Transient) FCastleDialogueLine CurrentLine;
	FString FullText;
	float CharactersPerSecond = 35.0f;
	float RevealRemainder = 0.0f;
	int32 RevealedCharacterCount = 0;
	FDelegateHandle ControlModeChangedHandle;
};
