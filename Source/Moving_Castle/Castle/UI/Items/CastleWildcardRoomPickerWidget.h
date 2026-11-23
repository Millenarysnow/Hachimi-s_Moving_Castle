#pragma once

#include "Blueprint/UserWidget.h"
#include "Castle/Items/CastleItemTypes.h"
#include "Components/Button.h"
#include "CastleWildcardRoomPickerWidget.generated.h"

class UCastlePendingRoomReward;
class UBorder;
class UTextBlock;
class UWrapBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCastleWildcardPickerClosed, bool, bClaimed);

/** 选项行复用 WBP 按钮的样式，单个选项转发自己的索引。 */
UCLASS()
class UCastleWildcardChoiceButton : public UButton
{
	GENERATED_BODY()

public:
	void BindChoice(FSimpleDelegate InAction);

private:
	FSimpleDelegate Action;

	UFUNCTION()
	void Choose();
};

/** WBP 提供弹窗布局、选择容器与样式。C++ 只管理候选值和领取生命周期。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleWildcardRoomPickerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 挂载前绑定待领取项。初始只显示领取按钮，属性弹窗由玩家点击后打开。 */
	bool BindReward(UCastlePendingRoomReward* InReward, FText& OutFailureReason);
	/** 外层领奖按钮已经取得用户意图，直接进入属性选择。 */
	bool BeginBoundReward(FText& OutFailureReason);

	UPROPERTY(BlueprintAssignable, Category = "移动城堡|道具")
	FCastleWildcardPickerClosed OnPickerClosed;

protected:
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ClaimButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ClaimLabel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> PickerPanel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> RewardTitle;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> DefinitionChoices;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> ColorChoices;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> NumberChoices;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ConfirmButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CancelButton;

	/** 隐藏的 WBP 模板保留可替换的字体和按钮样式。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> OptionStyleTemplate;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> OptionTextTemplate;

private:
	UPROPERTY(Transient)
	TObjectPtr<UCastlePendingRoomReward> Reward;

	FCastleWildcardRoomOptions Options;
	int32 DefinitionIndex = INDEX_NONE;
	int32 ColorIndex = INDEX_NONE;
	int32 NumberIndex = INDEX_NONE;
	bool bChoosing = false;

	void AppendChoice(UWrapBox& Panel, const FText& Label, FSimpleDelegate Action);
	void UpdateSelection();
	void Finish(bool bClaimed);

	UFUNCTION()
	void OpenReward();

	UFUNCTION()
	void Confirm();

	UFUNCTION()
	void Cancel();
};
