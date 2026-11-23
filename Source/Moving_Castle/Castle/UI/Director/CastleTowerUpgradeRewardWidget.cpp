#include "CastleTowerUpgradeRewardWidget.h"

#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/UI/Items/CastleWildcardRoomPickerWidget.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Moving_Castle.h"

bool UCastleTowerUpgradeRewardWidget::BindDirector(UCastleDirectorSubsystem& InDirector)
{
	if (!RewardRows || !ContinueButton || !RewardButtonTemplate || !RewardTextTemplate)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("升级领奖 %s 缺少列表、继续按钮或 WBP 样式模板。"), *GetPathName());
		return false;
	}
	Director = &InDirector;
	ContinueButton->OnClicked.AddUniqueDynamic(this, &UCastleTowerUpgradeRewardWidget::Continue);
	RewardButtonTemplate->SetVisibility(ESlateVisibility::Collapsed);
	return true;
}

void UCastleTowerUpgradeRewardWidget::RefreshRewards(const TConstArrayView<FCastleTowerUpgradeRewardEntry> Entries)
{
	// 每次领取后重建数据行，按钮索引与导演当前列表完全同步。
	RewardRows->ClearChildren();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FCastleTowerUpgradeRewardEntry& Entry = Entries[Index];
		auto* Button = NewObject<UCastleWildcardChoiceButton>(this);
		auto* Caption = NewObject<UTextBlock>(Button);
		Button->SetStyle(RewardButtonTemplate->GetStyle());
		Caption->SetFont(RewardTextTemplate->GetFont());
		Caption->SetColorAndOpacity(RewardTextTemplate->GetColorAndOpacity());
		switch (Entry.Kind)
		{
		case ECastleTowerUpgradeReward::PathPoint: Caption->SetText(FText::Format(INVTEXT("+{0} 路径点"), Entry.Count)); break;
		case ECastleTowerUpgradeReward::NPC: Caption->SetText(FText::Format(INVTEXT("+{0} 普通 NPC"), Entry.Count)); break;
		case ECastleTowerUpgradeReward::CustomRoom: Caption->SetText(INVTEXT("领取完全自定义房间")); break;
		}
		Button->SetContent(Caption);
		Button->BindChoice(FSimpleDelegate::CreateWeakLambda(this, [this, Index]
		{
			if (UCastleDirectorSubsystem* Source = Director.Get()) { Source->ClaimTowerUpgradeReward(Index); }
		}));
		RewardRows->AddChild(Button);
	}
}

void UCastleTowerUpgradeRewardWidget::Continue()
{
	if (UCastleDirectorSubsystem* Source = Director.Get()) { Source->DiscardTowerUpgradeRewards(); }
}
