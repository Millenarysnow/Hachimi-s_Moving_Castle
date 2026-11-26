#pragma once

#include "CastleGameplayTypes.h"
#include "Components/SceneComponent.h"
#include "CastleRoomTraitComponent.generated.h"

/** 已放置建筑的特质组件。Phase 1A 从建筑的 GameplayRoot 挂接并处理格内表现与交互。 */
UCLASS(ClassGroup = (Castle))
class MOVING_CASTLE_API UCastleRoomTraitComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** 从已校验的卡牌携带特质数据，完成组件准备后才允许放置提交。 */
	bool TryInitializeTrait(const FCastleBuildingDescriptor& Descriptor, FText& OutReason);

	/** 传送成功后由房间清除特质；失败时建筑与导演状态保持原状。 */
	bool TryInteractWithPortal(FText& OutReason);
};
