#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleBadgeBarWidget.generated.h"

class UCastleBadgeSubsystem;
class UPanelWidget;

/** 徽章栏的业务绑定。派生 WBP 提供六个按库存顺序排列的叠层槽。 */
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
	TWeakObjectPtr<UCastleBadgeSubsystem> Badges;

	/** 校验整栏控件和库存资源后一次更新六个槽，避免只刷新半栏。 */
	bool UpdateBadgeSlots(const UCastleBadgeSubsystem& Source);
	void UnbindBadges();

	UFUNCTION()
	void RefreshBadges();
};
