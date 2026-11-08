// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleHUD.h"

#include "Blueprint/UserWidget.h"
#include "CastleBackpackWidget.h"
#include "CastlePlayerController.h"
#include "CastleEconomyComponent.h"
#include "CastleShopComponent.h"
#include "CastleShopWidget.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "Moving_Castle.h"

void ACastleHUD::BeginPlay()
{
	Super::BeginPlay();

	// HUD 的 PlayerOwner 由 GameMode 在创建 HUD 时直接设置，比再次从世界查找
	// PlayerController 更稳定，尤其是在 PIE 和独立游戏窗口的初始化早期。
	APlayerController* OwningPlayerController = PlayerOwner;
	if (OwningPlayerController == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("城堡 HUD 创建背包 UI 失败：PlayerOwner 为空。"));
		return;
	}

	UE_LOG(LogMoving_Castle, Log, TEXT("城堡 HUD 开始创建背包 UI，PlayerController=%s。"), *GetNameSafe(OwningPlayerController));

	BackpackWidget = CreateWidget<UCastleBackpackWidget>(OwningPlayerController, UCastleBackpackWidget::StaticClass());
	if (BackpackWidget == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("城堡 HUD 创建背包 UI 失败：CreateWidget 返回空指针。"));
		return;
	}

	// 先明确设置可见性，再加入全局视口；当前只有单人视口，不需要额外的本地玩家层级。
	BackpackWidget->SetVisibility(ESlateVisibility::Visible);
	BackpackWidget->AddToViewport(20);
		// 直接设置 UserWidget 的视口槽位，而不是依赖背包内部的根 CanvasPanel。
		// 这样固定尺寸根节点会以自身期望尺寸贴在屏幕底部中央。
	// UE 5.8 的尺寸和位置接口都会把锚点重设到左上角，必须先写尺寸和位置，
	// 最后再设置底部锚点；否则整个背包会被放到屏幕左上方之外。
	BackpackWidget->SetDesiredSizeInViewport(FVector2D(1584.0f, 268.0f));
	BackpackWidget->SetPositionInViewport(FVector2D(0.0f, -24.0f));
	BackpackWidget->SetAnchorsInViewport(FAnchors(0.5f, 1.0f));
	BackpackWidget->SetAlignmentInViewport(FVector2D(0.5f, 1.0f));
	UE_LOG(LogMoving_Castle, Log, TEXT("城堡 HUD 背包 UI 已加入玩家屏幕，InViewport=%s。"), BackpackWidget->IsInViewport() ? TEXT("true") : TEXT("false"));

	// 商店使用独立全屏层级；是否显示由商店组件事件控制，HUD 不保存第二份商店状态。
	ShopWidget = CreateWidget<UCastleShopWidget>(OwningPlayerController);
	if (ShopWidget == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("城堡 HUD 创建商店 UI 失败。"));
		return;
	}
	ShopWidget->AddToViewport(40);
}

void ACastleHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ShopWidget != nullptr)
	{
		ShopWidget->RemoveFromParent();
		ShopWidget = nullptr;
	}
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
	const ACastlePlayerController* Controller = Cast<ACastlePlayerController>(PlayerOwner);
	if (Controller != nullptr)
	{
		// 关闭商店时仍显示余额和入口提示，玩家不必打开商店才能确认收支结果。
		DrawText(FString::Printf(TEXT("Gold: %d   |   [B] Shop   |   [1-9,0] Backpack"), Controller->GetEconomy()->GetGold()),
			FLinearColor(1.0f, 0.82f, 0.32f), 24.0f, 24.0f, nullptr, 1.25f);
		if (Controller->GetShop()->IsShopOpen())
		{
			return;
		}
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
