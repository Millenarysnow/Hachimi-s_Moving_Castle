// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CastleGameplayTypes.h"
#include "Styling/SlateBrush.h"
#include "CastleBuildingPreviewWidget.generated.h"

class UMaterialInstanceDynamic;

/** 背包和商店共用的缓慢旋转描边预览；形状读取实际模型，颜色读取当前卡牌。 */
UCLASS()
class MOVING_CASTLE_API UCastleBuildingPreviewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 描述引用的 Data Asset 和等级决定预览内容，与棋盘放置完全一致。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包预览")
	void SetBuildingDescriptor(const FCastleBuildingDescriptor& InDescriptor);

	/** 空槽位清除纹理引用，不创建占位卡牌。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包预览")
	void ClearPreview();

	/** 仅在已取得有效模型纹理后返回 true，空槽位和错误配置均不伪造预览。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|背包预览")
	bool HasPreviewDescriptor() const { return bHasDescriptor; }

protected:
	/** 仅可见卡片请求更新共享旋转纹理；原来每秒 14 度的旋转速度保持不变。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 纹理以正方形居中绘制，避免卡片的竖向布局拉伸建筑比例。 */
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY(Transient)
	FCastleBuildingDescriptor Descriptor;

	/** 每张卡片独立应用颜色，底层法线纹理与同定义、同等级的卡片共用。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;

	/** Slate 只绘制带柔光的轮廓，模型内部和周围背景均透明。 */
	UPROPERTY(Transient)
	FSlateBrush PreviewBrush;

	/** 当前卡片是否已经绑定可绘制的预览纹理。 */
	bool bHasDescriptor = false;
};
