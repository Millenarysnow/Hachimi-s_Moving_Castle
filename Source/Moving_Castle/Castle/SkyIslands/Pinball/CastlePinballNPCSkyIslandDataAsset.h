#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastlePinballNPCSkyIslandDataAsset.generated.h"

/** 增殖门按本次经过的单枚金币计算额外数量。 */
UENUM()
enum class ECastlePinballGateMode : uint8
{
	Multiply,
	Add
};

/** X 沿斜面向下，Y 沿滑轨横向，所有尺寸使用厘米。 */
namespace CastlePinball
{
	inline constexpr double HalfLength = 520.0;
	inline constexpr double HalfWidth = 350.0;
	inline constexpr double CoinRadius = 12.0;
	inline constexpr double NPCRadius = 28.0;
	inline constexpr double LaunchX = -HalfLength + 2.0 * CoinRadius;
	inline constexpr double FixedStep = 1.0 / 120.0;
}

USTRUCT()
struct FCastlePinballGate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Pinball") FVector2D Center = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "Pinball") double Width = 120.0;
	UPROPERTY(EditAnywhere, Category = "Pinball") ECastlePinballGateMode Mode = ECastlePinballGateMode::Multiply;
	UPROPERTY(EditAnywhere, Category = "Pinball") int32 Value = 2;
};

USTRUCT()
struct FCastlePinballPeg
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Pinball") FVector2D Center = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "Pinball") double Radius = 19.0;
};

/** 本岛资源独立校验，通过后才允许工厂把玩家传送进来。 */
UCLASS()
class MOVING_CASTLE_API UCastlePinballNPCSkyIslandDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UCastlePinballNPCSkyIslandDataAsset();
	bool Validate(FText& OutReason) const;

	UPROPERTY(EditAnywhere, Category = "Pinball") float TiltAngleDegrees = 22.0f;
	/** 护栏只改变接触法线方向上的速度，切向速度保留。 */
	UPROPERTY(EditAnywhere, Category = "Pinball|Physics") float WallRestitution = 0.92f;
	/** 碰撞柱先按此系数反射，再沿法线主动弹射。 */
	UPROPERTY(EditAnywhere, Category = "Pinball|Physics") float PegRestitution = 1.12f;
	UPROPERTY(EditAnywhere, Category = "Pinball|Physics", meta = (Units = "cm/s")) float PegBumperImpulse = 160.0f;
	/** 限制斜面加速和连续弹射后的速度，单位为厘米每秒。 */
	UPROPERTY(EditAnywhere, Category = "Pinball|Physics", meta = (Units = "cm/s")) float MaxCoinSpeed = 1400.0f;
	UPROPERTY(EditAnywhere, Category = "Pinball") int32 CoinLaunchCost = 5;
	UPROPERTY(EditAnywhere, Category = "Pinball") int32 InitialNPCCount = 6;
	UPROPERTY(EditAnywhere, Category = "Pinball") int32 CoinsRequiredPerNPC = 5;
	UPROPERTY(EditAnywhere, Category = "Pinball") int32 MaxActiveCoins = 200;
	UPROPERTY(EditAnywhere, Category = "Pinball") float LauncherMinAngle = -35.0f;
	UPROPERTY(EditAnywhere, Category = "Pinball") float LauncherMaxAngle = 35.0f;
	UPROPERTY(EditAnywhere, Category = "Pinball") TArray<FCastlePinballGate> MultiplierGates;
	UPROPERTY(EditAnywhere, Category = "Pinball") TArray<FCastlePinballPeg> Pegs;
};
