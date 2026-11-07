// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleHUD.h"

#include "Engine/Canvas.h"

void ACastleHUD::DrawHUD()
{
	Super::DrawHUD();

	if (Canvas == nullptr)
	{
		return;
	}

	const float CenterX = Canvas->SizeX * 0.5f;
	const float CenterY = Canvas->SizeY * 0.5f;
	const float Inner = CrosshairGap;
	const float Outer = CrosshairGap + CrosshairLength;

	// 四段线比单个十字更容易在浅色地面和深色建筑上保持清晰。
	Canvas->K2_DrawLine(FVector2D(CenterX - Outer, CenterY), FVector2D(CenterX - Inner, CenterY), CrosshairThickness, CrosshairColor);
	Canvas->K2_DrawLine(FVector2D(CenterX + Inner, CenterY), FVector2D(CenterX + Outer, CenterY), CrosshairThickness, CrosshairColor);
	Canvas->K2_DrawLine(FVector2D(CenterX, CenterY - Outer), FVector2D(CenterX, CenterY - Inner), CrosshairThickness, CrosshairColor);
	Canvas->K2_DrawLine(FVector2D(CenterX, CenterY + Inner), FVector2D(CenterX, CenterY + Outer), CrosshairThickness, CrosshairColor);
}
