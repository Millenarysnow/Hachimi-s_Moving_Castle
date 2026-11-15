#pragma once

#include "CoreMinimal.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "CastleFarmEffectComponent.generated.h"

class UCastleEconomyComponent;

/** 农田每次进入独立结算同色连通产金；五级同时把本座自身数字的 10% 叠加到真实 NPC。 */
UCLASS(ClassGroup=(Castle), BlueprintType)
class MOVING_CASTLE_API UCastleFarmEffectComponent : public UCastleBuildingEffectComponent
{
	GENERATED_BODY()

public:
	/** 测试世界或显式自定义账户可注入；未注入时只查询本世界玩家控制器的金币账户。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="建筑|农田")
	TObjectPtr<UCastleEconomyComponent> EconomyOverride;

	/** 便于界面观察最近一次实际入账，失败不覆盖该结果。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="建筑|农田")
	int32 LastGoldGranted = 0;

protected:
	/** 先完成范围、账户和 NPC 增益的校验，再执行一次金币提交与五级 NPC 增益累加。 */
	virtual bool ExecuteEffect(const FCastleBuildingTriggerContext& Context) override;
};
