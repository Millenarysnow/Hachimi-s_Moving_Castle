#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleBadgeReplaceModalWidget.generated.h"

class UButton;
class UCastleBadgeSubsystem;
class UImage;
class UPanelWidget;
class UTextBlock;

/** 替换弹窗只提交用户点选的动作。待替换奖励和已持有状态均由子系统维护。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleBadgeReplaceModalWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 绑定成功后可以挂载；没有待替换奖励时折叠，有待替换奖励时显示。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|道具")
	bool BindBadges(UCastleBadgeSubsystem* InBadges);

protected:
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PendingBadgeLabel;

	/** 候选徽章图标与名称说明由同一笔待替换奖励驱动。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> PendingBadgeIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> ReplaceSlots;

	/** 与按钮层共用六行布局。图标层不参与命中测试，保留原按钮热区和焦点。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> ReplaceIcons;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> DiscardButton;

private:
	/** 图标层与按钮层按索引一一对应，用同一份库存快照刷新。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> SlotButtons;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> SlotIcons;

	TWeakObjectPtr<UCastleBadgeSubsystem> Badges;

	/** 校验候选和库存图标，再一起更新弹窗内容。 */
	bool UpdateBadgeSlots(const UCastleBadgeSubsystem& Source);

	UFUNCTION()
	void RefreshBadges();

	void Replace(int32 SlotIndex);

	// UButton 的 OnClicked 不传参与触发按钮的信息，六个固定 WBP 槽位显式转发自己的索引。
	UFUNCTION()
	void ReplaceFirst() { Replace(0); }

	UFUNCTION()
	void ReplaceSecond() { Replace(1); }

	UFUNCTION()
	void ReplaceThird() { Replace(2); }

	UFUNCTION()
	void ReplaceFourth() { Replace(3); }

	UFUNCTION()
	void ReplaceFifth() { Replace(4); }

	UFUNCTION()
	void ReplaceSixth() { Replace(5); }

	UFUNCTION()
	void Discard();
};
