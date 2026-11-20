// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleHUD.h"

#include "Blueprint/UserWidget.h"
#include "BuildBar/CastleBuildBarWidget.h"
#include "CastlePlayerController.h"
#include "CastleEconomyComponent.h"
#include "CastleShopComponent.h"
#include "CastleShopWidget.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** HUD 唯一使用的建造栏资产；名字和位置供出错时明确定位。 */
	constexpr TCHAR BuildBarAssetPath[] = TEXT("/Game/MovingCastle/UI/BuildBar/WBP_BuildBar");
}

ACastleHUD::ACastleHUD()
{
	static const ConstructorHelpers::FClassFinder<UCastleBuildBarWidget> BuildBarClass(BuildBarAssetPath);
	BuildBarWidgetClass = BuildBarClass.Class;
}

void ACastleHUD::BeginPlay()
{
	Super::BeginPlay();

	// 使用引擎已绑定到 HUD 的控制器，而不是跨本地玩家搜索控制器。
	if (!IsValid(PlayerOwner))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("城堡 HUD %s 无法加载建造栏 %s：PlayerOwner 为空。"),
			*GetPathName(), BuildBarAssetPath);
		return;
	}

	// 商店不依赖建造栏的显示状态，保持原有高层级及开关行为。
	ShopWidget = CreateWidget<UCastleShopWidget>(PlayerOwner);
	if (ShopWidget == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("城堡 HUD 创建商店 UI 失败。"));
		return;
	}
	ShopWidget->AddToViewport(40);

	if (BuildBarWidgetClass == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("城堡 HUD 无法找到有效的建造栏 WBP：%s。"), BuildBarAssetPath);
		return;
	}
	BuildBarWidget = CreateWidget<UCastleBuildBarWidget>(PlayerOwner, BuildBarWidgetClass);
	if (BuildBarWidget == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("城堡 HUD 无法实例化建造栏 WBP：%s。"), BuildBarAssetPath);
		return;
	}

	// 底部居中和完整等比缩放由 WBP 设计器的 Canvas / ScaleBox 负责。
	BuildBarWidget->AddToViewport(20);
}

void ACastleHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ShopWidget != nullptr)
	{
		ShopWidget->RemoveFromParent();
		ShopWidget = nullptr;
	}
	if (BuildBarWidget != nullptr)
	{
		BuildBarWidget->RemoveFromParent();
		BuildBarWidget = nullptr;
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
		FText EditReason;
		if (Controller->GetShop()->IsShopOpen() || !Controller->IsDirectorEditingAllowed(EditReason))
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
