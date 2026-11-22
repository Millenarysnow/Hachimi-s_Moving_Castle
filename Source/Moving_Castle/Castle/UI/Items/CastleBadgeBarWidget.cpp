#include "Items/CastleBadgeBarWidget.h"

#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Moving_Castle.h"

bool UCastleBadgeBarWidget::BindBadges(UCastleBadgeSubsystem* InBadges)
{
	if (!IsValid(InBadges) || !IsValid(BadgeSlots) || BadgeSlots->GetChildrenCount() != UCastleBadgeSubsystem::MaxBadges)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 绑定失败：需要有效徽章源和含六个子项的 BadgeSlots。"), *GetPathName());
		return false;
	}

	TArray<TObjectPtr<UTextBlock>> Labels;
	for (int32 Index = 0; Index < BadgeSlots->GetChildrenCount(); ++Index)
	{
		UTextBlock* Label = Cast<UTextBlock>(BadgeSlots->GetChildAt(Index));
		if (!IsValid(Label))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 的槽位 %d 需要绑定文字控件。"), *GetPathName(), Index);
			return false;
		}
		Labels.Add(Label);
	}

	if (UCastleBadgeSubsystem* Previous = Badges.Get())
	{
		Previous->OnBadgesChanged.RemoveDynamic(this, &UCastleBadgeBarWidget::RefreshBadges);
	}
	Badges = InBadges;
	SlotLabels = MoveTemp(Labels);
	InBadges->OnBadgesChanged.AddDynamic(this, &UCastleBadgeBarWidget::RefreshBadges);
	RefreshBadges();
	return true;
}

void UCastleBadgeBarWidget::NativeDestruct()
{
	if (UCastleBadgeSubsystem* Source = Badges.Get())
	{
		Source->OnBadgesChanged.RemoveDynamic(this, &UCastleBadgeBarWidget::RefreshBadges);
	}
	Badges.Reset();
	Super::NativeDestruct();
}

void UCastleBadgeBarWidget::RefreshBadges()
{
	const UCastleBadgeSubsystem* Source = Badges.Get();
	if (!Source)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 无法读取徽章子系统。"), *GetPathName());
		return;
	}
	const TArray<FCastleBadgeDefinition> Owned = Source->GetBadges();
	for (int32 Index = 0; Index < SlotLabels.Num(); ++Index)
	{
		FText Caption = NSLOCTEXT("CastleBadgeBar", "EmptySlot", "空徽章槽");
		if (Owned.IsValidIndex(Index))
		{
			Caption = FText::Format(NSLOCTEXT("CastleBadgeBar", "SlotCaption", "{0}\n{1}"),
				Owned[Index].GetDisplayName(), Owned[Index].GetEffectSummary());
		}
		SlotLabels[Index]->SetText(Caption);
	}
}
