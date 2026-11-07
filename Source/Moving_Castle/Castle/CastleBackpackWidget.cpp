// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBackpackWidget.h"

#include "Blueprint/WidgetTree.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingCardWidget.h"
#include "CastleBackpackComponent.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"

void UCastleBackpackWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (WidgetTree == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("背包 UI 初始化失败：WidgetTree 为空。"));
		return;
	}

	// 面板宽度按固定容量计算，卡片数量变化时不会引起屏幕布局抖动。
	RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BackpackRootSizeBox"));
	RootSizeBox->SetWidthOverride(1584.0f);
	RootSizeBox->SetHeightOverride(240.0f);
	WidgetTree->RootWidget = RootSizeBox;

	UBorder* PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackpackPanelBorder"));
	PanelBorder->SetPadding(FMargin(12.0f, 10.0f, 12.0f, 10.0f));
	PanelBorder->SetBrushColor(FLinearColor(0.006f, 0.015f, 0.030f, 0.90f));
	RootSizeBox->AddChild(PanelBorder);

	UVerticalBox* PanelColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BackpackPanelColumn"));
	PanelBorder->SetContent(PanelColumn);

	CapacityText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CapacityText"));
	CapacityText->SetJustification(ETextJustify::Center);
	CapacityText->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.84f, 0.95f, 1.0f)));
	CapacityText->SetText(FText::FromString(TEXT("背包 0 / 10")));
	if (UVerticalBoxSlot* CapacitySlot = PanelColumn->AddChildToVerticalBox(CapacityText))
	{
		CapacitySlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
		CapacitySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	CardBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CardBox"));
	if (UVerticalBoxSlot* CardBoxSlot = PanelColumn->AddChildToVerticalBox(CardBox))
	{
		CardBoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
	}

	// 当前阶段不接收鼠标输入，保证原有中心射线和左键放置逻辑不被 UI 拦截。
	SetVisibility(ESlateVisibility::HitTestInvisible);

	FindActiveBoard();
	RefreshFromBackpack();
}

void UCastleBackpackWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (ActiveBoard == nullptr)
	{
		FindActiveBoard();
	}

	if (ActiveBoard != nullptr)
	{
		RefreshFromBackpack();
	}
}

void UCastleBackpackWidget::FindActiveBoard()
{
	if (GetWorld() == nullptr)
	{
		return;
	}

	ActiveBoard = Cast<ACastleBoard>(UGameplayStatics::GetActorOfClass(GetWorld(), ACastleBoard::StaticClass()));
}

void UCastleBackpackWidget::RefreshFromBackpack()
{
	if (ActiveBoard == nullptr || ActiveBoard->PrototypeBackpack == nullptr || CapacityText == nullptr || CardBox == nullptr)
	{
		return;
	}

	UCastleBackpackComponent* Backpack = ActiveBoard->PrototypeBackpack;
	const TArray<FCastleBuildingDescriptor> Items = Backpack->GetItems();
	if (bHasSynchronizedBackpack && AreDescriptorsEqual(CachedItems, Items))
	{
		return;
	}

	CachedItems = Items;
	bHasSynchronizedBackpack = true;

	const int32 Capacity = FMath::Max(Backpack->Capacity, 1);
	const int32 ItemCount = Items.Num();
	CapacityText->SetText(FText::FromString(FString::Printf(TEXT("背包 %d / %d"), ItemCount, Capacity)));
	RootSizeBox->SetWidthOverride(static_cast<float>(Capacity * 156 + 24));

	CardBox->ClearChildren();
	CardWidgets.Reset();
	CardWidgets.Reserve(Capacity);

	for (int32 SlotIndex = 0; SlotIndex < Capacity; ++SlotIndex)
	{
		UCastleBuildingCardWidget* CardWidget = CreateWidget<UCastleBuildingCardWidget>(GetWorld(), UCastleBuildingCardWidget::StaticClass());
		if (CardWidget == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("背包 UI 创建第 %d 张卡片失败。"), SlotIndex);
			continue;
		}

		if (Items.IsValidIndex(SlotIndex))
		{
			CardWidget->SetBuildingDescriptor(Items[SlotIndex]);
		}
		else
		{
			CardWidget->SetEmptySlot();
		}

		if (UHorizontalBoxSlot* CardSlot = CardBox->AddChildToHorizontalBox(CardWidget))
		{
			CardSlot->SetPadding(FMargin(4.0f, 0.0f, 4.0f, 0.0f));
			CardSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
		}
		CardWidgets.Add(CardWidget);
	}
}

bool UCastleBackpackWidget::AreDescriptorsEqual(const TArray<FCastleBuildingDescriptor>& Left, const TArray<FCastleBuildingDescriptor>& Right)
{
	if (Left.Num() != Right.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < Left.Num(); ++Index)
	{
		const FCastleBuildingDescriptor& LeftDescriptor = Left[Index];
		const FCastleBuildingDescriptor& RightDescriptor = Right[Index];
		if (LeftDescriptor.BuildingClass != RightDescriptor.BuildingClass
			|| LeftDescriptor.BuildingType != RightDescriptor.BuildingType
			|| LeftDescriptor.Color != RightDescriptor.Color
			|| LeftDescriptor.Number != RightDescriptor.Number
			|| LeftDescriptor.Level != RightDescriptor.Level)
		{
			return false;
		}
	}

	return true;
}
