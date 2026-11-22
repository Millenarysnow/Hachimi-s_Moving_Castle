#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleBadgeBarWidget.generated.h"

class UCastleBadgeSubsystem;
class UPanelWidget;
class UTextBlock;

/** 徽章栏的业务绑定。六张文字卡片和外观由派生 WBP 提供。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleBadgeBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 初始化后再挂载，缺失任何必需控件或徽章源时返回失败。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|道具")
	bool BindBadges(UCastleBadgeSubsystem* InBadges);

protected:
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> BadgeSlots;

private:
	/** 以 WBP 中的视觉顺序对应子系统槽位。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SlotLabels;

	TWeakObjectPtr<UCastleBadgeSubsystem> Badges;

	UFUNCTION()
	void RefreshBadges();
};
