#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastleGameplayTypes.h"
#include "CastleSupportTowerData.generated.h"

class UMassEntityConfigAsset;
class UMaterialInterface;

/** 只配置策划未给定的平衡数值、气球航线和表现依赖；等级规则仍由 C++ 唯一结算。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleSupportTowerData : public UDataAsset
{
	GENERATED_BODY()
public:
	/** 每只蜂每次命中的基础伤害；升档改变频率而不是单次伤害。 */
	UPROPERTY(EditAnywhere, Category="蜂巢") float BeeDamage = 0.0f;
	UPROPERTY(EditAnywhere, Category="蜂巢") float BeeFlightSpeed = 0.0f;

	/** 相对塔顶占格中心的航点，单位 cm；沿折线飞一次，到末点悬停直到被击破。 */
	UPROPERTY(EditAnywhere, Category="空投") TArray<FVector> BalloonPath;
	UPROPERTY(EditAnywhere, Category="空投") float BalloonSpeed = 0.0f;
	UPROPERTY(EditAnywhere, Category="空投") float BalloonHealth = 0.0f;
	UPROPERTY(EditAnywhere, Category="空投") float AttractionRadius = 0.0f;
	UPROPERTY(EditAnywhere, Category="空投") float LandingRadius = 0.0f;
	UPROPERTY(EditAnywhere, Category="空投") float CargoFallSpeed = 0.0f;
	UPROPERTY(EditAnywhere, Category="空投") float MaxDropDepth = 0.0f;

	/** 专供蓝色空投生成人员；和导演的战斗派遣使用同类 Mass NPC 模板。 */
	UPROPERTY(EditAnywhere, Category="空投") TObjectPtr<UMassEntityConfigAsset> NPCConfiguration;

	/** 球形蜂体、气球及货箱共用可着色的材质，不从塔身替换材质。 */
	UPROPERTY(EditAnywhere, Category="表现") TObjectPtr<UMaterialInterface> FlightMaterial;

	bool ValidateFor(ECastleBuildingType Type, FText& OutReason) const;
};
