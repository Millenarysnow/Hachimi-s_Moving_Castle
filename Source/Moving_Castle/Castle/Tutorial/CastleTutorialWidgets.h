#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleGameplayTypes.h"
#include "CastleTutorialWidgets.generated.h"

class ACastleBoard;
class ACastleTutorialDirector;
class UButton;
class UPanelWidget;
class UTextBlock;

/** 目标条、世界提示及城主台词气泡共用单个绑定文本。 */
UCLASS(Abstract)
class UCastleTutorialTextWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	bool SetTutorialText(const FText& Text);
private:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> TutorialText;
};

/** 教程奖励表示层，奖励入包只由教学导演提交。 */
UCLASS(Abstract)
class UCastleTutorialRewardWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	bool Configure(ACastleTutorialDirector& Director);
	bool ShowCard(const FCastleBuildingDescriptor& Descriptor, ACastleBoard& Board);
protected:
	virtual void NativeOnInitialized() override;
private:
	UFUNCTION() void ChooseCopy();
	UFUNCTION() void ClaimCard();
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> Choices;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ExperienceButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> HealingButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CopyButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CardButton;
	TWeakObjectPtr<ACastleTutorialDirector> OwnerDirector;
};
