#include "Items/CastleBadgeBarWidget.h"

#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Engine/Texture2D.h"
#include "Moving_Castle.h"

bool UCastleBadgeBarWidget::BindBadges(UCastleBadgeSubsystem* InBadges)
{
	// 重新绑定失败时，旧库存的事件不能再把已拒绝的徽章栏显示出来。
	UnbindBadges();
	SetVisibility(ESlateVisibility::Collapsed);
	if (!IsValid(InBadges))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 绑定失败：缺少有效徽章源。"), *GetClass()->GetPathName());
		return false;
	}
	if (!UpdateBadgeSlots(*InBadges))
	{
		return false;
	}
	Badges = InBadges;
	InBadges->OnBadgesChanged.AddDynamic(this, &UCastleBadgeBarWidget::RefreshBadges);
	return true;
}

void UCastleBadgeBarWidget::NativeDestruct()
{
	UnbindBadges();
	Super::NativeDestruct();
}

void UCastleBadgeBarWidget::UnbindBadges()
{
	if (UCastleBadgeSubsystem* Source = Badges.Get())
	{
		Source->OnBadgesChanged.RemoveDynamic(this, &UCastleBadgeBarWidget::RefreshBadges);
	}
	Badges.Reset();
}

void UCastleBadgeBarWidget::RefreshBadges()
{
	const UCastleBadgeSubsystem* Source = Badges.Get();
	if (!Source)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 无法读取徽章子系统。"), *GetClass()->GetPathName());
		SetVisibility(ESlateVisibility::Collapsed);
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
	constexpr int32 SlotCount = UCastleBadgeSubsystem::MaxBadges;
	if (!IsValid(BadgeSlots) || BadgeSlots->GetChildrenCount() != SlotCount)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 需要含六个子项的 BadgeSlots。"), *GetClass()->GetPathName());
		return false;
	}

	UOverlay* Slots[SlotCount];
	UImage* Icons[SlotCount];
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		// 依照 WBP 的绘制顺序校验底框和图标层，刷新前先核对整栏资源。
		UOverlay* BadgeSlot = Cast<UOverlay>(BadgeSlots->GetChildAt(Index));
		if (!IsValid(BadgeSlot) || BadgeSlot->GetChildrenCount() != 2)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 的第 %d 槽需要含底框和图标的 Overlay。"),
				*GetClass()->GetPathName(), Index + 1);
			return false;
		}
		const UImage* Frame = Cast<UImage>(BadgeSlot->GetChildAt(0));
		UImage* Icon = Cast<UImage>(BadgeSlot->GetChildAt(1));
		if (!IsValid(Frame) || !IsValid(Icon))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 的第 %d 槽需要依次绑定底框和图标 UImage。"),
				*GetClass()->GetPathName(), Index + 1);
			return false;
		}
		const auto* Texture = Cast<UTexture2D>(Frame->GetBrush().GetResourceObject());
		if (!IsValid(Texture) || Texture->GetPathName() != TEXT("/Game/MovingCastle/UI/Textures/T_UI_BadgeSlot.T_UI_BadgeSlot")
			|| Texture->LODGroup != TEXTUREGROUP_UI || Texture->CompressionSettings != TC_EditorIcon
			|| !Texture->SRGB || !Texture->NeverStream
#if WITH_EDITORONLY_DATA
			|| Texture->MipGenSettings != TMGS_NoMipmaps
#endif
		)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 的第 %d 槽缺少有效底框纹理 T_UI_BadgeSlot 或纹理设置不符（当前 %s）。"),
				*GetClass()->GetPathName(), Index + 1, *GetPathNameSafe(Texture));
			return false;
		}
		const FVector2D FrameSize = Frame->GetBrush().GetImageSize();
		const FVector2D IconSize = Icon->GetBrush().GetImageSize();
		if (!(IconSize.X > 0 && IconSize.Y > 0 && IconSize.X < FrameSize.X && IconSize.Y < FrameSize.Y)
			|| Frame->GetBrush().DrawAs == ESlateBrushDrawType::NoDrawType
			|| Icon->GetBrush().DrawAs == ESlateBrushDrawType::NoDrawType)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 的第 %d 槽需要可绘制且正尺寸的底框与图标，图标必须小于底框。"),
				*GetClass()->GetPathName(), Index + 1);
			return false;
		}
		Slots[Index] = BadgeSlot;
		Icons[Index] = Icon;
	}

	const TArray<FCastleBadgeDefinition> Owned = Source.GetBadges();
	if (Owned.Num() > SlotCount)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("徽章栏 %s 收到 %d 枚徽章，超过 %d 个展示槽。"),
			*GetClass()->GetPathName(), Owned.Num(), SlotCount);
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
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		const bool bOccupied = Owned.IsValidIndex(Index);
		// 保留 WBP 的图标尺寸，只让占用槽响应悬停。空槽及槽间空白继续透传世界指针。
		Icons[Index]->SetBrushFromTexture(bOccupied ? Owned[Index].Icon.Get() : nullptr, false);
		Icons[Index]->SetVisibility(bOccupied ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		Slots[Index]->SetVisibility(bOccupied ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
		Slots[Index]->SetToolTipText(bOccupied ? FText::Format(NSLOCTEXT("CastleBadgeBar", "SlotTooltip", "{0}\n{1}"),
			Owned[Index].GetDisplayName(), Owned[Index].GetEffectSummary()) : FText::GetEmpty());
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	return true;
}
