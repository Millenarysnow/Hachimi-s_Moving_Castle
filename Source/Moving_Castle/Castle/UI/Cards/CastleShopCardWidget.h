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

/** 卡面短点（位移小于阈值松手）时通知所属商品槽执行购买。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCastleShopCardClicked, UCastleShopCardWidget&);
/** 卡面鼠标悬停状态变化时通知父级展示或隐藏战术详情浮卡。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCastleShopCardHovered, UCastleShopCardWidget&, bool);

/** 商店建筑牌管理自己的旋转预览与短点购买分流；卡位、焦点和交易仍由商店管理。 */
UCLASS()
class MOVING_CASTLE_API UCastleShopCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UCastleShopCardWidget(const FObjectInitializer& ObjectInitializer);

	/** 用商品原始描述设置色号与取景；错误以明确的卡面消息及日志呈现，不替换商品。 */
	bool Configure(UCastleShopWidget& InShop, const FCastleBuildingDescriptor& Descriptor, const ACastleBoard* Board,
		bool bSold);

	/** 场上建筑使用同一牌面及摄影服务，独立展示时没有商店的购买和动态摄影权限。 */
	bool ConfigureStandalone(const FCastleBuildingDescriptor& Descriptor, const ACastleBoard* Board);

	/** 普通与箔光是同一张牌的表现状态，不触发重新成像。 */
	void SetFoilPreview(bool bEnabled);

	/** 关闭、重建或换牌时直接回正并归还这一张牌的鼠标捕获。 */
	void CancelInspection();

	/** 返回当前绑定的建筑卡牌描述，只读供悬停详情与测试核对。 */
	const FCastleBuildingDescriptor& GetDescriptor() const { return DisplayedDescriptor; }

	/** 返回当前是否处于已售罄态（已售罄禁止拖转与短点购买）。 */
	bool IsSold() const { return bIsSold; }

	/** 短点卡面触发购买事件（方案 A：位移未超过拖拽阈值且在卡面内松手）。 */
	FCastleShopCardClicked OnCardClicked;

	/** 悬停进入/离开通知，供商店显示左侧建筑机制浮卡。 */
	FCastleShopCardHovered OnCardHovered;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	/** 记录抓牌的指针、未投影局部起点以及是否已越过短点购买的拖拽阈值。 */
	struct FCardGrab
	{
		int32 UserIndex;
		uint32 PointerIndex;
		FVector2D Origin;
		FVector2D InitialOffset;
		bool bExceededDragThreshold = false;

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
	bool ConfigurePresentation(UCastleShopWidget* InShop, const FCastleBuildingDescriptor& Descriptor,
		const ACastleBoard* Board, bool bSold);

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
	FCastleBuildingDescriptor DisplayedDescriptor;
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
	bool bIsSold = false;
};
