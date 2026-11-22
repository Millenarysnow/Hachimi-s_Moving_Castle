// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CastleShopWBPBuilderLibrary.generated.h"

class UButton;
class UImage;
class UPanelWidget;
class UProgressBar;
class UTexture2D;
class UWidget;

/**
 * 仅供编辑器离线 Python 构建商店 WBP 使用的最小辅助接口。
 * 解决 UE5.8 Python 未直接暴露 UWidgetTree::RootWidget、bIsVariable 与 FSlateBrush::ImageSize 写权限的问题，
 * 不包含任何运行时逻辑，也不会回退或修补资产配置错误。
 */
UCLASS()
class MOVING_CASTLE_API UCastleShopWBPBuilderLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 在目标 WidgetBlueprint 的 WidgetTree 中构造指定控件，并挂为根节点或指定父面板子节点。 */
	UFUNCTION(BlueprintCallable, Category = "MovingCastle|EditorUI")
	static UWidget* ConstructAndAttachWidget(UObject* WidgetBlueprintObject, TSubclassOf<UWidget> WidgetClass,
		FName WidgetName, UPanelWidget* ParentPanel, bool bIsVariable);

	/** 配置 UImage 的笔刷资源、精确尺寸、九宫格边距与染色；Texture 为空时配置纯色笔刷。 */
	UFUNCTION(BlueprintCallable, Category = "MovingCastle|EditorUI")
	static void ConfigureImageBrush(UImage* Image, UTexture2D* Texture, FVector2D ImageSize,
		bool bUseBox9Slice, FMargin BoxMargin, FLinearColor TintColor);

	/** 将按钮样式设为全透明（由外层 WidgetSwitcher 承载切图）并按需配置键盘焦点。 */
	UFUNCTION(BlueprintCallable, Category = "MovingCastle|EditorUI")
	static void ConfigureTransparentButton(UButton* Button, bool bIsFocusable);

	/** 配置进度条的背景与填充纯色笔刷，保持与货币战争面板配色统一。 */
	UFUNCTION(BlueprintCallable, Category = "MovingCastle|EditorUI")
	static void ConfigureProgressBarColors(UProgressBar* ProgressBar, FLinearColor BackgroundColor, FLinearColor FillColor);

	/** 在编辑器中编译指定 WidgetBlueprint；对象无效或非编辑环境时记录错误并返回 false。 */
	UFUNCTION(BlueprintCallable, Category = "MovingCastle|EditorUI")
	static bool CompileWidgetBlueprintAsset(UObject* WidgetBlueprintObject);
};
