// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleShopCardDetailWidget.h"

#include "Castle/UI/Common/CastleHUDControls.h"
#include "CastleBuildingDefinition.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Moving_Castle.h"

namespace
{
	/** 统一映射五种正式玩法色；灰色或未知枚举视为非法配置。 */
	bool TryResolveDetailGameplayColor(const ECastleTileColor Color, FLinearColor& OutLinearColor, FString& OutColorName)
	{
		switch (Color)
		{
		case ECastleTileColor::Red:
			OutLinearColor = FLinearColor(FColor::FromHex(TEXT("D54548")));
			OutColorName = TEXT("红色");
			return true;
		case ECastleTileColor::Blue:
			OutLinearColor = FLinearColor(FColor::FromHex(TEXT("287ADB")));
			OutColorName = TEXT("蓝色");
			return true;
		case ECastleTileColor::Green:
			OutLinearColor = FLinearColor(FColor::FromHex(TEXT("36A86F")));
			OutColorName = TEXT("绿色");
			return true;
		case ECastleTileColor::Yellow:
			OutLinearColor = FLinearColor(FColor::FromHex(TEXT("E4AB31")));
			OutColorName = TEXT("黄色");
			return true;
		case ECastleTileColor::Purple:
			OutLinearColor = FLinearColor(FColor::FromHex(TEXT("9658DC")));
			OutColorName = TEXT("紫色");
			return true;
		default:
			return false;
		}
	}

	/** 读取每种建筑类型的简明机制说明，与《色号堡垒》策划案保持一致。 */
	bool TryResolveBuildingMechanismSummary(const ECastleBuildingType BuildingType, FText& OutSummary)
	{
		switch (BuildingType)
		{
		case ECastleBuildingType::NormalRoom:
			OutSummary = NSLOCTEXT("CastleShopDetail", "NormalRoom",
				"基础构筑方块，固定 1 级且不合成。\n同色融合贯通，同号异色生成门与楼梯，用于串联立体动线与垫高箭塔。");
			return true;
		case ECastleBuildingType::ArcherTower:
			OutSummary = NSLOCTEXT("CastleShopDetail", "ArcherTower",
				"人员经过时按总箭数连射。\n等级越高越能向下累加垂直堆叠房间数字，适合同色纵向复式叠高。");
			return true;
		case ECastleBuildingType::CannonTower:
			OutSummary = NSLOCTEXT("CastleShopDetail", "CannonTower",
				"人员经过本体或收编的同号弹药库时轰击一次，伤害等于本体加弹药库数字总和。\n升级扩展三维弹药库范围。");
			return true;
		case ECastleBuildingType::Farm:
			OutSummary = NSLOCTEXT("CastleShopDetail", "Farm",
				"人员经过时产出金币。\n升级后可跨格联动相连同色农田一并触发，Lv5 还能为人员附带攻击加成。");
			return true;
		case ECastleBuildingType::ShieldTower:
			OutSummary = NSLOCTEXT("CastleShopDetail", "ShieldTower",
				"人员经过展开护盾吸引附近敌军。\n升级累加相连同色/任意塔数字作为抵挡次数，Lv3 起附加反伤。");
			return true;
		case ECastleBuildingType::BeeHive:
			OutSummary = NSLOCTEXT("CastleShopDetail", "BeeHive",
				"人员经过派出蜂群锁敌高血量目标。\n升级解锁集群提速、顺号备用巢接力出蜂与击杀分裂孵化。");
			return true;
		case ECastleBuildingType::BalloonTower:
			OutSummary = NSLOCTEXT("CastleShopDetail", "BalloonTower",
				"人员经过放飞诱敌气球，破损或点击引爆空投货箱造成范围伤害与减速。\n升级附带色号增益与邻塔协击。");
			return true;
		default:
			return false;
		}
	}
}

bool UCastleShopCardDetailWidget::ValidatePresentation() const
{
	if (!CastleHUDControls::HasValidTextureBrush(PanelBg))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店详情浮卡 WBP %s 缺少 PanelBg 或其贴图/尺寸无效。"), *GetClass()->GetPathName());
		return false;
	}
	if (!IsValid(ColorAccent) || !IsValid(BuildingNameText) || !IsValid(CardIdentityText)
		|| !IsValid(MechanismText) || !IsValid(SynergyHintText))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店详情浮卡 WBP %s 缺少必需控件绑定。"), *GetClass()->GetPathName());
		return false;
	}
	return true;
}

bool UCastleShopCardDetailWidget::ShowCardDetail(const FCastleBuildingDescriptor& Descriptor, const bool bIsBenchCard,
	const int32 Price, const int32 MatchingInBackpackCount)
{
	FText ValidateReason;
	if (!ValidatePresentation() || !Descriptor.Validate(ValidateReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店详情浮卡 %s 展示失败：%s"),
			*GetClass()->GetPathName(), *ValidateReason.ToString());
		HideDetail();
		return false;
	}

	FLinearColor GameplayColor;
	FString ColorName;
	if (!TryResolveDetailGameplayColor(Descriptor.Color, GameplayColor, ColorName))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店详情浮卡 %s 收到非法玩法色 %d。"),
			*GetClass()->GetPathName(), static_cast<int32>(Descriptor.Color));
		HideDetail();
		return false;
	}

	FText MechanismSummary;
	if (!TryResolveBuildingMechanismSummary(Descriptor.Definition->BuildingType, MechanismSummary))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店详情浮卡 %s 收到未知建筑类型 %d（资产 %s）。"),
			*GetClass()->GetPathName(), static_cast<int32>(Descriptor.Definition->BuildingType),
			*GetPathNameSafe(Descriptor.Definition.Get()));
		HideDetail();
		return false;
	}

	ColorAccent->SetColorAndOpacity(GameplayColor);
	BuildingNameText->SetText(Descriptor.Definition->DisplayName);
	CardIdentityText->SetText(FText::FromString(FString::Printf(TEXT("%s  ·  数字 %d  ·  Lv.%d"),
		*ColorName, Descriptor.Number, Descriptor.Level)));
	MechanismText->SetText(MechanismSummary);

	FString SynergyText;
	if (bIsBenchCard)
	{
		SynergyText = FString::Printf(TEXT("出售收益：+%d 金币\n拖拽至下方回收区或右键立即出售"), Price);
	}
	else if (Descriptor.Definition->BuildingType == ECastleBuildingType::NormalRoom)
	{
		SynergyText = FString::Printf(TEXT("购买价格：%d 金币\n短点卡面或点击价格条直接购买，按住拖动可 3D 赏牌"), Price);
	}
	else if (MatchingInBackpackCount > 0)
	{
		SynergyText = FString::Printf(TEXT("购买价格：%d 金币\n背包已有 %d 张同色同号牌，放置紧贴可合成升级！"),
			Price, MatchingInBackpackCount);
	}
	else
	{
		SynergyText = FString::Printf(TEXT("购买价格：%d 金币\n短点卡面或点击价格条购买，按住拖动可 3D 赏牌"), Price);
	}
	SynergyHintText->SetText(FText::FromString(SynergyText));
	SetVisibility(ESlateVisibility::HitTestInvisible);
	return true;
}

void UCastleShopCardDetailWidget::HideDetail()
{
	SetVisibility(ESlateVisibility::Collapsed);
}
