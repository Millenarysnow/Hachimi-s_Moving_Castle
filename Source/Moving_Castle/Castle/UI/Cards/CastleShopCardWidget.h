// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleGameplayTypes.h"
#include "CoreMinimal.h"
#include "CastleShopCardWidget.generated.h"

class ACastleBoard;
class UCastleCardPortrait;
class UCastleCardPortraitSubsystem;
class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTextBlock;
class UTextureRenderTarget2D;

/** 商店建筑牌只负责展示和悬停。未接管交易焦点，卡牌上下方的操作仍由商店管理。 */
UCLASS()
class MOVING_CASTLE_API UCastleShopCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCastleShopCardWidget(const FObjectInitializer& ObjectInitializer);

	/** 用商品原始描述设置色号与取景；错误以明确的卡面消息及日志呈现，不替换商品。 */
	bool Configure(const FCastleBuildingDescriptor& Descriptor, const ACastleBoard* Board, bool bSold);

	/** 普通与箔光是同一张牌的表现状态，不触发重新成像。 */
	void SetFoilPreview(bool bEnabled);

	/** 商店隐去或重建列表时显式释放动态画面，不依赖鼠标离开事件能否送达。 */
	void EndHover();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeOnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void UpdateTilt(const FGeometry& Geometry, const FPointerEvent& Event);
	void RefreshPortrait();
	void ApplyFaceState();
	bool ReportSetupError(const FCastleBuildingDescriptor& Descriptor, const FText& Reason);
	void SetPortraitStatus(const FText& Message, const FText& Details);

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FaceMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Face;

	UPROPERTY(Transient)
	TObjectPtr<UCastleCardPortraitSubsystem> Portraits;

	UPROPERTY(Transient)
	TObjectPtr<UCastleCardPortrait> Portrait;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> DisplayedTexture;

	UPROPERTY(Transient)
	TObjectPtr<UImage> FaceImage;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LeadingNumber;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TrailingNumber;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Title;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LevelLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PortraitStatus;

	FText StatusDetails;
	FVector2D Tilt = FVector2D::ZeroVector;
	bool bFoilPreview = false;
	bool bHoverable = false;
};
