#include "Items/CastleBadgeBarWidget.h"

#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Moving_Castle.h"

bool UCastleBadgeBarWidget::BindBadges(UCastleBadgeSubsystem* InBadges)
{
	if (!IsValid(InBadges) || !IsValid(BadgeSlots) || BadgeSlots->GetChildrenCount() != UCastleBadgeSubsystem::MaxBadges)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 绑定失败：需要有效徽章源和含六个子项的 BadgeSlots。"), *GetClass()->GetPathName());
		return false;
	}

	TArray<TObjectPtr<UTextBlock>> Labels;
	TArray<TObjectPtr<UImage>> Icons;
	for (int32 Index = 0; Index < BadgeSlots->GetChildrenCount(); ++Index)
	{
		// 每个槽由一个容器承载，容器内的图标和说明占独立位置。
		const UPanelWidget* SlotLayout = Cast<UPanelWidget>(BadgeSlots->GetChildAt(Index));
		if (!IsValid(SlotLayout) || SlotLayout->GetChildrenCount() != 2)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 的第 %d 槽必须包含图标和说明容器。"), *GetClass()->GetPathName(), Index + 1);
			return false;
		}
		auto* Icon = Cast<UImage>(SlotLayout->GetChildAt(0));
		auto* Label = Cast<UTextBlock>(SlotLayout->GetChildAt(1));
		if (!IsValid(Icon) || !IsValid(Label))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 的第 %d 槽必须依次绑定 UImage 和 UTextBlock。"), *GetClass()->GetPathName(), Index + 1);
			return false;
		}
		Labels.Add(Label);
		Icons.Add(Icon);
	}

	SlotLabels = MoveTemp(Labels);
	SlotIcons = MoveTemp(Icons);
	if (!UpdateBadgeSlots(*InBadges))
	{
		return false;
	}
	if (UCastleBadgeSubsystem* Previous = Badges.Get())
	{
		Previous->OnBadgesChanged.RemoveDynamic(this, &UCastleBadgeBarWidget::RefreshBadges);
	}
	Badges = InBadges;
	InBadges->OnBadgesChanged.AddDynamic(this, &UCastleBadgeBarWidget::RefreshBadges);
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
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 无法读取徽章子系统。"), *GetClass()->GetPathName());
		return;
	}
	if (!UpdateBadgeSlots(*Source))
	{
		// 资产校验失败后停止展示，避免给玩家留下仍是旧库存的印象。
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

bool UCastleBadgeBarWidget::UpdateBadgeSlots(const UCastleBadgeSubsystem& Source)
{
	const TArray<FCastleBadgeDefinition> Owned = Source.GetBadges();
	if (Owned.Num() > SlotLabels.Num())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 收到 %d 枚徽章，超过 %d 个展示槽。"),
			*GetClass()->GetPathName(), Owned.Num(), SlotLabels.Num());
		return false;
	}
	for (int32 Index = 0; Index < Owned.Num(); ++Index)
	{
		if (!IsValid(Owned[Index].Icon))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 的第 %d 槽徽章 %s 缺少有效 Icon。"),
				*GetClass()->GetPathName(), Index + 1, *Owned[Index].GetDisplayName().ToString());
			return false;
		}
	}
	for (int32 Index = 0; Index < SlotLabels.Num(); ++Index)
	{
		const bool bOccupied = Owned.IsValidIndex(Index);
		// 保留 WBP 规定的图标尺寸，空槽清除画刷并用 Hidden 保持两列对齐。
		SlotIcons[Index]->SetBrushFromTexture(bOccupied ? Owned[Index].Icon.Get() : nullptr, false);
		SlotIcons[Index]->SetVisibility(bOccupied ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		SlotLabels[Index]->SetText(bOccupied ? FText::Format(NSLOCTEXT("CastleBadgeBar", "SlotCaption", "{0}\n{1}"),
			Owned[Index].GetDisplayName(), Owned[Index].GetEffectSummary()) : NSLOCTEXT("CastleBadgeBar", "EmptySlot", "空徽章槽"));
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	return true;
}
