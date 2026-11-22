#include "Items/CastleWildcardRoomPickerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Castle/Items/CastlePendingRoomReward.h"
#include "CastleBuildingDefinition.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/WrapBox.h"
#include "Moving_Castle.h"

void UCastleWildcardChoiceButton::BindChoice(FSimpleDelegate InAction)
{
	Action = MoveTemp(InAction);
	OnClicked.AddUniqueDynamic(this, &UCastleWildcardChoiceButton::Choose);
}

void UCastleWildcardChoiceButton::Choose()
{
	Action.ExecuteIfBound();
}

bool UCastleWildcardRoomPickerWidget::BindReward(UCastlePendingRoomReward* InReward, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (!IsValid(InReward) || InReward->IsClaimed() || Reward || !ClaimButton || !ClaimLabel || !PickerPanel
		|| !RewardTitle || !StatusText || !DefinitionChoices || !ColorChoices || !NumberChoices
		|| !ConfirmButton || !CancelButton || !OptionStyleTemplate || !OptionTextTemplate || !WidgetTree)
	{
		OutFailureReason = INVTEXT("万能卡界面缺少必需绑定，或待领取奖励无效。");
		UE_LOG(LogMoving_Castle, Error, TEXT("万能卡界面 %s 无法挂载：%s"), *GetPathName(), *OutFailureReason.ToString());
		return false;
	}
	Reward = InReward;
	ClaimLabel->SetText(FText::Format(INVTEXT("{0}　获取"), Reward->GetDisplayName()));
	PickerPanel->SetVisibility(ESlateVisibility::Collapsed);
	OptionStyleTemplate->SetVisibility(ESlateVisibility::Collapsed);
	ClaimButton->OnClicked.AddUniqueDynamic(this, &UCastleWildcardRoomPickerWidget::OpenReward);
	ConfirmButton->OnClicked.AddUniqueDynamic(this, &UCastleWildcardRoomPickerWidget::Confirm);
	CancelButton->OnClicked.AddUniqueDynamic(this, &UCastleWildcardRoomPickerWidget::Cancel);
	ClaimButton->SetVisibility(ESlateVisibility::Visible);
	SetVisibility(ESlateVisibility::Visible);
	return true;
}

void UCastleWildcardRoomPickerWidget::OpenReward()
{
	if (!Reward || bChoosing)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("万能卡界面 %s 没有可打开的待领取项。"), *GetPathName());
		return;
	}
	FText Failure;
	if (!Reward->TryBeginClaim(Options, Failure))
	{
		return;
	}

	bChoosing = true;
	RewardTitle->SetText(Reward->GetDisplayName());
	DefinitionChoices->ClearChildren();
	ColorChoices->ClearChildren();
	NumberChoices->ClearChildren();
	DefinitionIndex = Options.Definitions.Num() == 1 ? 0 : INDEX_NONE;
	ColorIndex = Options.Colors.Num() == 1 ? 0 : INDEX_NONE;
	NumberIndex = Options.Numbers.Num() == 1 ? 0 : INDEX_NONE;

	for (int32 Index = 0; Index < Options.Definitions.Num(); ++Index)
	{
		AppendChoice(*DefinitionChoices, Options.Definitions[Index]->DisplayName, FSimpleDelegate::CreateWeakLambda(this, [this, Index]
		{
			DefinitionIndex = Index;
			UpdateSelection();
		}));
	}
	for (int32 Index = 0; Index < Options.Colors.Num(); ++Index)
	{
		const ECastleTileColor Color = Options.Colors[Index];
		FText Label;
		switch (Color)
		{
		case ECastleTileColor::Red: Label = INVTEXT("红色"); break;
		case ECastleTileColor::Blue: Label = INVTEXT("蓝色"); break;
		case ECastleTileColor::Green: Label = INVTEXT("绿色"); break;
		case ECastleTileColor::Yellow: Label = INVTEXT("黄色"); break;
		case ECastleTileColor::Purple: Label = INVTEXT("紫色"); break;
		default: checkNoEntry(); break;
		}
		AppendChoice(*ColorChoices, Label, FSimpleDelegate::CreateWeakLambda(this, [this, Index]
		{
			ColorIndex = Index;
			UpdateSelection();
		}));
	}
	for (int32 Index = 0; Index < Options.Numbers.Num(); ++Index)
	{
		AppendChoice(*NumberChoices, FText::AsNumber(Options.Numbers[Index]), FSimpleDelegate::CreateWeakLambda(this, [this, Index]
		{
			NumberIndex = Index;
			UpdateSelection();
		}));
	}

	UpdateSelection();
	ClaimButton->SetVisibility(ESlateVisibility::Collapsed);
	PickerPanel->SetVisibility(ESlateVisibility::Visible);
}

