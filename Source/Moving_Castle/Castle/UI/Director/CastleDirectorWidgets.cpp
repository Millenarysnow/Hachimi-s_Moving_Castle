// Copyright Epic Games, Inc. All Rights Reserved.
#include "CastleDirectorWidgets.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastleMainTowerComponent.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "Moving_Castle.h"

namespace
{
	/** 普通图和按下图都由 WBP 提供；不允许用默认笔刷伪装成已配置。 */
	bool HasTextureBrush(const UImage* Image)
	{
		if (!IsValid(Image)) { return false; }
		// 有资源引用还不够，零尺寸或不绘制的笔刷会在 Slate 中完全看不见。
		const FSlateBrush& Brush = Image->GetBrush();
		const auto Size = Brush.GetImageSize();
		return IsValid(Brush.GetResourceObject()) && Brush.GetDrawType() != ESlateBrushDrawType::NoDrawType
			&& Size.X > 0.0f && Size.Y > 0.0f;
	}

	/** 固定索引与 WBP 中的两个 Image 对应，检查层级以免按下时切到空控件。 */
	bool HasActionIconBrushes(const UWidgetSwitcher* Icons)
	{
		return IsValid(Icons) && Icons->GetNumWidgets() == 2
			&& HasTextureBrush(Cast<UImage>(Icons->GetWidgetAtIndex(0)))
			&& HasTextureBrush(Cast<UImage>(Icons->GetWidgetAtIndex(1)));
	}
}

bool UCastleDirectorNodeLabelWidget::HasRequiredBindings() const
{
	return IsValid(LabelText);
}

bool UCastleDirectorNodeLabelWidget::SetNodeLabel(const FText& Text, const FLinearColor& Color)
{
	if (!IsValid(LabelText))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("节点标签WBP %s 缺少必需控件 LabelText。"), *GetClass()->GetPathName());
		return false;
	}
	LabelText->SetText(Text);
	LabelText->SetColorAndOpacity(FSlateColor(Color));
	return true;
}

bool UCastleDirectorNodeLabelWidget::TryGetNodeLabel(FText& OutText, FLinearColor& OutColor) const
{
	if (!IsValid(LabelText))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("读取节点标签失败：WBP %s 缺少必需控件 LabelText。"), *GetClass()->GetPathName());
		return false;
	}
	OutText = LabelText->GetText();
	OutColor = LabelText->GetColorAndOpacity().GetSpecifiedColor();
	return true;
}

void UCastleDirectorHUDWidget::NativeTick(const FGeometry& Geometry, const float DeltaTime)
{
 Super::NativeTick(Geometry, DeltaTime);
 UCastleDirectorSubsystem* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
 if (SummaryText && StatusText && Director)
 {
  SummaryText->SetText(FText::FromString(Director->GetHUDSummary()));
  StatusText->SetText(Director->GetStatusMessage());
 }
}

bool UCastleDirectorHUDWidget::BindMainTowerHealth(UCastleMainTowerComponent* MainTower)
{
	if (!IsValid(MainTower) || !HasRequiredBindings())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("导演 HUD %s 无法绑定生命：主塔组件 %s 或必需心形图标/文本无效。"),
			*GetClass()->GetPathName(), *GetPathNameSafe(MainTower));
		return false;
	}
	HealthSource = MainTower;
	MainTower->OnHealthChanged.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandleTowerHealthChanged);
	HandleTowerHealthChanged(MainTower->CurrentHealth, MainTower->CurrentHealth, 0.0f);
	return true;
}

void UCastleDirectorHUDWidget::HandleTowerHealthChanged(float PreviousHealth, float CurrentHealth, float Damage)
{
	const UCastleMainTowerComponent* MainTower = HealthSource.Get();
	if (!IsValid(MainTower) || !IsValid(TowerHealthText))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("导演 HUD %s 无法更新主塔生命：组件 %s 或 TowerHealthText 已失效。"),
			*GetClass()->GetPathName(), *GetPathNameSafe(MainTower));
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// 沿用原摘要的显示精度，战斗仍使用主塔组件保存的浮点生命值。
	TowerHealthText->SetText(FText::FromString(FString::Printf(TEXT("主塔生命 %.0f/%.0f"), CurrentHealth, MainTower->MaxHealth)));
}

void UCastleDirectorHUDWidget::NativeDestruct()
{
	if (UCastleMainTowerComponent* MainTower = HealthSource.Get())
	{
		MainTower->OnHealthChanged.RemoveDynamic(this, &UCastleDirectorHUDWidget::HandleTowerHealthChanged);
		HealthSource.Reset();
	}
	Super::NativeDestruct();
}

void UCastleRewardChoiceWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 SetIsFocusable(true);
 // BindWidget 缺失属于资产错误，不能创建临时按钮冒充配置成功。
 if (!HasRequiredBindings())
 {
  UE_LOG(LogMoving_Castle, Error, TEXT("奖励 WBP %s 的必需控件、回血图标或商店普通/按下笔刷无效，请检查资源与显示尺寸。"), *GetClass()->GetPathName());
  return;
 }
 ExperienceButton->OnClicked.AddDynamic(this, &UCastleRewardChoiceWidget::ChooseExperience);
 HealingButton->OnClicked.AddDynamic(this, &UCastleRewardChoiceWidget::ChooseHealing);
 ShopButton->OnClicked.AddDynamic(this, &UCastleRewardChoiceWidget::ChooseShop);
 // 图标只是按钮内容，按下/释放只切换图像；点击仍走原有奖励事务。
 ShopButton->OnPressed.AddDynamic(this, &UCastleRewardChoiceWidget::ShowShopPressedIcon);
 ShopButton->OnReleased.AddDynamic(this, &UCastleRewardChoiceWidget::ShowShopNormalIcon);
}

void UCastleRewardChoiceWidget::ShowShopPressedIcon()
{
 ShopIconStates->SetActiveWidgetIndex(1);
}

void UCastleRewardChoiceWidget::ShowShopNormalIcon()
{
 ShopIconStates->SetActiveWidgetIndex(0);
}

void UCastleRewardChoiceWidget::NativeTick(const FGeometry& Geometry, const float DeltaTime)
{
 Super::NativeTick(Geometry, DeltaTime);
 UCastleDirectorSubsystem* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
 if (Director && ExperienceLabel && HealingLabel && ShopLabel && FailureText)
 {
  ExperienceLabel->SetText(Director->GetRewardLabel(ECastleDirectorReward::Experience));
  HealingLabel->SetText(Director->GetRewardLabel(ECastleDirectorReward::Healing));
  ShopLabel->SetText(Director->GetRewardLabel(ECastleDirectorReward::Shop));
  FailureText->SetText(Director->GetStatusMessage());
 }
}

void UCastleRewardChoiceWidget::ChooseExperience()
{
 FText Reason;
 GetWorld()->GetSubsystem<UCastleDirectorSubsystem>()->TryChooseReward(ECastleDirectorReward::Experience, Reason);
}
void UCastleRewardChoiceWidget::ChooseHealing()
{
 FText Reason;
 GetWorld()->GetSubsystem<UCastleDirectorSubsystem>()->TryChooseReward(ECastleDirectorReward::Healing, Reason);
}
void UCastleRewardChoiceWidget::ChooseShop()
{
 FText Reason;
 GetWorld()->GetSubsystem<UCastleDirectorSubsystem>()->TryChooseReward(ECastleDirectorReward::Shop, Reason);
}

void UCastleGameOverWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 SetIsFocusable(true);
 if (!HasRequiredBindings())
 {
  UE_LOG(LogMoving_Castle, Error, TEXT("游戏结束 WBP %s 的必需控件、骷髅图标或重试普通/按下笔刷无效，请检查资源与显示尺寸。"), *GetClass()->GetPathName());
  return;
 }
 RestartButton->OnClicked.AddDynamic(this, &UCastleGameOverWidget::Restart);
 // 世界已暂停时，Slate 仍能即时更新按压反馈；不改变重试实际触发时机。
 RestartButton->OnPressed.AddDynamic(this, &UCastleGameOverWidget::ShowRestartPressedIcon);
 RestartButton->OnReleased.AddDynamic(this, &UCastleGameOverWidget::ShowRestartNormalIcon);
}

void UCastleGameOverWidget::ShowRestartPressedIcon()
{
 RestartIconStates->SetActiveWidgetIndex(1);
}

void UCastleGameOverWidget::ShowRestartNormalIcon()
{
 RestartIconStates->SetActiveWidgetIndex(0);
}
void UCastleGameOverWidget::NativeTick(const FGeometry& Geometry, const float DeltaTime)
{
 Super::NativeTick(Geometry, DeltaTime);
 UCastleDirectorSubsystem* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
 if (Director && SummaryText && FailureText)
 {
  SummaryText->SetText(FText::FromString(FString::Printf(TEXT("游戏结束 · 通过 %d 关"), Director->GetClearedSteps())));
  FailureText->SetText(Director->GetGameOverFailure());
 }
}
void UCastleGameOverWidget::Restart()
{
 GetWorld()->GetSubsystem<UCastleDirectorSubsystem>()->RestartCurrentMap();
}

bool UCastleDirectorHUDWidget::HasRequiredBindings() const
{
  return SummaryText && StatusText && TowerHealthText && HasTextureBrush(HeartIcon);
}
bool UCastleRewardChoiceWidget::HasRequiredBindings() const
{
  return ExperienceButton && HealingButton && ShopButton && ExperienceLabel && HealingLabel && ShopLabel && FailureText
    && HasTextureBrush(HealingIcon) && HasActionIconBrushes(ShopIconStates);
}
bool UCastleGameOverWidget::HasRequiredBindings() const
{
  return SummaryText && FailureText && RestartButton && HasTextureBrush(SkullIcon) && HasActionIconBrushes(RestartIconStates);
}
