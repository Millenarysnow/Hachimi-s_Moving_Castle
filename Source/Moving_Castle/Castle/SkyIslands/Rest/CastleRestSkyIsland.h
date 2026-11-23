#pragma once

#include "Castle/SkyIslands/CastleSkyIslandBase.h"
#include "Engine/DataAsset.h"
#include "CastleRestSkyIsland.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTextRenderComponent;

/** 单座休息空岛的免费补给数值。配置错误时由空岛初始化拒绝进入。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleRestSkyIslandDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 按本局主塔的实际最大生命计算恢复量。 */
	UPROPERTY(EditAnywhere, Category = "休息空岛")
	float HealFraction = 1.0f;

	/** 金币台的一次性奖励。零金币仍按一次有效的领取处理。 */
	UPROPERTY(EditAnywhere, Category = "休息空岛")
	int32 GoldReward = 50;

	bool Validate(FText& OutReason) const;
};

/** 台座与奖品一一对应；已领取状态只存在于本次生成的台座实例。 */
UCLASS()
class UCastleRestSkyIslandStation : public UStaticMeshComponent, public ICastleSkyIslandInteractable
{
	GENERATED_BODY()

public:
	virtual FText GetInteractPrompt() const override;
	virtual bool CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const override;
	virtual bool ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) override;

private:
	friend class ACastleRestSkyIsland;
	FText Prompt;
	FString Label;
	UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> Sign;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Tint;
	bool bClaimed = false;
	bool bClaiming = false;
};

/** 在基座的公共平台上布置回血、金币和额外房间商店三个独立交互点。 */
UCLASS()
class MOVING_CASTLE_API ACastleRestSkyIsland : public ACastleSkyIslandBase
{
	GENERATED_BODY()

public:
	ACastleRestSkyIsland();

protected:
	virtual bool OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutReason) override;

private:
	friend class UCastleRestSkyIslandStation;
	bool ClaimStation(const UCastleRestSkyIslandStation& Station, FText& OutMessage, FText& OutReason);
	bool IsIslandController(const ACastlePlayerController* Controller) const;

	UPROPERTY(EditDefaultsOnly, Category = "休息空岛")
	TSoftObjectPtr<UCastleRestSkyIslandDataAsset> ConfigAsset;
	UPROPERTY(Transient) TObjectPtr<UCastleRestSkyIslandDataAsset> Config;
	UPROPERTY() TObjectPtr<UMaterialInterface> StationMaterial;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCastleRestSkyIslandStation> HealStation;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCastleRestSkyIslandStation> GoldStation;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCastleRestSkyIslandStation> ShopStation;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> HealSign;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> GoldSign;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> ShopSign;
};
