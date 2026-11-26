#pragma once

#include "Engine/DataAsset.h"
#include "CastleGameplayTypes.h"
#include "Items/CastleBadgeTypes.h"
#include "CastleBadgeDataAsset.generated.h"

class UStaticMesh;

/** 奖励编辑器配置单枚徽章。持有状态只保存在对应 GameInstance 的徽章子系统。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleBadgeDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 单枚徽章的效果和槽位图标，一起传入徽章库存。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "徽章")
	FCastleBadgeDefinition Badge;

	/** 决定商店空岛的抽样档位，必须在资产中填写。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "徽章")
	ECastleItemRarity Rarity = ECastleItemRarity::None;

	/** 单件售价，负数标记资产尚未配置售价。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "徽章")
	int32 ShopPrice = INDEX_NONE;

	/** 空岛货台展示此徽章对应的 GLB 导入网格。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "徽章|展示")
	TObjectPtr<UStaticMesh> DisplayMesh = nullptr;
};
