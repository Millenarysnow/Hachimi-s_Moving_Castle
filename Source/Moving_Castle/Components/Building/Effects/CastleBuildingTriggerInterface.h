#pragma once

#include "CoreMinimal.h"
#include "CastleMassTypes.h"
#include "UObject/Interface.h"
#include "CastleBuildingTriggerInterface.generated.h"

/** 进入事件与主动触发共用入口，但不能把没有 NPC 的主动调用伪装为实体进入。 */
UENUM(BlueprintType)
enum class ECastleBuildingTriggerSource : uint8
{
	Manual,
	NPCEntry
};

/** 值类型触发上下文；保留真实 Mass 句柄，不要求单位具有 Actor 表现。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleBuildingTriggerContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="建筑|效果")
	ECastleBuildingTriggerSource Source = ECastleBuildingTriggerSource::Manual;

	/** 仅 NPCEntry 必填；世界和实体序列号共同防止跨世界或槽位复用。 */
	UPROPERTY(BlueprintReadOnly, Category="建筑|效果")
	FCastleMassUnitHandle NPC;

	/** 触发瞬间在共享路线上的位置，不能用本帧最终位置代替中途经过的位置。 */
	UPROPERTY(BlueprintReadOnly, Category="建筑|效果")
	FVector EntryLocation = FVector::ZeroVector;
};

/** 入口由 C++ 实现；蓝图可以调用、观察和配置，但不接管核心效果逻辑。 */
UINTERFACE(BlueprintType, meta=(CannotImplementInterfaceInBlueprint))
class MOVING_CASTLE_API UCastleBuildingTriggerInterface : public UInterface
{
	GENERATED_BODY()
};

class MOVING_CASTLE_API ICastleBuildingTriggerInterface
{
	GENERATED_BODY()

public:
	/** 无内置冷却；每次成功调用都必须执行自身效果组件一次。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|建筑效果")
	virtual bool TriggerBuildingEffect(const FCastleBuildingTriggerContext& Context) = 0;
};
