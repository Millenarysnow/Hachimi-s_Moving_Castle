#pragma once

#include "Blueprint/UserWidget.h"
#include "Castle/Director/CastleDirectorTypes.h"
#include "CastleTowerUpgradeRewardWidget.generated.h"

class UButton;
class UImage;
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
	/** 奖励行从 WBP 复制图标画刷，运行时只按实际奖励类型组装。 */
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UImage> PathRewardIcon;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UImage> NPCRewardIcon;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UImage> RoomRewardIcon;

private:
	TWeakObjectPtr<UCastleDirectorSubsystem> Director;
	UFUNCTION() void Continue();
};
