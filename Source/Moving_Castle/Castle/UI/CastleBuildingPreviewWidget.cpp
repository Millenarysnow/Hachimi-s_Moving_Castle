// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingPreviewWidget.h"

#include "CastleBuildingPreviewSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	PreviewMaterial = UMaterialInstanceDynamic::Create(Subsystem->GetOutlineMaterial(), this);
	if (PreviewMaterial == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡片预览失败：无法创建描边材质实例。"));
		return;
	}
	// 与旧版线框卡片使用相同的高亮色；正式模型材质只用于场上表现，不填充预览内部。
	const FLinearColor Colors[] = {
		FLinearColor(0.72f, 0.82f, 0.90f), FLinearColor(0.98f, 0.18f, 0.18f),
		FLinearColor(0.16f, 0.50f, 1.0f), FLinearColor(0.16f, 0.92f, 0.36f),
		FLinearColor(1.0f, 0.78f, 0.12f), FLinearColor(0.76f, 0.30f, 1.0f)
	};
	PreviewMaterial->SetTextureParameterValue(TEXT("PreviewTexture"), Texture);
	PreviewMaterial->SetVectorParameterValue(TEXT("PreviewTint"), Colors[static_cast<int32>(Descriptor.Color)]);
	PreviewBrush.SetResourceObject(PreviewMaterial);
	PreviewBrush.ImageSize = FVector2D(256.0f, 256.0f);
	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	bHasDescriptor = true;
	InvalidateLayoutAndVolatility();
}

void UCastleBuildingPreviewWidget::ClearPreview()
{
	Descriptor = FCastleBuildingDescriptor();
	PreviewBrush.SetResourceObject(nullptr);
	PreviewMaterial = nullptr;
	bHasDescriptor = false;
	InvalidateLayoutAndVolatility();
}

void UCastleBuildingPreviewWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bHasDescriptor && GetWorld() != nullptr)
	{
		if (UCastleBuildingPreviewSubsystem* Subsystem = GetWorld()->GetSubsystem<UCastleBuildingPreviewSubsystem>())
		{
			Subsystem->MarkPreviewUsed(Descriptor);
		}
		// 旋转纹理本身更新后也要使 Slate 重绘，不能保留旧卡片的绘制缓存。
		Invalidate(EInvalidateWidgetReason::Paint);
	}
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
