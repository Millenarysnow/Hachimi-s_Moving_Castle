#include "Castle/Tutorial/CastleTutorialWidgets.h"

#include "Castle/Tutorial/CastleTutorialDirector.h"
#include "Castle/UI/Cards/CastleShopCardWidget.h"
#include "CastleBoard.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Moving_Castle.h"

bool UCastleTutorialTextWidget::SetTutorialText(const FText& Text)
{
	if (!IsValid(TutorialText))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("教程界面 %s 缺少 TutorialText。"), *GetClass()->GetPathName());
		return false;
	}
	TutorialText->SetText(Text);
	return true;
}

void UCastleTutorialRewardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (CopyButton) { CopyButton->OnClicked.AddUniqueDynamic(this, &UCastleTutorialRewardWidget::ChooseCopy); }
	if (CardButton) { CardButton->OnClicked.AddUniqueDynamic(this, &UCastleTutorialRewardWidget::ClaimCard); }
}

bool UCastleTutorialRewardWidget::HasRequiredBindings() const
{
	if (!Choices || !GoldButton || !HealingButton || !CopyButton || !CardButton)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("教程奖励界面 %s 缺少选择容器、奖励按钮或单卡容器。"), *GetClass()->GetPathName());
		return false;
	}
	return true;
}

bool UCastleTutorialRewardWidget::Configure(ACastleTutorialDirector& Director)
{
	if (!HasRequiredBindings()) { return false; }
	OwnerDirector = &Director;
	const FText Locked = INVTEXT("本次教学请先体验复制功能房间");
	// 金币与回血仅展示通关选项。教学导演仍通过复制牌引导后续合成。
	GoldButton->SetToolTipText(Locked);
	HealingButton->SetToolTipText(Locked);
	GoldButton->SetIsEnabled(false);
	HealingButton->SetIsEnabled(false);
	Choices->SetVisibility(ESlateVisibility::Visible);
	CardButton->SetVisibility(ESlateVisibility::Collapsed);
	return true;
}

bool UCastleTutorialRewardWidget::ShowCard(const FCastleBuildingDescriptor& Descriptor, ACastleBoard& Board)
{
	// 复用正式商店的 3D 卡面，外层按钮是本次领取的唯一交互。
	auto* Card = CreateWidget<UCastleShopCardWidget>(GetOwningPlayer());
	if (!Card || !Card->ConfigureStandalone(Descriptor, &Board)) { return false; }
	Card->SetVisibility(ESlateVisibility::HitTestInvisible);
	CardButton->SetContent(Card);
	Choices->SetVisibility(ESlateVisibility::Collapsed);
	CardButton->SetVisibility(ESlateVisibility::Visible);
	return true;
}

void UCastleTutorialRewardWidget::ChooseCopy()
{
	if (auto* Director = OwnerDirector.Get()) { Director->ChooseCopyReward(); }
}

void UCastleTutorialRewardWidget::ClaimCard()
{
	if (auto* Director = OwnerDirector.Get()) { Director->ClaimCopyReward(); }
}
