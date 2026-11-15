#pragma once

#include "CoreMinimal.h"
#include "CastleTowerAttackEffectComponent.h"
#include "CastleCannonTowerAttackEffectComponent.generated.h"

class ACastleBoard;
class ACastleBuildingBase;

/** 炮塔每次进入结算一发全额炮击，弹药库只联动一跳，伤害在触发时读取并逐发保存。 */
UCLASS(ClassGroup=(Castle), BlueprintType)
class MOVING_CASTLE_API UCastleCannonTowerAttackEffectComponent : public UCastleTowerAttackEffectComponent
{
	GENERATED_BODY()

public:
	/** 查询当前等级收录的同数字弹药库；按建筑实例去重且不含炮塔自身。 */
	bool GetAmmunitionBuildings(TArray<ACastleBuildingBase*>& OutBuildings) const;
	/** 只读观察待射伤害，不能用发射时的 NPC 或当前弹药库重新计算历史炮击。 */
	const TArray<float>& GetPendingShotDamages() const { return PendingShotDamages; }

	virtual void OnBoardRegistered(ACastleBoard& Board) override;
	virtual void OnBoardUnregistered(ACastleBoard& Board) override;

protected:
	/** 只读 NPC 值状态并入队；即使 Mass 正在处理，也绝不在此生成投射物。 */
	virtual bool ExecuteEffect(const FCastleBuildingTriggerContext& Context) override;
	/** 基类 Tick 已完成冷却校验，此处只消费队首保存的单发伤害。 */
	virtual bool FireOneShot(const UCastleTowerAttackDataAsset& DataAsset, const FCastleTowerAttackLevelData& LevelData) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 自身触发才由棋盘广播；库联动直接调用组件，不再次走建筑广播入口。 */
	void HandleBoardBuildingTriggered(ACastleBuildingBase* Source, const FCastleBuildingTriggerContext& Context);
	/** 委托保存弱引用，注销与销毁时解除，不让被回收的炮塔继续监听旧棋盘。 */
	TWeakObjectPtr<ACastleBoard> RegisteredBoard;
	FDelegateHandle BoardTriggerHandle;
	UPROPERTY(Transient)
	TArray<float> PendingShotDamages;
};
