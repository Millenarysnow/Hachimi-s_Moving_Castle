#pragma once

#include "Engine/DataAsset.h"
#include "Items/CastleItemTypes.h"
#include "CastleRoomGroupDataAsset.generated.h"

/** 房间组用有序的发牌规则、主栏动作和自定义选项配置，资产名只用于标识和日志。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleRoomGroupDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 奖励待领取项显示的名称。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组")
	FText DisplayName;

	/** 决定空岛商店的抽样档位和卡包材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组")
	ECastleItemRarity Rarity = ECastleItemRarity::None;

	/** 资产须显式填写单品售价；负数用于标识无效配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组")
	int32 ShopPrice = INDEX_NONE;

	/** 供商店与发放共用校验；非法值记录房间组资产路径和具体数值。 */
	bool ValidateRarity(FText& OutReason) const;

	/** 直接发牌时按顺序准备，后续规则可使用已准备的牌面。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组|生成")
	TArray<FCastleRoomCardRule> CardRules;

	/** 仅供显式配置了空主栏发牌的房间组使用。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组|生成")
	TArray<FCastleRoomCardRule> EmptyMainBarCardRules;

	/** 染色或消耗首卡与新牌发放一起提交。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组|主栏")
	ECastleRoomGroupMainBarAction MainBarAction = ECastleRoomGroupMainBarAction::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组|主栏")
	ECastleTileColor RecolorFrom = ECastleTileColor::Gray;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组|主栏")
	ECastleTileColor RecolorTo = ECastleTileColor::Gray;

	/** 明确指定需要使用房间选择界面的奖励。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组|自定义")
	bool bChooseInPicker = false;

	/** 首次打开界面时锁定一次随机功能塔；重复打开沿用同一结果。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组|自定义")
	bool bLockRandomFunctionOnFirstOpen = false;

	/** 自定义奖励的允许选项；可用一个显式候选锁定属性。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "房间组")
	FCastleWildcardRoomSpec RoomSpec;
};
