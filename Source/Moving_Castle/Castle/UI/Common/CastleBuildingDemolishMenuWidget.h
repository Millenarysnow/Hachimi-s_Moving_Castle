// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleBuildingDemolishMenuWidget.generated.h"

class ACastleBoard;
class ACastleBuildingBase;
class UBorder;
class UButton;
class UCastleShopCardWidget;
class USizeBox;

/** WBP 提供悬浮面板和两个视觉区。回收事务保留在控制器与棋盘。 */
UCLASS()
class MOVING_CASTLE_API UCastleBuildingDemolishMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	bool ConfigureTarget(ACastleBuildingBase& Building, const FIntVector& InTargetCell, const ACastleBoard& Board,
		FVector2D ScreenPosition, FVector2D LogicalOffset);
	ACastleBuildingBase* GetTargetBuilding() const { return TargetBuilding.Get(); }
	const FIntVector& GetTargetCell() const { return TargetCell; }
	const UCastleShopCardWidget* GetBuildingCard() const { return BuildingCard; }
	static FVector2D ClampPopupPosition(FVector2D Point, FVector2D PanelSize, FVector2D ViewportSize);

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& Event) override;

private:
	void PlacePanel(FVector2D ViewportSize, float Scale);
	UFUNCTION() void HandleDemolishClicked();

	UPROPERTY(meta=(BindWidget)) TObjectPtr<UBorder> PopupPanel;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<USizeBox> BuildingCardSlot;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> DemolishButton;
	UPROPERTY(Transient) TObjectPtr<UCastleShopCardWidget> BuildingCard;
	TWeakObjectPtr<ACastleBuildingBase> TargetBuilding;
	/** 同一 Actor 可能横跨多格，提交必须保留点按时选择的那个占格。 */
	FIntVector TargetCell = FIntVector::ZeroValue;
	/** 原始点击保留为像素坐标，视口改变尺寸或 DPI 后重新计算面板位置。 */
	FVector2D ClickPixel = FVector2D::ZeroVector;
	/** 经典模式为准星留出空隙，设计模式直接使用点击点。 */
	FVector2D AnchorOffset = FVector2D::ZeroVector;
};
