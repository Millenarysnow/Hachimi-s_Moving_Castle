#include "Items/CastleBadgeTypes.h"
#include "Moving_Castle.h"

#define LOCTEXT_NAMESPACE "CastleBadgeTypes"

bool FCastleBadgeDefinition::TryMakeStandard(const ECastleBadgeType InType, FCastleBadgeDefinition& OutBadge)
{
	float StandardMagnitude;
	// 这里只声明资产表中的标准数值。实际玩法效果和计数由各自的运行时入口处理。
	switch (InType)
	{
	case ECastleBadgeType::BlueAbacus: StandardMagnitude = 1.0f; break;
	case ECastleBadgeType::WorkshopRefund: StandardMagnitude = 2.0f; break;
	case ECastleBadgeType::BalloonRack:
	case ECastleBadgeType::QuickstringPass: StandardMagnitude = 0.20f; break;
	case ECastleBadgeType::BurstAlarm: StandardMagnitude = 2.0f; break;
	case ECastleBadgeType::EmergencyBallast: StandardMagnitude = 0.5f; break;
	case ECastleBadgeType::FirepowerCard:
	case ECastleBadgeType::CoinPurse:
	case ECastleBadgeType::SiphonArrow: StandardMagnitude = 0.10f; break;
	case ECastleBadgeType::StartingFunds: StandardMagnitude = 100.0f; break;
	case ECastleBadgeType::DoubleDoorPass:
	case ECastleBadgeType::LoopPurchase:
	case ECastleBadgeType::LapReassignment: StandardMagnitude = 10.0f; break;
	case ECastleBadgeType::SpareBowstring:
	case ECastleBadgeType::ShopRebate:
	case ECastleBadgeType::StringGlove:
	case ECastleBadgeType::ChainReaction:
	case ECastleBadgeType::LastBalloonClasp: StandardMagnitude = 1.0f; break;
	case ECastleBadgeType::ShopDiscount: StandardMagnitude = 0.85f; break;
	case ECastleBadgeType::ShiftWhistle:
	case ECastleBadgeType::RapidMarch:
	case ECastleBadgeType::LateAdvantage: StandardMagnitude = 0.20f; break;
	case ECastleBadgeType::FreeRefresh: StandardMagnitude = 3.0f; break;
	case ECastleBadgeType::QuickLadder: StandardMagnitude = 0.50f; break;
	case ECastleBadgeType::BerserkBee: StandardMagnitude = 5.0f; break;
	default:
		UE_LOG(LogMoving_Castle, Error, TEXT("无法生成标准徽章：类型 %u 无效。"), static_cast<uint32>(InType));
		return false;
	}
	OutBadge = {};
	OutBadge.Type = InType;
	OutBadge.Magnitude = StandardMagnitude;
	return true;
}

bool FCastleBadgeDefinition::Validate(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	FCastleBadgeDefinition Standard;
	if (Type <= ECastleBadgeType::None || Type > ECastleBadgeType::LastBalloonClasp
		|| !TryMakeStandard(Type, Standard) || !FMath::IsFinite(Magnitude) || Magnitude <= 0.0f)
	{
		OutFailureReason = LOCTEXT("InvalidBadge", "徽章类型必须有效，效果数值必须为有限正数。");
		return false;
	}
	if (Type == ECastleBadgeType::BlueAbacus || Type == ECastleBadgeType::WorkshopRefund
		|| Type == ECastleBadgeType::StartingFunds || Type == ECastleBadgeType::DoubleDoorPass
		|| Type == ECastleBadgeType::SpareBowstring || Type == ECastleBadgeType::ShopRebate
		|| Type == ECastleBadgeType::BerserkBee)
	{
		// 牌面数字和张数属于整数契约。先用 double 比较上限，再检验整数性，避免 float 容量边界舍入。
		const double Count = Magnitude;
		if (Count > MAX_int32 || Count != static_cast<int32>(Count))
		{
			OutFailureReason = LOCTEXT("NonIntegralBadge", "徽章的房间数字、赠牌数或金币数必须是 int32 范围内的正整数。");
			return false;
		}
	}
	// 次数、门槛和指定倍率属于玩法规则。拒绝配置成界面显示一套、结算使用另一套的数值。
	if ((Type == ECastleBadgeType::BurstAlarm || Type == ECastleBadgeType::EmergencyBallast
		|| Type == ECastleBadgeType::FreeRefresh || Type == ECastleBadgeType::LoopPurchase
		|| Type == ECastleBadgeType::LateAdvantage || Type == ECastleBadgeType::ChainReaction
		|| Type == ECastleBadgeType::LapReassignment || Type == ECastleBadgeType::LastBalloonClasp
		|| Type == ECastleBadgeType::ShopDiscount || Type == ECastleBadgeType::CoinPurse)
		&& Magnitude != Standard.Magnitude)
	{
		OutFailureReason = LOCTEXT("FixedMagnitudeBadge", "该徽章必须使用规则表规定的固定数值。");
		return false;
	}
	return true;
}

