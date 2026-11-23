#pragma once

#include "Blueprint/UserWidget.h"
#include "Castle/Director/CastleDirectorTypes.h"
#include "CastleTowerUpgradeRewardWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;
class UCastleDirectorSubsystem;

/** 布局、标题、滚动容器和按钮样式均由 WBP 提供，导演拥有待领列表。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleTowerUpgradeRewardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	bool BindDirector(UCastleDirectorSubsystem& InDirector);
	void RefreshRewards(TConstArrayView<FCastleTowerUpgradeRewardEntry> Entries);

protected:
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UPanelWidget> RewardRows;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> ContinueButton;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> RewardButtonTemplate;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> RewardTextTemplate;

private:
	TWeakObjectPtr<UCastleDirectorSubsystem> Director;
	UFUNCTION() void Continue();
};
