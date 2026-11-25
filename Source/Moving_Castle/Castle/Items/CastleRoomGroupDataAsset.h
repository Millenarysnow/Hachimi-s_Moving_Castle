#pragma once

#include "Engine/DataAsset.h"
#include "Items/CastleItemTypes.h"
#include "CastleRoomGroupDataAsset.generated.h"

/** 同一模板按配置约束可用建筑、色值和数字，再由原生发放入口生成卡牌。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleRoomGroupDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 奖励待领取项显示的名称。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组")
	ECastleRoomGroupType GroupType = ECastleRoomGroupType::ThreeNumberFourArchers;

	/** 正式房间组必须显式选择卡包稀有度，未配置的资产会被商店和发放入口拒绝。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组")
	ECastleRoomPackRarity Rarity = ECastleRoomPackRarity::None;

	/** 供商店与发放共用校验；非法值记录房间组资产路径和具体数值。 */
	bool ValidateRarity(FText& OutReason) const;

	/** 模板使用的候选属性。模板固定的属性由类型约束，可用单一合法候选显式锁定。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组")
	FCastleWildcardRoomSpec RoomSpec;

	/** 大数字房和加速房可选为先展示待领取项，点击后再选择允许属性。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组")
	bool bChooseInPicker = false;
};