FText FCastleBadgeDefinition::GetDisplayName() const
{
	switch (Type)
	{
	case ECastleBadgeType::BlueAbacus: return LOCTEXT("BlueRoomNumberName", "蓝釉算盘");
	case ECastleBadgeType::WorkshopRefund: return LOCTEXT("UpgradeRoomCardsName", "工坊返料章");
	case ECastleBadgeType::BalloonRack: return LOCTEXT("MainTowerHealthName", "备用气球挂架");
	case ECastleBadgeType::QuickstringPass: return LOCTEXT("ArcherFireRateName", "快弦工牌");
	case ECastleBadgeType::BurstAlarm: return LOCTEXT("FirstMainTowerDamageName", "破球警钟");
	case ECastleBadgeType::EmergencyBallast: return LOCTEXT("LowMainTowerHealthName", "应急压舱扣");
	case ECastleBadgeType::FirepowerCard: return LOCTEXT("FirepowerCardName", "旺财的火力牌");
	case ECastleBadgeType::CoinPurse: return LOCTEXT("CoinPurseName", "管家的零钱袋");
	case ECastleBadgeType::StartingFunds: return LOCTEXT("StartingFundsName", "充足的启动资金");
	case ECastleBadgeType::DoubleDoorPass: return LOCTEXT("DoubleDoorPassName", "双门打卡牌");
	case ECastleBadgeType::SpareBowstring: return LOCTEXT("SpareBowstringName", "门后备用弦");
	case ECastleBadgeType::ShopDiscount: return LOCTEXT("ShopDiscountName", "商店折扣");
	case ECastleBadgeType::ShopRebate: return LOCTEXT("ShopRebateName", "商店返利");
	case ECastleBadgeType::ShiftWhistle: return LOCTEXT("ShiftWhistleName", "值班汽笛");
	case ECastleBadgeType::StringGlove: return LOCTEXT("StringGloveName", "摘绳手套");
	case ECastleBadgeType::FreeRefresh: return LOCTEXT("FreeRefreshName", "翻货免单章");
	case ECastleBadgeType::QuickLadder: return LOCTEXT("QuickLadderName", "快速梯子");
	case ECastleBadgeType::LoopPurchase: return LOCTEXT("LoopPurchaseName", "环路采购签");
	case ECastleBadgeType::LateAdvantage: return LOCTEXT("LateAdvantageName", "后手优势");
	case ECastleBadgeType::SiphonArrow: return LOCTEXT("SiphonArrowName", "虹吸箭簇");
	case ECastleBadgeType::RapidMarch: return LOCTEXT("RapidMarchName", "急速行军");
	case ECastleBadgeType::BerserkBee: return LOCTEXT("BerserkBeeName", "狂暴工蜂");
	case ECastleBadgeType::ChainReaction: return LOCTEXT("ChainReactionName", "连锁反应");
	case ECastleBadgeType::LapReassignment: return LOCTEXT("LapReassignmentName", "一圈调岗令");
	case ECastleBadgeType::LastBalloonClasp: return LOCTEXT("LastBalloonClaspName", "最后一颗气球扣");
	default: return FText::GetEmpty();
	}
}

