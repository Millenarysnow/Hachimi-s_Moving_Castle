#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleBadgeReplaceModalWidget.generated.h"

class UButton;
class UCastleBadgeSubsystem;
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

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> ReplaceSlots;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> DiscardButton;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SlotLabels;

	TWeakObjectPtr<UCastleBadgeSubsystem> Badges;

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
