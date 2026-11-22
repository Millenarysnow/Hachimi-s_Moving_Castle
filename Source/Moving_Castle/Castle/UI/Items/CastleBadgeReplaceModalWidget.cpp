#include "Items/CastleBadgeReplaceModalWidget.h"

#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Moving_Castle.h"

bool UCastleBadgeReplaceModalWidget::BindBadges(UCastleBadgeSubsystem* InBadges)
{
	if (!IsValid(InBadges) || !IsValid(PendingBadgeLabel) || !IsValid(DiscardButton) || !IsValid(ReplaceSlots)
		|| ReplaceSlots->GetChildrenCount() != UCastleBadgeSubsystem::MaxBadges)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章弹窗 %s 绑定失败：需要有效徽章源、标题、六个替换按钮和舍弃按钮。"), *GetPathName());
		return false;
	}

	TArray<UButton*> Buttons;
	TArray<TObjectPtr<UTextBlock>> Labels;
	for (int32 Index = 0; Index < ReplaceSlots->GetChildrenCount(); ++Index)
	{
		UButton* Button = Cast<UButton>(ReplaceSlots->GetChildAt(Index));
		UTextBlock* Label = Button ? Cast<UTextBlock>(Button->GetContent()) : nullptr;
		if (!IsValid(Label))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章弹窗 %s 的槽位 %d 必须是带有文字的按钮。"), *GetPathName(), Index);
			return false;
		}
		Buttons.Add(Button);
		Labels.Add(Label);
	}

	if (UCastleBadgeSubsystem* Previous = Badges.Get())
	{
		Previous->OnBadgesChanged.RemoveDynamic(this, &UCastleBadgeReplaceModalWidget::RefreshBadges);
	}
	Badges = InBadges;
	SlotLabels = MoveTemp(Labels);
	Buttons[0]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceFirst);
	Buttons[1]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceSecond);
	Buttons[2]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceThird);
	Buttons[3]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceFourth);
	Buttons[4]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceFifth);
	Buttons[5]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceSixth);
	DiscardButton->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::Discard);
	InBadges->OnBadgesChanged.AddDynamic(this, &UCastleBadgeReplaceModalWidget::RefreshBadges);
	RefreshBadges();
	return true;
}

void UCastleBadgeReplaceModalWidget::NativeDestruct()
{
	if (UCastleBadgeSubsystem* Source = Badges.Get())
	{
		Source->OnBadgesChanged.RemoveDynamic(this, &UCastleBadgeReplaceModalWidget::RefreshBadges);
	}
	Badges.Reset();
	Super::NativeDestruct();
}

void UCastleBadgeReplaceModalWidget::RefreshBadges()
{
	const UCastleBadgeSubsystem* Source = Badges.Get();
	if (!Source)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章弹窗 %s 无法读取徽章子系统。"), *GetPathName());
		return;
	}
	FCastleBadgeDefinition Pending;
	if (!Source->TryGetPendingBadge(Pending))
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	const TArray<FCastleBadgeDefinition> Owned = Source->GetBadges();
	PendingBadgeLabel->SetText(FText::Format(NSLOCTEXT("CastleBadgeReplace", "Pending", "新徽章 {0}\n{1}"),
		Pending.GetDisplayName(), Pending.GetEffectSummary()));
	for (int32 Index = 0; Index < Owned.Num(); ++Index)
	{
		SlotLabels[Index]->SetText(FText::Format(NSLOCTEXT("CastleBadgeReplace", "Existing", "替换第 {0} 槽　{1}"),
			Index + 1, Owned[Index].GetDisplayName()));
	}
	SetVisibility(ESlateVisibility::Visible);
}

void UCastleBadgeReplaceModalWidget::Replace(const int32 SlotIndex)
{
	UCastleBadgeSubsystem* Source = Badges.Get();
	FCastleBadgeDefinition Pending;
	if (!Source || !Source->TryGetPendingBadge(Pending))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章弹窗 %s 没有可提交替换的徽章。"), *GetPathName());
		return;
	}
	FText Failure;
	Source->TryReplaceBadgeAt(SlotIndex, Pending, Failure);
}

void UCastleBadgeReplaceModalWidget::Discard()
{
	if (UCastleBadgeSubsystem* Source = Badges.Get())
	{
		Source->DiscardPendingBadge();
	}
	else
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章弹窗 %s 缺少徽章源，无法舍弃新徽章。"), *GetPathName());
	}
}
