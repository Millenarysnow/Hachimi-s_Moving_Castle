#pragma once

#include "Engine/DataAsset.h"
#include "Items/CastleBadgeTypes.h"
#include "CastleBadgeDataAsset.generated.h"

/** 奖励编辑器配置单枚徽章。持有状态只保存在对应 GameInstance 的徽章子系统。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleBadgeDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "徽章")
	FCastleBadgeDefinition Badge;
};
