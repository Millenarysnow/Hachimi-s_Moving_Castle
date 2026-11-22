// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CastleHUD.generated.h"

class UCastleBuildBarWidget;
class UCastleEconomyHUDWidget;
class UCastleShopWidget;

/**
 * 移动城堡的最小中心指针 HUD。
 *
 * 绘制屏幕中心的四段准星，并挂载金币、建造栏和商店；网格面本身由世界
 * 空间的 ACastleGridCursor 显示，二者共同让玩家能明确看到当前视线目标。
 */
UCLASS()
class MOVING_CASTLE_API ACastleHUD : public AHUD
{
	GENERATED_BODY()

public:
	/** 将正式建造栏与金币 WBP 作为 HUD 的打包依赖。 */
	ACastleHUD();

	/** 创建金币、建造栏与商店；建造栏保持键盘选牌和中心指针放置。 */
	virtual void BeginPlay() override;

	/** 移除运行时挂载的界面，不在切换关卡后保留金币订阅。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 在屏幕中心绘制轻量准星。 */
	virtual void DrawHUD() override;

protected:
	/** 构造时绑定正式 WBP 类；没有原生替代布局。 */
	UPROPERTY()
	TSubclassOf<UCastleBuildBarWidget> BuildBarWidgetClass;

	/** 随 HUD 一起销毁的建造栏实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UCastleBuildBarWidget> BuildBarWidget;

	/** 正式金币 WBP 及其实例，金币始终读当前玩家控制器中的账户。 */
	UPROPERTY()
	TSubclassOf<UCastleEconomyHUDWidget> EconomyWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UCastleEconomyHUDWidget> EconomyWidget;

	/** 商店 WBP 类与覆盖层实例；加载失败或绑定无效直接报错，不回退动态拼树。 */
	UPROPERTY()
	TSubclassOf<UCastleShopWidget> ShopWidgetClass;

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

private:
	/** 商店打开时隐藏左上角独立金币条，关闭时恢复，避免与商店顶部金币栏重影。 */
	UFUNCTION()
	void HandleShopVisibilityChanged();
};
