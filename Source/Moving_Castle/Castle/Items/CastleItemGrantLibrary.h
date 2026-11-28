#pragma once

#include "Items/CastleItemTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CastleItemGrantLibrary.generated.h"

class ACastleBoard;
class UCastleBackpackComponent;
class UCastleBadgeDataAsset;
class UCastleBadgeSubsystem;
class UCastlePendingRoomReward;
class UCastleRoomGroupDataAsset;

/** 奖励入口先完整验证配置，再发牌或返回一个待领取句柄。 */
UCLASS()
class MOVING_CASTLE_API UCastleItemGrantLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 直接发放时 OutPendingReward 为空，待领取项只在点击「获取」后开始选择。 */
	static bool TryGrantRoomGroup(const UCastleRoomGroupDataAsset* Group, UCastleBackpackComponent* Backpack,
		const ACastleBoard* Board, TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool, FRandomStream& Random,
		UCastlePendingRoomReward*& OutPendingReward, FText& OutFailureReason);

	/** 准备发牌、待领句柄与主栏转换。调用方应使用候选随机流，失败时保留原随机流和背包。 */
	static bool TryPrepareRoomGroup(const UCastleRoomGroupDataAsset* Group, UCastleBackpackComponent* Backpack,
		const ACastleBoard* Board, TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool, FRandomStream& Random,
		FCastlePreparedRoomGroup& OutPrepared, FText& OutFailureReason);

	/** 原子提交主栏转换与溢出发牌。待领句柄由调用方交给现有选择界面。 */
	static bool TryCommitPreparedRoomGroup(const FCastlePreparedRoomGroup& Prepared, UCastleBackpackComponent* Backpack,
		FText& OutFailureReason);

	/** 普通奖励也可直接用万能卡规格创建独立待领取项。 */
	static bool TryCreateWildcardReward(FText DisplayName, const FCastleWildcardRoomSpec& Spec,
		UCastleBackpackComponent* Backpack, TConstArrayView<TObjectPtr<UCastleBuildingDefinition>> DefinitionPool,
		UCastlePendingRoomReward*& OutPendingReward, FText& OutFailureReason);

	UFUNCTION(BlueprintCallable, Category = "移动城堡|道具")
	static bool TryGrantBadge(const UCastleBadgeDataAsset* Asset, UCastleBadgeSubsystem* Badges,
		bool& bOutNeedsReplacement, FText& OutFailureReason);

private:
	static UCastlePendingRoomReward* CreateReward(FText DisplayName, UCastleBackpackComponent& Backpack,
		FCastleWildcardRoomOptions&& Options);
};
