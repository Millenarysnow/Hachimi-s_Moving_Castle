// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleHUD.h"

#include "Blueprint/UserWidget.h"
#include "CastleBackpackWidget.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"

void ACastleHUD::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* OwningPlayerController = GetOwningPlayerController();
	if (OwningPlayerController == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("城堡 HUD 创建背包 UI 失败：没有对应的 PlayerController。"));
		return;
	}

	BackpackWidget = CreateWidget<UCastleBackpackWidget>(OwningPlayerController, UCastleBackpackWidget::StaticClass());
	if (BackpackWidget == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("城堡 HUD 创建背包 UI 失败：CreateWidget 返回空指针。"));
		return;
	}

	// 先加入屏幕，再设置底部居中位置；具体尺寸由背包控件根据容量提供。
	BackpackWidget->AddToViewport(20);
	BackpackWidget->SetAnchorsInViewport(FAnchors(0.5f, 1.0f));
	BackpackWidget->SetAlignmentInViewport(FVector2D(0.5f, 1.0f));
	BackpackWidget->SetPositionInViewport(FVector2D(0.0f, -24.0f), true);
}

void ACastleHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BackpackWidget != nullptr)
	{
		BackpackWidget->RemoveFromParent();
		BackpackWidget = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

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
