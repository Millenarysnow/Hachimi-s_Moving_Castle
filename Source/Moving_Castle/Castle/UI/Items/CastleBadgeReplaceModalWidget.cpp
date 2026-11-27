#include "Items/CastleBadgeReplaceModalWidget.h"

#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Moving_Castle.h"

bool UCastleBadgeReplaceModalWidget::BindBadges(UCastleBadgeSubsystem* InBadges)
{
	if (!IsValid(InBadges) || !IsValid(PendingBadgeLabel) || !IsValid(PendingBadgeIcon) || !IsValid(DiscardButton)
		|| !IsValid(ReplaceSlots) || !IsValid(ReplaceIcons) || !IsValid(ReplaceSlots->GetParent())
		|| ReplaceSlots->GetParent() != ReplaceIcons->GetParent()
		|| ReplaceSlots->GetChildrenCount() != UCastleBadgeSubsystem::MaxBadges
		|| ReplaceIcons->GetChildrenCount() != UCastleBadgeSubsystem::MaxBadges)
	{
		UE_LOG(LogMoving_Castle, Error,
			TEXT("徽章弹窗 %s 绑定失败：需要徽章源、候选图标和说明、同层的六个替换按钮和六个图标，以及舍弃按钮。"),
			*GetClass()->GetPathName());
		return false;
	}

	TArray<TObjectPtr<UButton>> Buttons;
	TArray<TObjectPtr<UImage>> Icons;
	for (int32 Index = 0; Index < ReplaceSlots->GetChildrenCount(); ++Index)
	{
		auto* Button = Cast<UButton>(ReplaceSlots->GetChildAt(Index));
		auto* Icon = Cast<UImage>(ReplaceIcons->GetChildAt(Index));
		if (!IsValid(Button) || !IsValid(Cast<UTextBlock>(Button->GetContent())) || !IsValid(Icon))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章弹窗 %s 的第 %d 槽需要带直接文字子控件的按钮及对应 UImage。"),
				*GetClass()->GetPathName(), Index + 1);
			return false;
		}
		Buttons.Add(Button);
		Icons.Add(Icon);
	}

	SlotButtons = MoveTemp(Buttons);
	SlotIcons = MoveTemp(Icons);
	if (!UpdateBadgeSlots(*InBadges))
	{
		return false;
	}
	if (UCastleBadgeSubsystem* Previous = Badges.Get())
	{
		Previous->OnBadgesChanged.RemoveDynamic(this, &UCastleBadgeReplaceModalWidget::RefreshBadges);
	}
	Badges = InBadges;
	SlotButtons[0]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceFirst);
	SlotButtons[1]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceSecond);
	SlotButtons[2]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceThird);
	SlotButtons[3]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceFourth);
	SlotButtons[4]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceFifth);
	SlotButtons[5]->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::ReplaceSixth);
	DiscardButton->OnClicked.AddUniqueDynamic(this, &UCastleBadgeReplaceModalWidget::Discard);
	InBadges->OnBadgesChanged.AddDynamic(this, &UCastleBadgeReplaceModalWidget::RefreshBadges);
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
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章弹窗 %s 无法读取徽章子系统。"), *GetClass()->GetPathName());
		return;
	}
	if (!UpdateBadgeSlots(*Source))
	{
		// 缺图时拒绝呈现这次替换，避免上一笔候选的图文仍可被点击。
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool UCastleBadgeReplaceModalWidget::UpdateBadgeSlots(const UCastleBadgeSubsystem& Source)
{
	FCastleBadgeDefinition Pending;
	const bool bHasPending = Source.TryGetPendingBadge(Pending);
	if (bHasPending && !IsValid(Pending.Icon))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章弹窗 %s 的候选徽章 %s 缺少有效 Icon。"),
			*GetClass()->GetPathName(), *Pending.GetDisplayName().ToString());
		return false;
	}

	// 关闭后清除预览引用。下一笔替换在显示前重新从库存读取六行。
	const TArray<FCastleBadgeDefinition> Owned = bHasPending ? Source.GetBadges() : TArray<FCastleBadgeDefinition>();
	if (Owned.Num() > SlotButtons.Num())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章弹窗 %s 收到 %d 个替换选项，超过 %d 个按钮。"),
			*GetClass()->GetPathName(), Owned.Num(), SlotButtons.Num());
		return false;
	}
	for (int32 Index = 0; Index < Owned.Num(); ++Index)
	{
		if (!IsValid(Owned[Index].Icon))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章弹窗 %s 的第 %d 槽徽章 %s 缺少有效 Icon。"),
				*GetClass()->GetPathName(), Index + 1, *Owned[Index].GetDisplayName().ToString());
			return false;
		}
	}

	PendingBadgeIcon->SetBrushFromTexture(Pending.Icon.Get(), false);
	PendingBadgeIcon->SetVisibility(bHasPending ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	PendingBadgeLabel->SetText(bHasPending ? FText::Format(NSLOCTEXT("CastleBadgeReplace", "Pending", "新徽章 {0}\n{1}"),
		Pending.GetDisplayName(), Pending.GetEffectSummary()) : FText::GetEmpty());
	for (int32 Index = 0; Index < SlotButtons.Num(); ++Index)
	{
		const bool bOccupied = Owned.IsValidIndex(Index);
		SlotIcons[Index]->SetBrushFromTexture(bOccupied ? Owned[Index].Icon.Get() : nullptr, false);
		SlotIcons[Index]->SetVisibility(bOccupied ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		SlotButtons[Index]->SetIsEnabled(bOccupied);
		CastChecked<UTextBlock>(SlotButtons[Index]->GetContent())->SetText(bOccupied
			? FText::Format(NSLOCTEXT("CastleBadgeReplace", "ReplaceSlotCaption", "替换第 {0} 槽　{1}\n{2}"),
				Index + 1, Owned[Index].GetDisplayName(), Owned[Index].GetEffectSummary())
			: NSLOCTEXT("CastleBadgeBar", "EmptySlot", "空徽章槽"));
	}
	SetVisibility(bHasPending ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	return true;
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
