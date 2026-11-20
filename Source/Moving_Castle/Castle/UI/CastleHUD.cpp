// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleHUD.h"

#include "Blueprint/UserWidget.h"
#include "BuildBar/CastleBuildBarWidget.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "CastleShopWidget.h"
#include "Economy/CastleEconomyHUDWidget.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** HUD 唯一使用的建造栏资产；名字和位置供出错时明确定位。 */
	constexpr TCHAR BuildBarAssetPath[] = TEXT("/Game/MovingCastle/UI/BuildBar/WBP_BuildBar");
	/** 金币与快捷键提示的正式 WBP；保留硬引用，保证与 HUD 一同烘焙。 */
	constexpr TCHAR EconomyHUDAssetPath[] = TEXT("/Game/MovingCastle/UI/Economy/WBP_EconomyHUD");
}

ACastleHUD::ACastleHUD()
{
	static const ConstructorHelpers::FClassFinder<UCastleBuildBarWidget> BuildBarClass(BuildBarAssetPath);
	BuildBarWidgetClass = BuildBarClass.Class;
	static const ConstructorHelpers::FClassFinder<UCastleEconomyHUDWidget> EconomyHUDClass(EconomyHUDAssetPath);
	EconomyWidgetClass = EconomyHUDClass.Class;
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

	if (!IsValid(EconomyWidgetClass.Get()))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("城堡 HUD 无法找到有效的金币 WBP：%s。"), EconomyHUDAssetPath);
		return;
	}
	UCastleEconomyHUDWidget* NewEconomyWidget = CreateWidget<UCastleEconomyHUDWidget>(PlayerOwner, EconomyWidgetClass);
	if (!IsValid(NewEconomyWidget))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("城堡 HUD 无法实例化金币 WBP：%s。"), EconomyHUDAssetPath);
		return;
	}
	if (!NewEconomyWidget->BindToOwningEconomy())
	{
		// 错误已由金币 WBP 记录；不能用旧的 Canvas 文字或占位余额替代。
		return;
	}

	// 在商店和导演模态之下展示；提示是只读标签，不截获准星放置的鼠标输入。
	EconomyWidget = NewEconomyWidget;
	EconomyWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	EconomyWidget->AddToViewport(20);
}

void ACastleHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 移除视口上的金币 WBP，触发 NativeDestruct 清理账户事件订阅。
	if (EconomyWidget != nullptr)
	{
		EconomyWidget->RemoveFromParent();
		EconomyWidget = nullptr;
	}
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
		// 金币与提示由 WBP 接管；Canvas 保留独立的中心准星和既有可见条件。
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
