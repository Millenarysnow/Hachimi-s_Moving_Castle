// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBackpackWidget.h"

#include "Blueprint/WidgetTree.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingCardWidget.h"
#include "CastleBackpackComponent.h"
#include "CastlePlayerController.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"

void UCastleBackpackWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (bWidgetTreeBuilt)
	{
		return;
	}

	if (WidgetTree == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("背包 UI 初始化失败：WidgetTree 为空。"));
		return;
	}

	UE_LOG(LogMoving_Castle, Log, TEXT("背包 UI NativeOnInitialized 开始构建 Slate 根控件。"));

		// 直接使用固定尺寸盒作为根节点。背包控件自身由 HUD 的视口槽位定位到
		// 屏幕底部，避免根 CanvasPanel 在 AddToViewport 的期望尺寸布局阶段得到零尺寸。
	RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BackpackRootSizeBox"));
	RootSizeBox->SetWidthOverride(1584.0f);
	RootSizeBox->SetHeightOverride(268.0f);
		WidgetTree->RootWidget = RootSizeBox;

	UBorder* PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BackpackPanelBorder"));
	PanelBorder->SetPadding(FMargin(12.0f, 10.0f, 12.0f, 10.0f));
	PanelBorder->SetBrushColor(FLinearColor(0.006f, 0.015f, 0.030f, 0.90f));
	RootSizeBox->AddChild(PanelBorder);

	UVerticalBox* PanelColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BackpackPanelColumn"));
	PanelBorder->SetContent(PanelColumn);

	CapacityText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CapacityText"));
	CapacityText->SetJustification(ETextJustify::Center);
	FSlateFontInfo CapacityFont = CapacityText->GetFont();
	CapacityFont.Size = 14;
	CapacityText->SetFont(CapacityFont);
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
	bWidgetTreeBuilt = true;
}

void UCastleBackpackWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UE_LOG(LogMoving_Castle, Log, TEXT("背包 UI NativeConstruct 已执行。"));

	FindActiveBoard();
	RefreshFromBackpack();
}

void UCastleBackpackWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bLoggedSlateGeometry)
	{
		bLoggedSlateGeometry = true;
		const FVector2D RootSize = WidgetTree != nullptr && WidgetTree->RootWidget != nullptr
			? WidgetTree->RootWidget->GetCachedGeometry().GetLocalSize()
			: FVector2D::ZeroVector;
		const bool bRootSlateValid = WidgetTree != nullptr && WidgetTree->RootWidget != nullptr && WidgetTree->RootWidget->GetCachedWidget().IsValid();
		UE_LOG(LogMoving_Castle, Log, TEXT("背包 UI 首次 Tick：自身几何=(%.1f, %.1f)，根控件=%s，根几何=(%.1f, %.1f)，根 Slate=%s，可见性=%d。"),
			MyGeometry.GetLocalSize().X,
			MyGeometry.GetLocalSize().Y,
				WidgetTree != nullptr ? *GetNameSafe(WidgetTree->RootWidget) : TEXT("None"),
			RootSize.X,
			RootSize.Y,
			bRootSlateValid ? TEXT("true") : TEXT("false"),
			static_cast<int32>(GetVisibility()));
	}

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
	if (ActiveBoard == nullptr)
	{
		UE_LOG(LogMoving_Castle, Warning, TEXT("背包 UI 暂未找到 ACastleBoard，将在后续 Tick 重试。"));
	}
}

void UCastleBackpackWidget::RefreshFromBackpack()
{
	if (ActiveBoard == nullptr || ActiveBoard->PrototypeBackpack == nullptr || CapacityText == nullptr || CardBox == nullptr)
	{
		return;
	}

	UCastleBackpackComponent* Backpack = ActiveBoard->PrototypeBackpack;
	const TArray<FCastleBuildingDescriptor> Items = Backpack->GetItems();
	const ACastlePlayerController* Controller = Cast<ACastlePlayerController>(GetOwningPlayer());
	const int32 SelectedIndex = Controller != nullptr ? Controller->GetSelectedBackpackIndex() : INDEX_NONE;
	if (bHasSynchronizedBackpack && AreDescriptorsEqual(CachedItems, Items)
		&& CachedSelection == SelectedIndex && CachedCapacity == Backpack->Capacity)
	{
		return;
	}

	CachedItems = Items;
	CachedSelection = SelectedIndex;
	CachedCapacity = Backpack->Capacity;
	bHasSynchronizedBackpack = true;

	const int32 Capacity = FMath::Max(Backpack->Capacity, 1);
	const int32 ItemCount = Items.Num();
	UE_LOG(LogMoving_Castle, Log, TEXT("背包 UI 同步完成：%d/%d 张卡牌。"), ItemCount, Capacity);
	CapacityText->SetText(FText::FromString(FString::Printf(TEXT("背包 %d / %d  |  数字键 1-9、0 选槽  |  当前第 %d 格  |  B 打开商店"),
		ItemCount, Capacity, SelectedIndex == INDEX_NONE ? 0 : SelectedIndex + 1)));
	const FVector2D PanelSize(static_cast<float>(Capacity * 156 + 24), 268.0f);
	RootSizeBox->SetWidthOverride(PanelSize.X);
	// 视口槽位的实际尺寸与根控件保持一致，避免 NativeConstruct 首次布局前
	// GetDesiredSize 返回零，导致控件内部已经有卡片但父级不给它任何绘制区域。
	SetDesiredSizeInViewport(PanelSize);
	// UE 5.8 的 SetDesiredSizeInViewport 会清除现有锚点，刷新尺寸后必须恢复，
	// 选牌或购买导致内容同步时也不能把底部面板移回左上角。
	SetAnchorsInViewport(FAnchors(0.5f, 1.0f));
	SetAlignmentInViewport(FVector2D(0.5f, 1.0f));

	CardBox->ClearChildren();
	CardWidgets.Reset();
	CardWidgets.Reserve(Capacity);

	for (int32 SlotIndex = 0; SlotIndex < Capacity; ++SlotIndex)
	{
		// 使用父级 PlayerController 作为上下文，确保卡片的 NativeOnInitialized
		// 在它自己的 Slate 根控件取走前执行。
		UCastleBuildingCardWidget* CardWidget = CreateWidget<UCastleBuildingCardWidget>(GetOwningPlayer(), UCastleBuildingCardWidget::StaticClass());
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
		// 当前槽位保持明亮，其余槽位降低透明度，选择结果有直接的视觉反馈。
		CardWidget->SetRenderOpacity(SlotIndex == SelectedIndex ? 1.0f : 0.60f);

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
