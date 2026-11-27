#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleMassTypes.h"
#include "CastleShieldTowerSubsystem.generated.h"

/** 护盾次数变更通知；零表示本次激活已经结束，建筑组件据此更新显示。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCastleShieldStateChanged, UObject*, int64);

/** 世界级激活护盾契约。只保存建筑弱引用，敌人不依赖具体盾塔组件或表现资产。 */
UCLASS()
class MOVING_CASTLE_API UCastleShieldTowerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	/** 游戏线程注册/更新激活态；同一 OwnerKey 在本次激活期间保留 ID，错误返回零。 */
	uint64 UpsertActiveShield(UObject* OwnerKey, const FVector& WorldLocation, float AttractionRadius,
		int64 RemainingBlocks, float ReflectDamage);
	/** 幂等关闭；重新激活会得到新 ID，旧的在途弹不会误伤新护盾。 */
	void DeactivateShield(UObject* OwnerKey);
	bool GetShieldRemainingBlocks(const UObject* OwnerKey, int64& OutRemainingBlocks) const;
	/** 近战同时检查吸引圈与查询距离；远程只检查查询距离。无候选是正常状态。 */
	bool FindNearestActiveShield(const FVector& QueryLocation, float MaxDistance, bool bRequireWithinAttractionRadius,
		uint64& OutShieldId, FVector& OutTargetLocation) const;
	/** 扣一次抵挡后立即反伤存活攻击者。破盾和重入通知均不能复用本次激活 ID。 */
	bool ApplyAttackToShield(uint64 ShieldId, const FCastleMassUnitHandle& Attacker, int64& OutRemainingBlocksAfterHit);
	/** 专供已锁定的敌方投射物追踪位置；不进行重新索敌。 */
	bool GetActiveShieldLocation(uint64 ShieldId, FVector& OutLocation) const;

	/** 整面耗尽剩余次数最多的已展开护盾，成功时返回耗尽前的次数并通知破盾表现。 */
	bool TryConsumeStrongestActiveShield(int64& OutConsumedBlocks);

	FCastleShieldStateChanged OnShieldStateChanged;

private:
	/** 注册表不维持建筑存活；失效 Owner 不属于激活护盾查询集合。 */
	struct FActiveShield
	{
		TWeakObjectPtr<UObject> Owner;
		FVector Location = FVector::ZeroVector;
		float AttractionRadius = 0.0f;
		int64 RemainingBlocks = 0;
		float ReflectDamage = 0.0f;
	};
	TMap<uint64, FActiveShield> ActiveShields;
	uint64 NextShieldId = 1;
};
