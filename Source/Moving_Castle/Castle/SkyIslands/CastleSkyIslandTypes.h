#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CastleSkyIslandTypes.generated.h"

class ACastlePlayerController;
class ACastleSkyIslandBase;
class UCastleDirectorSubsystem;

/** 四个工厂槽位；每个玩法子类只依赖空岛基座。 */
UENUM(BlueprintType)
enum class ECastleSkyIslandType : uint8
{
	Shop,
	NPC,
	Gamble,
	Rest
};

/** 初始化只交付本世界、关数和子类需要的导演入口。 */
struct FCastleSkyIslandContext
{
	TWeakObjectPtr<UCastleDirectorSubsystem> Director;
	TWeakObjectPtr<ACastlePlayerController> Controller;
	ECastleSkyIslandType Type = ECastleSkyIslandType::Rest;
	int32 Step = 0;
	int32 Seed = 0;
};

/** 玩家准星命中时提供本岛、控制器与实际命中组件。 */
struct FCastleSkyIslandInteractContext
{
	ACastleSkyIslandBase* Island = nullptr;
	ACastlePlayerController* Controller = nullptr;
	FHitResult Hit;
};

UINTERFACE()
class UCastleSkyIslandInteractable : public UInterface
{
	GENERATED_BODY()
};

/** 原生空岛物件可由 Actor 或其组件实现同一组交互方法。 */
class MOVING_CASTLE_API ICastleSkyIslandInteractable
{
	GENERATED_BODY()

public:
	virtual FText GetInteractPrompt() const = 0;
	virtual bool CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const = 0;
	/** 每帧悬停探测使用的纯查询，不得输出日志或触发玩法副作用。 */
	virtual bool CanHighlightInteract(const FCastleSkyIslandInteractContext& Context) const = 0;
	virtual bool ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutFailureReason) = 0;
};
