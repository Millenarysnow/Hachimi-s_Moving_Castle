// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CastleHUD.generated.h"

class UCastleBackpackWidget;
class UCastleShopWidget;

/**
 * 移动城堡的最小中心指针 HUD。
 *
 * 绘制屏幕中心的四段准星，并创建当前阶段的背包卡片 UI；网格面本身由世界
 * 空间的 ACastleGridCursor 显示，二者共同让玩家能明确看到当前视线目标。
 */
UCLASS()
class MOVING_CASTLE_API ACastleHUD : public AHUD
{
	GENERATED_BODY()

public:
	/** 创建背包 UI；当前阶段 UI 仅展示，不接收鼠标输入。 */
	virtual void BeginPlay() override;

	/** 移除运行时创建的背包 UI。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 在屏幕中心绘制轻量准星。 */
	virtual void DrawHUD() override;

protected:
	/** 当前 HUD 持有的背包展示控件。 */
	UPROPERTY(Transient)
	TObjectPtr<UCastleBackpackWidget> BackpackWidget;

	/** 商店覆盖层独立控制自己的开关，复用玩家的账户和商店逻辑组件。 */
	UPROPERTY(Transient)
	TObjectPtr<UCastleShopWidget> ShopWidget;

	/** 准星颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "中心指针")
	FLinearColor CrosshairColor = FLinearColor(0.65f, 0.92f, 1.0f, 0.95f);

	/** 准星每段线的长度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "中心指针", meta = (ClampMin = "1.0"))
	float CrosshairLength = 8.0f;

	/** 准星中心到四段线的间隔。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "中心指针", meta = (ClampMin = "0.0"))
	float CrosshairGap = 4.0f;

	/** 准星线宽。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "中心指针", meta = (ClampMin = "0.5"))
	float CrosshairThickness = 2.0f;
};
