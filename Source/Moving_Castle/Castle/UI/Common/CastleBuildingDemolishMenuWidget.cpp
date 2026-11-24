// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingDemolishMenuWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastlePlayerController.h"
#include "Castle/UI/Cards/CastleShopCardWidget.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Moving_Castle.h"

bool UCastleBuildingDemolishMenuWidget::ConfigureTarget(ACastleBuildingBase& Building, const FIntVector& InTargetCell,
	const ACastleBoard& Board, FVector2D ScreenPosition, FVector2D LogicalOffset)
{
	// 槽位由 WBP 定义，检查完整之后才创建并订阅可提交拆除的控件。
	const TPair<const TCHAR*, UWidget*> Required[] = {{TEXT("PopupPanel"), PopupPanel.Get()},
		{TEXT("BuildingCardSlot"), BuildingCardSlot.Get()}, {TEXT("DemolishButton"), DemolishButton.Get()}};
	for (const auto& Control : Required)
	{
		if (!IsValid(Control.Value))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("%s 缺少必需控件 %s。"), *GetClass()->GetPathName(), Control.Key);
			return false;
		}
	}
	if (!Cast<UCanvasPanelSlot>(PopupPanel->Slot) || BuildingCardSlot->GetContent())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("%s 的 PopupPanel 需要 Canvas 槽，BuildingCardSlot 必须留给运行时卡牌。"),
			*GetClass()->GetPathName());
		return false;
	}
	if (Board.GetBuildingAt(InTargetCell) != &Building)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("%s 收到建筑 %s 不占据的拆除格 %s。"),
			*GetClass()->GetPathName(), *Building.GetPathName(), *InTargetCell.ToString());
		return false;
	}
	// 卡片展示实际退回背包的一级建筑，保留落定建筑的定义与色号。
	FCastleBuildingDescriptor Recovered = Building.GetBuildingDescriptor();
	Recovered.Level = 1;
	BuildingCard = CreateWidget<UCastleShopCardWidget>(GetOwningPlayer());
	if (!BuildingCard || !BuildingCard->ConfigureStandalone(Recovered, &Board))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("%s 无法为建筑 %s 配置独立卡牌。"), *GetClass()->GetPathName(), *Building.GetPathName());
		return false;
	}
	BuildingCardSlot->SetContent(BuildingCard);
	TargetBuilding = &Building;
	TargetCell = InTargetCell;
	ClickPixel = ScreenPosition;
	AnchorOffset = LogicalOffset;
	DemolishButton->OnClicked.AddUniqueDynamic(this, &UCastleBuildingDemolishMenuWidget::HandleDemolishClicked);
	// 先计算真实卡牌和按钮的总尺寸，第一帧就定位到视口内。
	ForceLayoutPrepass();
	const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
	PlacePanel(UWidgetLayoutLibrary::GetViewportSize(this) / Scale, Scale);
	return true;
}

FVector2D UCastleBuildingDemolishMenuWidget::ClampPopupPosition(FVector2D Point, FVector2D PanelSize, FVector2D ViewportSize)
{
	const FVector2D Limit = ViewportSize - PanelSize;
	return FVector2D(FMath::Clamp(Point.X, 0.0, FMath::Max(0.0, Limit.X)),
		FMath::Clamp(Point.Y, 0.0, FMath::Max(0.0, Limit.Y)));
}

void UCastleBuildingDemolishMenuWidget::PlacePanel(FVector2D ViewportSize, float Scale)
{
	const FVector2D Desired = PopupPanel->GetDesiredSize();
	if (Desired.X <= 0.0 || Desired.Y <= 0.0 || ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0) { return; }
	// Canvas 位置使用逻辑坐标。极小视口下等比缩放面板，以保留可点击的拆除按钮。
	const double Fit = FMath::Min3(1.0, ViewportSize.X / Desired.X, ViewportSize.Y / Desired.Y);
	PopupPanel->SetRenderScale(FVector2D(Fit, Fit));
	CastChecked<UCanvasPanelSlot>(PopupPanel->Slot)->SetPosition(
		ClampPopupPosition(ClickPixel / Scale + AnchorOffset, Desired * Fit, ViewportSize));
}

void UCastleBuildingDemolishMenuWidget::NativeTick(const FGeometry& MyGeometry, float DeltaSeconds)
{
	Super::NativeTick(MyGeometry, DeltaSeconds);
	PlacePanel(MyGeometry.GetLocalSize(), UWidgetLayoutLibrary::GetViewportScale(this));
}

FReply UCastleBuildingDemolishMenuWidget::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
	// 仅面板本体可命中。内部留白消费点击，面板外的 HUD 按钮与世界仍正常收到事件。
	return FReply::Handled();
}

FReply UCastleBuildingDemolishMenuWidget::NativeOnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
	return NativeOnMouseButtonDown(MyGeometry, Event);
}

void UCastleBuildingDemolishMenuWidget::HandleDemolishClicked()
{
	if (auto* Controller = GetOwningPlayer<ACastlePlayerController>()) { Controller->ConfirmBuildingDemolish(*this); }
}
