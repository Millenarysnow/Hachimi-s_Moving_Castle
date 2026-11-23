#pragma once

#include "Engine/DataAsset.h"
#include "CastleGambleSkyIslandDataAsset.generated.h"

class UCastleBadgeDataAsset;
class UCastleBuildingDefinition;
class UCastleRoomGroupDataAsset;

/** 下注颜色与转盘格数共用相同次序。 */
enum class ECastleRouletteColor : uint8 { Black, Red, Green };

/** 五档按配置表的顺序抽取，未中档始终使用基础权重。 */
enum class ECastleSlotPrize : uint8 { None, DoubleGold, Jackpot, RoomGroup, Badge };

USTRUCT(BlueprintType)
struct FCastleSlotWeights
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "老虎机") int32 Miss = 40;
	UPROPERTY(EditAnywhere, Category = "老虎机") int32 DoubleGold = 30;
	UPROPERTY(EditAnywhere, Category = "老虎机") int32 Jackpot = 12;
	UPROPERTY(EditAnywhere, Category = "老虎机") int32 RoomGroup = 12;
	UPROPERTY(EditAnywhere, Category = "老虎机") int32 Badge = 6;
};

/** 奖励表独立校验，初始化成功后才允许玩家投入牌、金币或气球。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleGambleSkyIslandDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "轮盘") int32 RouletteBlackSlots = 18;
	UPROPERTY(EditAnywhere, Category = "轮盘") int32 RouletteRedSlots = 18;
	UPROPERTY(EditAnywhere, Category = "轮盘") int32 RouletteGreenSlots = 1;
	UPROPERTY(EditAnywhere, Category = "轮盘") int32 RouletteRedBlackMultiplier = 2;
	UPROPERTY(EditAnywhere, Category = "轮盘") int32 RouletteGreenMultiplier = 30;
	/** 微缩区沿用主棋盘的格距和层高，按此比例缩小。 */
	UPROPERTY(EditAnywhere, Category = "轮盘") float MiniBoardCellScale = 0.22f;
	UPROPERTY(EditAnywhere, Category = "轮盘") FIntPoint MiniBoardGridSize = FIntPoint(5, 5);

	UPROPERTY(EditAnywhere, Category = "老虎机") int32 SlotMachineCost = 10;
	UPROPERTY(EditAnywhere, Category = "老虎机") FCastleSlotWeights SlotWeights;
	UPROPERTY(EditAnywhere, Category = "老虎机") TArray<TObjectPtr<UCastleRoomGroupDataAsset>> RoomGroups;
	/** 房间奖励可包含加速房等正式卡牌，定义池独立于主图的商店出牌池。 */
	UPROPERTY(EditAnywhere, Category = "老虎机") TArray<TObjectPtr<UCastleBuildingDefinition>> RoomDefinitions;
	UPROPERTY(EditAnywhere, Category = "老虎机") TArray<TObjectPtr<UCastleBadgeDataAsset>> Badges;

	UPROPERTY(EditAnywhere, Category = "回收") int32 BalloonRecycleGold = 30;

	/** 在传送前报告配置错误，保障运行中整数收支与概率区间都合法。 */
	bool Validate(FText& OutReason) const;

	/** 格号为对应颜色内部的零基索引，动画用它对准结果所属的实际条纹。 */
	bool TryRollRoulette(ECastleRouletteColor Bet, double LuckBonus, double Sample,
		ECastleRouletteColor& OutColor, int32& OutColorSlot) const;
	bool TryRollSlot(double LuckBonus, double Sample, ECastleSlotPrize& OutPrize) const;
};
