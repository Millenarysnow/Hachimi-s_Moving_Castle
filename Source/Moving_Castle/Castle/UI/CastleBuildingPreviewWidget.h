// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CastleGameplayTypes.h"
#include "Styling/SlateBrush.h"
#include "CastleBuildingPreviewWidget.generated.h"

/** 背包和商店共用的建筑模型预览；只绘制缓存纹理，不自行生成虚构模型或修改卡牌。 */
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
	/** 纹理以正方形居中绘制，避免卡片的竖向布局拉伸建筑比例。 */
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY(Transient)
	FCastleBuildingDescriptor Descriptor;

	/** Slate Brush 持有缓存纹理，背包重排时不重新捕获相同模型。 */
	UPROPERTY(Transient)
	FSlateBrush PreviewBrush;

	/** 当前卡片是否已经绑定可绘制的预览纹理。 */
	bool bHasDescriptor = false;
};
