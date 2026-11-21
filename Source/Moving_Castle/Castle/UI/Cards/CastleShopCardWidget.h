// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleGameplayTypes.h"
#include "Components/RetainerBox.h"
#include "CoreMinimal.h"
#include "CastleShopCardWidget.generated.h"

class ACastleBoard;
class UCastleCardPortrait;
class UCastleCardPortraitSubsystem;
class UCastleShopWidget;
class UImage;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTextBlock;
class UTextureRenderTarget2D;

/** 只在牌面变化时重新合成，不让六张静止卡每帧都画入各自的渲染目标。 */
UCLASS()
class UCastleCardFaceRetainer final : public URetainerBox
{
	GENERATED_BODY()

public:
	UCastleCardFaceRetainer(const FObjectInitializer& ObjectInitializer);
};

/** 商店建筑牌管理自己的旋转预览；卡位、焦点和交易仍由商店管理。 */
UCLASS()
class MOVING_CASTLE_API UCastleShopCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCastleShopCardWidget(const FObjectInitializer& ObjectInitializer);

	/** 用商品原始描述设置色号与取景；错误以明确的卡面消息及日志呈现，不替换商品。 */
	bool Configure(UCastleShopWidget& InShop, const FCastleBuildingDescriptor& Descriptor, const ACastleBoard* Board,
		bool bSold);

	/** 普通与箔光是同一张牌的表现状态，不触发重新成像。 */
	void SetFoilPreview(bool bEnabled);

	/** 关闭、重建或换牌时直接回正并归还这一张牌的鼠标捕获。 */
	void CancelInspection();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	/** 记录抓牌的指针和未投影的局部坐标，拖出卡位仍由同一个指针控制。 */
	struct FCardGrab
	{
		int32 UserIndex;
		uint32 PointerIndex;
		FVector2D Origin;
		FVector2D InitialOffset;

		bool Matches(int32 InUserIndex, uint32 InPointerIndex) const
		{
			return UserIndex == InUserIndex && PointerIndex == InPointerIndex;
		}
	};

	void SetInspectionOffset(const FVector2D& NewOffset);
	void FinishInspection();
	void ApplyProjection();
	void RefreshPortrait();
	void ApplyFaceState();
	void RequestFaceRender();
	bool ReportSetupError(const FCastleBuildingDescriptor& Descriptor, const FText& Reason);
	void SetPortraitStatus(const FText& Message, const FText& Details);

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FaceMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PerspectiveMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Perspective;

	UPROPERTY(Transient)
	TObjectPtr<UCastleCardFaceRetainer> FaceRetainer;

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

	/** 商店只授予一张牌动态摄影权；投影和内部效果则都使用本卡的 Tilt。 */
	TWeakObjectPtr<UCastleShopWidget> ShopOwner;
	TOptional<FCardGrab> Grab;
	FVector2D InspectionOffset = FVector2D::ZeroVector;
	FVector2D Tilt = FVector2D::ZeroVector;
	FVector2D ReturnFromOffset = FVector2D::ZeroVector;
	float ReturnElapsed = 0.0f;
	uint64 DisplayedCaptureNumber = 0;
	FText StatusDetails;
	bool bReturning = false;
	bool bFoilPreview = false;
	bool bCanInspect = false;
};
