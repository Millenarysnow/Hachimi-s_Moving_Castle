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
	/** 货币战争风格房间商店的正式 WBP；缺失或绑定错误时直接报错拒绝挂载。 */
	constexpr TCHAR RoomShopAssetPath[] = TEXT("/Game/MovingCastle/UI/Shop/WBP_RoomShop");
}

ACastleHUD::ACastleHUD()
{
	static const ConstructorHelpers::FClassFinder<UCastleBuildBarWidget> BuildBarClass(BuildBarAssetPath);
	BuildBarWidgetClass = BuildBarClass.Class;
	static const ConstructorHelpers::FClassFinder<UCastleEconomyHUDWidget> EconomyHUDClass(EconomyHUDAssetPath);
	EconomyWidgetClass = EconomyHUDClass.Class;
	static const ConstructorHelpers::FClassFinder<UCastleShopWidget> RoomShopClass(RoomShopAssetPath);
	ShopWidgetClass = RoomShopClass.Class;
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

	// 商店使用正式 WBP 资产，缺失类或必需 BindWidget 时直接报错，绝不回退旧布局。
	if (!IsValid(ShopWidgetClass.Get()))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("城堡 HUD 无法找到有效的房间商店 WBP：%s。"), RoomShopAssetPath);
		return;
	}
	ShopWidget = CreateWidget<UCastleShopWidget>(PlayerOwner, ShopWidgetClass);
	if (!IsValid(ShopWidget) || !ShopWidget->ValidatePresentation())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("城堡 HUD 创建或校验房间商店 WBP 失败：%s。"), RoomShopAssetPath);
		ShopWidget = nullptr;
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

	if (ACastlePlayerController* CastleController = Cast<ACastlePlayerController>(PlayerOwner))
	{
		if (IsValid(CastleController->GetShop()))
		{
			CastleController->GetShop()->OnShopChanged.AddUniqueDynamic(this, &ACastleHUD::HandleShopVisibilityChanged);
			HandleShopVisibilityChanged();
		}
	}
}

void ACastleHUD::HandleShopVisibilityChanged()
{
	if (!IsValid(EconomyWidget))
	{
		return;
	}
	const ACastlePlayerController* CastleController = Cast<ACastlePlayerController>(PlayerOwner);
	const bool bShopOpen = IsValid(CastleController) && IsValid(CastleController->GetShop())
		&& CastleController->GetShop()->IsShopOpen();
	// 透景商店顶部自带金币栏，开店期间隐藏独立金币 HUD 避免重影。
	EconomyWidget->SetVisibility(bShopOpen ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void ACastleHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACastlePlayerController* CastleController = Cast<ACastlePlayerController>(PlayerOwner))
	{
		if (IsValid(CastleController->GetShop()))
		{
			CastleController->GetShop()->OnShopChanged.RemoveDynamic(this, &ACastleHUD::HandleShopVisibilityChanged);
		}
	}
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
	if (Controller == nullptr || !Controller->ShouldDrawCenterCrosshair())
	{
		// 控制器统一裁定模式与模态状态。设计模式由系统指针表达瞄准位置。
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
