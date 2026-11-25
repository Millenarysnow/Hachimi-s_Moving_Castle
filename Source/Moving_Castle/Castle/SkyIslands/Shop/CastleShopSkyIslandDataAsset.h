#pragma once

#include "Engine/DataAsset.h"
#include "Castle/Items/CastleItemTypes.h"
#include "CastleShopSkyIslandDataAsset.generated.h"

class UCastleBadgeDataAsset;
class UCastleRoomGroupDataAsset;
class UMaterialInterface;
class UStaticMesh;

/** 实体货架使用专属配置，商品内容沿用房间组和徽章的原始资产。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleShopSkyIslandDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 本次访问在岛上展示的商品台数量。 */
	UPROPERTY(EditAnywhere, Category = "商店空岛")
	int32 OfferCount = 4;

	/** 房间组商品的统一售价。配置为零表示免费商品。 */
	UPROPERTY(EditAnywhere, Category = "商店空岛")
	int32 DefaultRoomGroupPrice = 30;

	/** 徽章商品的统一售价。配置为零表示免费商品。 */
	UPROPERTY(EditAnywhere, Category = "商店空岛")
	int32 DefaultBadgePrice = 50;

	/** 全部房间组共用同一包底轴心网格，配置必须提供可见性查询几何。 */
	UPROPERTY(EditAnywhere, Category = "房间包")
	TObjectPtr<UStaticMesh> RoomPackMesh;

	/** 五个正式稀有度都必须提供材质，商品台据此创建各自的动态材质。 */
	UPROPERTY(EditAnywhere, Category = "房间包")
	TMap<ECastleRoomPackRarity, TObjectPtr<UMaterialInterface>> RoomPackMaterials;

	/** 房间组池中的条目引用原始发牌模板，无需为实体商店复制配置。 */
	UPROPERTY(EditAnywhere, Category = "商店空岛")
	TArray<TObjectPtr<UCastleRoomGroupDataAsset>> RoomGroupPool;

	/** 徽章池中的条目引用原始徽章效果。 */
	UPROPERTY(EditAnywhere, Category = "商店空岛")
	TArray<TObjectPtr<UCastleBadgeDataAsset>> BadgePool;

	bool Validate(FText& OutReason) const;
};
