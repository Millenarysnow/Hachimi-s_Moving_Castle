#pragma once

#include "Engine/DataAsset.h"
#include "Containers/StaticArray.h"
#include "CastleGameplayTypes.h"
#include "CastleShopSkyIslandDataAsset.generated.h"

class UCastleBadgeDataAsset;
class UCastleRoomGroupDataAsset;
class UMaterialInterface;
class UStaticMesh;

/** 单个主塔等级的五档整数百分比；四行依次对应主塔 Lv1 至 Lv4。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleShopRarityWeights
{
	GENERATED_BODY()

	/** 由空岛配置显式填写。五档均非负，每行合计 100。 */
	UPROPERTY(EditAnywhere, Category = "商品稀有度") int32 Common = 0;
	UPROPERTY(EditAnywhere, Category = "商品稀有度") int32 Uncommon = 0;
	UPROPERTY(EditAnywhere, Category = "商品稀有度") int32 Rare = 0;
	UPROPERTY(EditAnywhere, Category = "商品稀有度") int32 Epic = 0;
	UPROPERTY(EditAnywhere, Category = "商品稀有度") int32 Legendary = 0;

	/** 校验和抽样共用从普通到传说的顺序，单位为百分比。 */
	TStaticArray<int32, 5> GetPercentages() const { return {Common, Uncommon, Rare, Epic, Legendary}; }
};

/** 实体货架使用专属配置，商品内容沿用房间组和徽章的原始资产。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleShopSkyIslandDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 商店空岛固定四个货位。保留配置字段供初始化拒绝错误的资产。 */
	UPROPERTY(EditAnywhere, Category = "商店空岛")
	int32 OfferCount = 4;

	/** 全部房间组共用同一包底轴心网格，配置必须提供可见性查询几何。 */
	UPROPERTY(EditAnywhere, Category = "房间包")
	TObjectPtr<UStaticMesh> RoomPackMesh;

	/** 五个正式稀有度都必须提供材质，商品台据此创建各自的动态材质。 */
	UPROPERTY(EditAnywhere, Category = "房间包")
	TMap<ECastleItemRarity, TObjectPtr<UMaterialInterface>> RoomPackMaterials;

	/** 四行依次对应主塔 Lv1 至 Lv4。每个货位按该行先抽档位，再抽商品。 */
	UPROPERTY(EditAnywhere, Category = "商品稀有度")
	TArray<FCastleShopRarityWeights> RarityWeightsByTowerLevel;

	/** 房间组池中的条目引用原始发牌模板，无需为实体商店复制配置。 */
	UPROPERTY(EditAnywhere, Category = "商店空岛")
	TArray<TObjectPtr<UCastleRoomGroupDataAsset>> RoomGroupPool;

	/** 徽章池中的条目引用原始徽章效果。 */
	UPROPERTY(EditAnywhere, Category = "商店空岛")
	TArray<TObjectPtr<UCastleBadgeDataAsset>> BadgePool;

	bool Validate(FText& OutReason) const;
};
