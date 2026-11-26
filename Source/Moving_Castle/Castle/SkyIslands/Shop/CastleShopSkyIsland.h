#pragma once

#include "Castle/SkyIslands/CastleSkyIslandBase.h"
#include "CastleShopSkyIsland.generated.h"

class UCastleBadgeDataAsset;
class UCastleEconomyComponent;
class UCastlePendingRoomReward;
class ACastleBoard;
class UCastleRoomGroupDataAsset;
class UCastleShopSkyIslandDataAsset;
class UMaterialInstanceDynamic;
class UTextRenderComponent;

/** 玩家按 E 命中的具体商品台，保存本台商品、固定售价和本次访问的售出状态。 */
UCLASS()
class UCastleShopSkyIslandOffer : public UStaticMeshComponent, public ICastleSkyIslandInteractable
{
	GENERATED_BODY()

public:
	virtual FText GetInteractPrompt() const override;
	virtual bool CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const override;
	virtual bool ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) override;

private:
	friend class ACastleShopSkyIsland;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FCastleShopSkyIslandScenario;
#endif
	void MarkSold();

	UPROPERTY(Transient) TObjectPtr<UCastleRoomGroupDataAsset> RoomGroup;
	UPROPERTY(Transient) TObjectPtr<UCastleBadgeDataAsset> Badge;
	UPROPERTY(Transient) TObjectPtr<UCastlePendingRoomReward> PendingReward;
	UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> Sign;
	/** 售罄时只更新本货位的动态材质，其他货位继续使用各自的状态。 */
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> OfferMaterialInstance;
	FText ProductName;
	FString DisplayLabel;
	int32 Price = 0;
	int32 RewardSeed = 0;
	bool bSold = false;
	bool bSubmitting = false;
};

/** 商店空岛以本岛种子决定商品，经正式背包和徽章接口交付购买结果。 */
UCLASS()
class MOVING_CASTLE_API ACastleShopSkyIsland : public ACastleSkyIslandBase
{
	GENERATED_BODY()

public:
	ACastleShopSkyIsland();

protected:
	virtual bool OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutReason) override;

private:
	friend class UCastleShopSkyIslandOffer;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FCastleShopSkyIslandScenario;
	friend class FCastleShopSkyIslandRarityTest;
#endif
	/** 抽样结果只引用一种商品；房间组发牌使用货位自己的随机种子。 */
	struct FProduct
	{
		UCastleRoomGroupDataAsset* RoomGroup = nullptr;
		UCastleBadgeDataAsset* Badge = nullptr;
		int32 RewardSeed = 0;

		bool operator==(const FProduct& Other) const
		{
			// 去重看资产身份，同一商品的不同发牌种子仍是同一件商品。
			return RoomGroup == Other.RoomGroup && Badge == Other.Badge;
		}
	};

	/** 从已校验的配置生成整套货位，失败时不提交任何抽样结果。 */
	static bool TryRollProducts(const UCastleShopSkyIslandDataAsset& Config, int32 TowerLevel, int32 Seed,
		TArray<FProduct>& OutProducts, FText& OutReason);
	bool IsIslandController(const ACastlePlayerController* Controller) const;
	bool Purchase(UCastleShopSkyIslandOffer& Offer, FText& OutReason);
	bool CreateOffer(const FProduct& Product, FText& OutReason);
	bool RejectOffer(const FText& Reason, FText& OutReason) const;
	bool CheckFunds(const UCastleShopSkyIslandOffer& Offer, FText& OutReason) const;
	bool OpenPendingOffer(UCastleShopSkyIslandOffer& Offer, FText& OutReason);
	void RefundFailedOffer(const UCastleShopSkyIslandOffer& Offer, const FText& Failure);
	void CompleteSale(UCastleShopSkyIslandOffer& Offer);

	UPROPERTY(EditDefaultsOnly, Category = "商店空岛")
	TSoftObjectPtr<UCastleShopSkyIslandDataAsset> ConfigAsset;
	UPROPERTY(Transient) TObjectPtr<UCastleShopSkyIslandDataAsset> Config;
	UPROPERTY(Transient) TArray<TObjectPtr<UCastleShopSkyIslandOffer>> Offers;
	UPROPERTY(Transient) TObjectPtr<UCastleShopSkyIslandOffer> OpenOffer;
	TWeakObjectPtr<ACastleBoard> Board;
	TWeakObjectPtr<UCastleEconomyComponent> Economy;
	bool bResolvingSale = false;
};
