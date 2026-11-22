#include "Items/CastleTempBuildBarWidget.h"

#include "CastleBackpackComponent.h"
#include "CastleBuildingDefinition.h"
#include "Components/TextBlock.h"
#include "Moving_Castle.h"

bool UCastleTempBuildBarWidget::BindBackpack(UCastleBackpackComponent* InBackpack)
{
	if (!IsValid(InBackpack) || !StackCount || !FrontCard)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("临时建筑栏 %s 缺少背包、StackCount 或 FrontCard，拒绝挂载。"), *GetPathName());
		return false;
	}
	if (UCastleBackpackComponent* Previous = Backpack.Get())
	{
		Previous->OnBackpackChanged.RemoveDynamic(this, &UCastleTempBuildBarWidget::RefreshCards);
	}
	Backpack = InBackpack;
	InBackpack->OnBackpackChanged.AddDynamic(this, &UCastleTempBuildBarWidget::RefreshCards);
	RefreshCards();
	return true;
}

void UCastleTempBuildBarWidget::NativeDestruct()
{
	if (UCastleBackpackComponent* Source = Backpack.Get())
	{
		Source->OnBackpackChanged.RemoveDynamic(this, &UCastleTempBuildBarWidget::RefreshCards);
	}
	Backpack.Reset();
	Super::NativeDestruct();
}

void UCastleTempBuildBarWidget::RefreshCards()
{
	const UCastleBackpackComponent* Source = Backpack.Get();
	if (!Source)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("临时建筑栏 %s 的背包已失效。"), *GetPathName());
		return;
	}
	const TArray<FCastleBuildingDescriptor> Cards = Source->GetTempItems();
	if (Cards.IsEmpty())
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	StackCount->SetText(FText::Format(INVTEXT("临时建筑栏 ({0})"), FText::AsNumber(Cards.Num())));
	FrontCard->SetText(Cards[0].Definition->DisplayName);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}
