#include "Items/CastleBadgeTypes.h"

#define LOCTEXT_NAMESPACE "CastleBadgeTypes"

bool FCastleBadgeDefinition::TryMakeStandard(const ECastleBadgeType InType, FCastleBadgeDefinition& OutBadge)
{
	float StandardMagnitude;
	switch (InType)
	{
	case ECastleBadgeType::GambleLuck: StandardMagnitude = 0.15f; break;
	case ECastleBadgeType::BlueRoomNumber: StandardMagnitude = 1.0f; break;
	case ECastleBadgeType::UpgradeRoomCards: StandardMagnitude = 2.0f; break;
	case ECastleBadgeType::MainTowerHealth:
	case ECastleBadgeType::ArcherFireRate: StandardMagnitude = 0.20f; break;
	case ECastleBadgeType::FirstMainTowerDamage: StandardMagnitude = 2.0f; break;
	case ECastleBadgeType::LowMainTowerHealth: StandardMagnitude = 0.5f; break;
	default: return false;
	}
	OutBadge.Type = InType;
	OutBadge.Magnitude = StandardMagnitude;
	return true;
}

bool FCastleBadgeDefinition::Validate(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	FCastleBadgeDefinition Standard;
	if (!TryMakeStandard(Type, Standard) || !FMath::IsFinite(Magnitude) || Magnitude <= 0.0f)
	{
		OutFailureReason = LOCTEXT("InvalidBadge", "徽章类型必须有效，效果数值必须为有限正数。");
		return false;
	}
	if (Type == ECastleBadgeType::BlueRoomNumber || Type == ECastleBadgeType::UpgradeRoomCards)
	{
		// 牌面数字和张数属于整数契约。先用 double 比较上限，再检验整数性，避免 float 容量边界舍入。
		const double Count = Magnitude;
		if (Count > MAX_int32 || Count != static_cast<int32>(Count))
		{
			OutFailureReason = LOCTEXT("NonIntegralBadge", "蓝色房间数字加成与升级赠牌数量必须为正整数。");
			return false;
		}
	}
	if ((Type == ECastleBadgeType::FirstMainTowerDamage || Type == ECastleBadgeType::LowMainTowerHealth)
		&& Magnitude != Standard.Magnitude)
	{
		OutFailureReason = LOCTEXT("FixedMagnitudeBadge", "触发式徽章须使用对应的固定倍率。");
		return false;
	}
	return true;
}

FText FCastleBadgeDefinition::GetDisplayName() const
{
	switch (Type)
	{
	case ECastleBadgeType::GambleLuck: return LOCTEXT("GambleLuckName", "赌博幸运");
	case ECastleBadgeType::BlueRoomNumber: return LOCTEXT("BlueRoomNumberName", "蓝色房间数字");
	case ECastleBadgeType::UpgradeRoomCards: return LOCTEXT("UpgradeRoomCardsName", "功能塔升级赠牌");
	case ECastleBadgeType::MainTowerHealth: return LOCTEXT("MainTowerHealthName", "主塔生命");
	case ECastleBadgeType::ArcherFireRate: return LOCTEXT("ArcherFireRateName", "箭塔射速");
	case ECastleBadgeType::FirstMainTowerDamage: return LOCTEXT("FirstMainTowerDamageName", "首次受伤加速");
	case ECastleBadgeType::LowMainTowerHealth: return LOCTEXT("LowMainTowerHealthName", "低血量压制");
	default: return FText::GetEmpty();
	}
}

FText FCastleBadgeDefinition::GetEffectSummary() const
{
	// 槽位读取已经验证的徽章，数值文案直接使用相同的配置。
	switch (Type)
	{
	case ECastleBadgeType::GambleLuck:
		return FText::Format(LOCTEXT("GambleLuckSummary", "赌博幸运权重 +{0}"), FText::AsNumber(Magnitude));
	case ECastleBadgeType::BlueRoomNumber:
		return FText::Format(LOCTEXT("BlueRoomNumberSummary", "蓝色房间结算数字 +{0}"), FText::AsNumber(Magnitude));
	case ECastleBadgeType::UpgradeRoomCards:
		return FText::Format(LOCTEXT("UpgradeRoomCardsSummary", "功能塔合成升级时获得 {0} 张随机房间卡"), FText::AsNumber(Magnitude));
	case ECastleBadgeType::MainTowerHealth:
		return FText::Format(LOCTEXT("MainTowerHealthSummary", "主塔生命 +{0}"), FText::AsPercent(static_cast<double>(Magnitude)));
	case ECastleBadgeType::ArcherFireRate:
		return FText::Format(LOCTEXT("ArcherFireRateSummary", "箭塔射速 +{0}"), FText::AsPercent(static_cast<double>(Magnitude)));
	case ECastleBadgeType::FirstMainTowerDamage:
		return LOCTEXT("FirstMainTowerDamageSummary", "本场主塔首次掉血后，所有房间射速与召唤物移速翻倍");
	case ECastleBadgeType::LowMainTowerHealth:
		return LOCTEXT("LowMainTowerHealthSummary", "主塔生命剩余 20% 或更少时，敌人攻速降低 50%");
	default:
		return FText::GetEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
