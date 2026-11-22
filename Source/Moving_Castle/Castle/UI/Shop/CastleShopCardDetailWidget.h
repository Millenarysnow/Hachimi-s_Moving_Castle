// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CastleGameplayTypes.h"
#include "CoreMinimal.h"
#include "CastleShopCardDetailWidget.generated.h"

class UImage;
class UTextBlock;

/**
 * 商店左侧轻量化战术详情浮卡：鼠标悬停商品卡或备战席卡牌时展示建筑机制与同色同号持有计数。
 * 纯只读展示控件，不修改商店、背包或任何玩法状态。
 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleShopCardDetailWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 校验必需 BindWidget 与背景贴图资源；缺失时输出 Error 并返回失败。 */
	bool ValidatePresentation() const;

	/** 展示指定卡牌的建筑名称、玩法色号、机制说明与价格/合成提示；非法描述直接报错隐藏。 */
	bool ShowCardDetail(const FCastleBuildingDescriptor& Descriptor, bool bIsBenchCard, int32 Price,
		int32 MatchingInBackpackCount);

	/** 清除悬停并隐藏详情浮卡。 */
	void HideDetail();

protected:
	/** 浮卡外框贴图（T_Shop_DetailPanel）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> PanelBg;

	/** 顶部玩法色强调条，直接反映该牌的红/蓝/绿/黄/紫玩法色。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ColorAccent;

	/** 建筑中文名称（如箭塔、炮塔、蜂巢塔等）。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BuildingNameText;

	/** 卡牌身份行：玩法色、数字与等级。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CardIdentityText;

	/** 该建筑的核心触发机制与分级成长摘要。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MechanismText;

	/** 只读构筑提示：显示当前背包中同类型、同色、同号张数与交易价格。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SynergyHintText;
};
