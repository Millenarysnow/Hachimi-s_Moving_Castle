// Copyright Epic Games, Inc. All Rights Reserved.
#include "CastleDirectorWidgets.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Moving_Castle.h"

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

void UCastleRewardChoiceWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 SetIsFocusable(true);
 // BindWidget 缺失属于资产错误，不能创建临时按钮冒充配置成功。
 if (!ExperienceButton || !HealingButton || !ShopButton || !ExperienceLabel || !HealingLabel || !ShopLabel || !FailureText)
 {
  UE_LOG(LogMoving_Castle, Error, TEXT("奖励WBP %s 缺少必需绑定控件。"), *GetClass()->GetPathName());
  return;
 }
 ExperienceButton->OnClicked.AddDynamic(this, &UCastleRewardChoiceWidget::ChooseExperience);
 HealingButton->OnClicked.AddDynamic(this, &UCastleRewardChoiceWidget::ChooseHealing);
 ShopButton->OnClicked.AddDynamic(this, &UCastleRewardChoiceWidget::ChooseShop);
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
 if (!SummaryText || !FailureText || !RestartButton)
 {
  UE_LOG(LogMoving_Castle, Error, TEXT("游戏结束WBP %s 缺少必需绑定控件。"), *GetClass()->GetPathName());
  return;
 }
 RestartButton->OnClicked.AddDynamic(this, &UCastleGameOverWidget::Restart);
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
 return SummaryText && StatusText;
}
bool UCastleRewardChoiceWidget::HasRequiredBindings() const
{
 return ExperienceButton && HealingButton && ShopButton && ExperienceLabel && HealingLabel && ShopLabel && FailureText;
}
bool UCastleGameOverWidget::HasRequiredBindings() const
{
 return SummaryText && FailureText && RestartButton;
}
