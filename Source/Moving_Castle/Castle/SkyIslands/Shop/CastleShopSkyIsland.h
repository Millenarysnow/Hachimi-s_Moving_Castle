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
class UMaterialInterface;
class UStaticMesh;
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
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Tint;
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
#endif
	bool IsIslandController(const ACastlePlayerController* Controller) const;
	bool Purchase(UCastleShopSkyIslandOffer& Offer, FText& OutReason);
	bool CreateOffer(UCastleRoomGroupDataAsset* RoomGroup, UCastleBadgeDataAsset* Badge, int32 RewardSeed, FText& OutReason);
	bool RejectOffer(const FText& Reason, FText& OutReason) const;
	bool CheckFunds(const UCastleShopSkyIslandOffer& Offer, FText& OutReason) const;
	bool OpenPendingOffer(UCastleShopSkyIslandOffer& Offer, FText& OutReason);
	void RefundFailedOffer(const UCastleShopSkyIslandOffer& Offer, const FText& Failure);
	void CompleteSale(UCastleShopSkyIslandOffer& Offer);

	UPROPERTY(EditDefaultsOnly, Category = "商店空岛")
	TSoftObjectPtr<UCastleShopSkyIslandDataAsset> ConfigAsset;
	UPROPERTY(Transient) TObjectPtr<UCastleShopSkyIslandDataAsset> Config;
	UPROPERTY() TObjectPtr<UStaticMesh> RoomGroupMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> BadgeMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> OfferMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UCastleShopSkyIslandOffer>> Offers;
	UPROPERTY(Transient) TObjectPtr<UCastleShopSkyIslandOffer> OpenOffer;
	TWeakObjectPtr<ACastleBoard> Board;
	TWeakObjectPtr<UCastleEconomyComponent> Economy;
	bool bResolvingSale = false;
};
