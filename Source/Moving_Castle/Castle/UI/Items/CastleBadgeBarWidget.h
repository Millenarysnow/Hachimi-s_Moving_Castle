#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleBadgeBarWidget.generated.h"

class UCastleBadgeSubsystem;
class UImage;
class UPanelWidget;
class UTextBlock;

/** 徽章栏的业务绑定。派生 WBP 提供按顺序排列的六个图文槽。 */
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
	/** 按 WBP 中的槽位顺序缓存图标和说明，索引与子系统库存一致。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SlotLabels;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> SlotIcons;

	TWeakObjectPtr<UCastleBadgeSubsystem> Badges;

	/** 先校验整栏的图标资源，再一次更新六个槽，避免只刷新半栏。 */
	bool UpdateBadgeSlots(const UCastleBadgeSubsystem& Source);

	UFUNCTION()
	void RefreshBadges();
};