FText FCastleBadgeDefinition::GetEffectSummary() const
{
	// 槽位读取已经验证的徽章，数值文案直接使用相同的配置。
	switch (Type)
	{
	case ECastleBadgeType::BlueAbacus:
		return FText::Format(LOCTEXT("BlueRoomNumberSummary", "蓝色房间结算数字 +{0}"), FText::AsNumber(Magnitude));
	case ECastleBadgeType::WorkshopRefund:
		return FText::Format(LOCTEXT("UpgradeRoomCardsSummary", "功能塔合成升级时获得 {0} 张随机房间卡"), FText::AsNumber(Magnitude));
	case ECastleBadgeType::BalloonRack:
		return FText::Format(LOCTEXT("MainTowerHealthSummary", "主塔生命 +{0}"), FText::AsPercent(static_cast<double>(Magnitude)));
	case ECastleBadgeType::QuickstringPass:
		return FText::Format(LOCTEXT("ArcherFireRateSummary", "箭塔射速 +{0}"), FText::AsPercent(static_cast<double>(Magnitude)));
	case ECastleBadgeType::BurstAlarm:
		return LOCTEXT("FirstMainTowerDamageSummary", "本场主塔首次掉血后，所有房间射速与召唤物移速翻倍");
	case ECastleBadgeType::EmergencyBallast:
		return LOCTEXT("LowMainTowerHealthSummary", "主塔生命剩余 20% 或更少时，敌人攻速降低 50%");
	case ECastleBadgeType::FirepowerCard:
		return FText::Format(LOCTEXT("FirepowerCardSummary", "全体我方伤害 +{0}"), FText::AsPercent(static_cast<double>(Magnitude)));
	case ECastleBadgeType::CoinPurse:
		return LOCTEXT("CoinPurseSummary", "玩法产出的金币增加 10%，向上取整");
	case ECastleBadgeType::StartingFunds:
		return FText::Format(LOCTEXT("StartingFundsSummary", "新开局额外获得 {0} 金币"), FText::AsNumber(Magnitude));
	case ECastleBadgeType::DoubleDoorPass:
		return FText::Format(LOCTEXT("DoubleDoorPassSummary", "巡逻路线长度超过 10 格时，清场获得 {0} 金币"), FText::AsNumber(Magnitude));
	case ECastleBadgeType::SpareBowstring:
		return FText::Format(LOCTEXT("SpareBowstringSummary", "每波首次有士兵进入功能塔时，本次触发数字 +{0}"), FText::AsNumber(Magnitude));
	case ECastleBadgeType::ShopDiscount:
		return LOCTEXT("ShopDiscountSummary", "普通商店买牌价乘 85%，正价最低 1 金币");
	case ECastleBadgeType::ShopRebate:
		return FText::Format(LOCTEXT("ShopRebateSummary", "在普通商店每购买 10 次，获得 {0} 张随机房间卡"), FText::AsNumber(Magnitude));
	case ECastleBadgeType::ShiftWhistle:
		return FText::Format(LOCTEXT("ShiftWhistleSummary", "每波每名士兵首次进功能塔时移速 +{0}，持续 4 秒，每波最多惠及 3 人"),
			FText::AsPercent(static_cast<double>(Magnitude)));
	case ECastleBadgeType::StringGlove:
		return FText::Format(LOCTEXT("StringGloveSummary", "手动点破空投气球时，落地伤害和减速半径扩大 {0} 格"), FText::AsNumber(Magnitude));
	case ECastleBadgeType::FreeRefresh:
		return LOCTEXT("FreeRefreshSummary", "每次进入编辑阶段获得 3 次普通商店免费刷新机会");
	case ECastleBadgeType::QuickLadder:
		return FText::Format(LOCTEXT("QuickLadderSummary", "士兵在梯子上的移速 +{0}"), FText::AsPercent(static_cast<double>(Magnitude)));
	case ECastleBadgeType::LoopPurchase:
		return LOCTEXT("LoopPurchaseSummary", "普通商店每刷新 10 次，首件商品数字与主建筑栏首张牌相同");
	case ECastleBadgeType::LateAdvantage:
		return LOCTEXT("LateAdvantageSummary", "士兵首圈移速减半，第二圈起每已完成一圈，移速在基础值上增加 20%");
	case ECastleBadgeType::SiphonArrow:
		return FText::Format(LOCTEXT("SiphonArrowSummary", "箭塔追踪箭命中后，为存活主塔恢复相当于伤害 {0} 的生命"),
			FText::AsPercent(static_cast<double>(Magnitude)));
	case ECastleBadgeType::RapidMarch:
		return FText::Format(LOCTEXT("RapidMarchSummary", "士兵进入功能塔后，按塔的每级获得 {0} 移速加成，持续 3 秒"),
			FText::AsPercent(static_cast<double>(Magnitude)));
	case ECastleBadgeType::BerserkBee:
		return FText::Format(LOCTEXT("BerserkBeeSummary", "蜂群每击杀一个敌人获得 {0} 金币"), FText::AsNumber(Magnitude));
	case ECastleBadgeType::ChainReaction:
		return LOCTEXT("ChainReactionSummary", "功能塔触发时，同类型、颜色和数字的其他功能塔各触发一次");
	case ECastleBadgeType::LapReassignment:
		return LOCTEXT("LapReassignmentSummary", "本场正式士兵每累计完成 10 圈，从门 A 派出一名走完一圈即离场的临时士兵");
	case ECastleBadgeType::LastBalloonClasp:
		return LOCTEXT("LastBalloonClaspSummary", "每局一次，敌方攻击将摧毁主塔时，耗尽剩余次数最多的护盾并保留最后一颗气球");
	default:
		return FText::GetEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
