#include "Items/CastleBadgeDataAsset.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"

bool UCastleBadgeDataAsset::ValidateReward(FText& OutFailureReason) const
{
	// 槽位刷新是库存变更的同步观察者。图标和货台网格缺失时，必须在写徽章库存前拒绝。
	if (!IsValid(Badge.Icon) || !IsValid(DisplayMesh))
	{
		OutFailureReason = INVTEXT("徽章缺少有效的 Badge.Icon PNG 或 DisplayMesh GLB。");
		return false;
	}
	if (Rarity < ECastleItemRarity::Common || Rarity > ECastleItemRarity::Legendary || ShopPrice < 0)
	{
		OutFailureReason = INVTEXT("徽章的稀有度无效或售价为负。");
		return false;
	}
	return Badge.Validate(OutFailureReason);
}
