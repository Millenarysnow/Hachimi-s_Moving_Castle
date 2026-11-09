// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingPreviewWidget.h"

#include "CastleBuildingPreviewSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Moving_Castle.h"
#include "Rendering/DrawElements.h"

void UCastleBuildingPreviewWidget::SetBuildingDescriptor(const FCastleBuildingDescriptor& InDescriptor)
{
	Descriptor = InDescriptor;
	bHasDescriptor = false;
	PreviewBrush.SetResourceObject(nullptr);
	FText Reason;
	if (!Descriptor.Validate(Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡片预览失败：%s"), *Reason.ToString());
		return;
	}
	UCastleBuildingPreviewSubsystem* Subsystem = GetWorld() != nullptr ? GetWorld()->GetSubsystem<UCastleBuildingPreviewSubsystem>() : nullptr;
	if (Subsystem == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡片预览失败：当前世界没有预览子系统。"));
		return;
	}
	UTextureRenderTarget2D* Texture = Subsystem->GetPreview(Descriptor, Reason);
	if (Texture == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡片预览失败：%s"), *Reason.ToString());
		return;
	}
	PreviewBrush.SetResourceObject(Texture);
	PreviewBrush.ImageSize = FVector2D(256.0f, 256.0f);
	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	bHasDescriptor = true;
	InvalidateLayoutAndVolatility();
}

void UCastleBuildingPreviewWidget::ClearPreview()
{
	Descriptor = FCastleBuildingDescriptor();
	PreviewBrush.SetResourceObject(nullptr);
	bHasDescriptor = false;
	InvalidateLayoutAndVolatility();
}

int32 UCastleBuildingPreviewWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bHasDescriptor) { return BaseLayer; }
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float Edge = FMath::Min(Size.X, Size.Y);
	const FVector2D Offset = (Size - FVector2D(Edge, Edge)) * 0.5f;
	const FPaintGeometry Geometry = AllottedGeometry.ToPaintGeometry(FVector2D(Edge, Edge), FSlateLayoutTransform(Offset));
	FSlateDrawElement::MakeBox(OutDrawElements, BaseLayer + 1, Geometry, &PreviewBrush,
		ESlateDrawEffect::None, InWidgetStyle.GetColorAndOpacityTint());
	return BaseLayer + 1;
}
