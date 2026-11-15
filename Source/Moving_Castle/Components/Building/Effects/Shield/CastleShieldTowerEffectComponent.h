#pragma once

#include "CoreMinimal.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "CastleShieldTowerEffectComponent.generated.h"

class UCastleShieldTowerSubsystem;

/** 盾塔等级数值和表现的拥有者；敌人受击、吸引和反伤只使用现有世界护盾契约。 */
UCLASS(ClassGroup=(Castle), BlueprintType)
class MOVING_CASTLE_API UCastleShieldTowerEffectComponent : public UCastleBuildingEffectComponent
{
	GENERATED_BODY()

public:
	/** D15 显式规定吸引半径为 1500cm，可配置但非法数值必须拒绝，不自动夹紧。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="建筑|护盾", meta=(Units="cm"))
	float AttractionRadius = 1500.0f;

	/** 由子系统通知同步的剩余次数；破盾后归零但建筑自身保留。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="建筑|护盾")
	int64 RemainingBlocks = 0;

	/** 计算本次完整激活次数，不修改既有护盾；便于 UI 和测试检查连通倍率。 */
	bool CalculateShieldBlocks(int64& OutBlocks) const;
	virtual void OnBoardUnregistered(ACastleBoard& Board) override;

protected:
	virtual bool ExecuteEffect(const FCastleBuildingTriggerContext& Context) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 先验证显式组件与资源，再连接通知，避免注册出没有可见反馈的护盾。 */
	bool PreparePresentation();
	void HandleShieldStateChanged(UObject* Owner, int64 Blocks);
	/** 注销只关闭本实例，不能清空子系统或其他盾塔的激活状态。 */
	void DeactivateAndUnbind();
	TWeakObjectPtr<UCastleShieldTowerSubsystem> RegisteredShields;
	FDelegateHandle ShieldStateHandle;
};