void UCastleWildcardRoomPickerWidget::AppendChoice(UWrapBox& Panel, const FText& Label, FSimpleDelegate Action)
{
	// 动态内容只添进派生 WBP 显式绑定的容器，外层弹窗结构和样式始终来自资产。
	UCastleWildcardChoiceButton* Button = NewObject<UCastleWildcardChoiceButton>(this);
	Button->SetStyle(OptionStyleTemplate->GetStyle());
	UTextBlock* Caption = NewObject<UTextBlock>(Button);
	Caption->SetFont(OptionTextTemplate->GetFont());
	Caption->SetColorAndOpacity(OptionTextTemplate->GetColorAndOpacity());
	Caption->SetText(Label);
	Button->SetContent(Caption);
	Button->BindChoice(MoveTemp(Action));
	Panel.AddChildToWrapBox(Button);
}

void UCastleWildcardRoomPickerWidget::UpdateSelection()
{
	auto UpdateButtons = [](UWrapBox& Panel, const int32 Selected)
	{
		for (int32 Index = 0; Index < Panel.GetChildrenCount(); ++Index)
		{
			// 单一选项以及当前选择均用选项按钮的禁用样式表示，可点其他选项改选。
			Panel.GetChildAt(Index)->SetIsEnabled(Index != Selected);
		}
	};
	UpdateButtons(*DefinitionChoices, DefinitionIndex);
	UpdateButtons(*ColorChoices, ColorIndex);
	UpdateButtons(*NumberChoices, NumberIndex);
	ConfirmButton->SetIsEnabled(Options.Definitions.IsValidIndex(DefinitionIndex)
		&& Options.Colors.IsValidIndex(ColorIndex) && Options.Numbers.IsValidIndex(NumberIndex));
	StatusText->SetText(FText::GetEmpty());
}

void UCastleWildcardRoomPickerWidget::Confirm()
{
	if (!Reward || !bChoosing || !Options.Definitions.IsValidIndex(DefinitionIndex) || !Options.Colors.IsValidIndex(ColorIndex)
		|| !Options.Numbers.IsValidIndex(NumberIndex))
	{
		UE_LOG(LogMoving_Castle, Warning, TEXT("万能卡弹窗 %s 必须选定全部属性后才能确认。"), *GetPathName());
		return;
	}
	bool bWentToTempBar;
	int32 ItemIndex;
	FText Failure;
	if (!Reward->TryConfirmClaim(Options.Definitions[DefinitionIndex], Options.Colors[ColorIndex],
		Options.Numbers[NumberIndex], bWentToTempBar, ItemIndex, Failure))
	{
		StatusText->SetText(Failure);
		return;
	}
	Finish(true);
}

void UCastleWildcardRoomPickerWidget::Cancel()
{
	if (!Reward || !bChoosing)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("万能卡弹窗 %s 没有可取消的奖励。"), *GetPathName());
		return;
	}
	FText Failure;
	if (!Reward->TryCancelClaim(Failure))
	{
		StatusText->SetText(Failure);
		return;
	}
	Finish(false);
}

void UCastleWildcardRoomPickerWidget::Finish(const bool bClaimed)
{
	bChoosing = false;
	Options = {};
	DefinitionChoices->ClearChildren();
	ColorChoices->ClearChildren();
	NumberChoices->ClearChildren();
	PickerPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (bClaimed)
	{
		Reward = nullptr;
		SetVisibility(ESlateVisibility::Collapsed);
		RemoveFromParent();
	}
	else
	{
		ClaimButton->SetVisibility(ESlateVisibility::Visible);
	}
	OnPickerClosed.Broadcast(bClaimed);
}

void UCastleWildcardRoomPickerWidget::NativeDestruct()
{
	if (Reward && bChoosing)
	{
		FText Failure;
		Reward->TryCancelClaim(Failure);
	}
	Reward = nullptr;
	bChoosing = false;
	Options = {};
	Super::NativeDestruct();
}
