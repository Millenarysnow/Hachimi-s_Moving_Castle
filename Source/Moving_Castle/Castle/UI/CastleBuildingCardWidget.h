// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CastleGameplayTypes.h"
#include "CastleBuildingCardWidget.generated.h"

class UBorder;
class UTextBlock;
class UCastleBuildingPreviewWidget;

/**
 * 背包中的单张建筑卡片。
 *
 * 卡片只接收建筑描述并负责显示，不负责从背包移除卡牌，也不处理鼠标输入。
 * 这样后续加入拖放时，数据操作仍可以集中在背包和棋盘接口中。
 */
UCLASS()
class MOVING_CASTLE_API UCastleBuildingCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 配置一张有效建筑卡片。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包卡片")
	void SetBuildingDescriptor(const FCastleBuildingDescriptor& InDescriptor);

	/** 将卡片切换为空槽位显示。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包卡片")
	void SetEmptySlot();

	/** 返回当前卡片是否绑定了建筑描述。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|背包卡片")
	bool HasBuildingDescriptor() const
	{
		return bHasDescriptor;
	}

protected:
	/** 在 Slate 根控件取走前创建卡片的纯 C++ UMG 布局。 */
	virtual void NativeOnInitialized() override;

	/** 执行每次加入屏幕时的视觉同步。 */
	virtual void NativeConstruct() override;

	/** 创建并返回颜色对应的边框色。 */
	static FLinearColor GetCardColor(ECastleTileColor Color);

	/** 返回建筑类别的中文显示名。 */
	static FText GetBuildingTypeText(ECastleBuildingType BuildingType);

	/** 返回建筑颜色的中文显示名。 */
	static FText GetBuildingColorText(ECastleTileColor Color);

	/** 重新写入标题、等级和预览数据。 */
	void RefreshVisuals();

	/** 当前卡片的建筑描述。 */
	UPROPERTY(Transient)
	FCastleBuildingDescriptor Descriptor;

	/** 是否是有效建筑卡片，false 表示容量槽位尚未存入卡牌。 */
	UPROPERTY(Transient)
	bool bHasDescriptor = false;

	/** 卡片外框，用建筑颜色做低透明度强调。 */
	UPROPERTY(Transient)
	TObjectPtr<UBorder> CardBorder;

	/** 卡片顶部的类型和数字文本。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;

	/** 卡片底部的等级和颜色文本。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DetailText;

	/** 卡片中央的三维线框预览。 */
	UPROPERTY(Transient)
	TObjectPtr<UCastleBuildingPreviewWidget> PreviewWidget;

	/** 防止布局重复创建。 */
	bool bWidgetTreeBuilt = false;
};
