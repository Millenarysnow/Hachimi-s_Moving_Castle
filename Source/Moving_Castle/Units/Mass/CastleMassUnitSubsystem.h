#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleMassTypes.h"
#include "Projectiles/CastleMassHomingProjectileTypes.h"
#include "Projectiles/CastleMassEnemyProjectileTypes.h"
#include "CastleMassUnitSubsystem.generated.h"

class UMassEntityConfigAsset;
struct FMassEntityManager;

/** 蓝图/建筑系统与 Mass 的批量接口。批量命令延迟执行，农田增益与即时伤害同步修改值状态。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassUnitSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	/**
	 * 战斗清场专用：统计本世界所有 EnemyTag 实体，包括已死但尚未回收者及分裂子体。
	 * 只能在游戏线程且 Mass 未处理时查询；失败输出 Error，不以零冒充成功结果。
	 */
	bool CountEnemyEntities(int32& OutCount) const;

	/** 游戏线程向本世界存活 NPC 同步累加有限正数；允许游戏线程 Processor，失败写 Error。 */
	bool AddNPCFarmBuff(const FCastleMassUnitHandle& NPC, float DeltaBonus);
	/** 游戏线程同步读取存活 NPC 的农田增益；允许游戏线程 Processor，失败输出零并写 Error。 */
	bool GetNPCFarmBuff(const FCastleMassUnitHandle& NPC, float& OutBonusNumber) const;
	/** 游戏线程即时伤害，允许 Processor 调用；须在并行查询同步结束后或拥有生命写权限时使用。 */
	bool ApplyImmediateDamageToEnemy(const FCastleMassUnitHandle& Enemy, float Damage);
	/** 只在游戏线程安全边界生成敌方弹；Processor 用延迟 Create 命令调用，逐发锁定护盾或主塔。 */
	bool SpawnEnemyProjectiles(UMassEntityConfigAsset* Configuration, const TArray<FCastleMassEnemyProjectileSpawn>& Spawns,
		TArray<FCastleMassUnitHandle>& OutProjectiles, FText& OutFailureReason);

	/**
	 * 在 Origin 周围 Radius（三维，cm）内查询最近的存活敌人（EnemyTag 且 Health>0）。
	 * 只能在游戏线程且 Mass 未处理时调用；非法调用或输入写 Error，正常无目标返回 false 且不报错。
	 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool FindNearestEnemy(const FVector& Origin, float Radius, FCastleMassUnitHandle& OutEnemy) const;

	/**
	 * 游戏线程且 Mass 未处理时，整批先校验后生成；配置必须具有 UCastleMassHomingProjectileTrait。
	 * 每发 Target 必须是当前世界存活敌人，Damage 必须为有限非负数；任一失败均清空输出、写失败原因及 Error。
	 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool SpawnHomingProjectiles(UMassEntityConfigAsset* Configuration,
		const TArray<FCastleMassHomingProjectileSpawn>& Spawns, TArray<FCastleMassUnitHandle>& OutProjectiles, FText& OutFailureReason);

	/** 在 Mass Processor 外调用；整批先校验再生成。子弹沿各变换的 +X 方向发射。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool SpawnUnits(UMassEntityConfigAsset* Configuration, const TArray<FTransform>& SpawnTransforms,
		TArray<FCastleMassUnitHandle>& OutUnits, FText& OutFailureReason);

	/** 三维直达目标接口；不自动查询 NavMesh 或建筑路网，仅作用于敌人/NPC。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool MoveUnitsTo(const TArray<FCastleMassUnitHandle>& Units, FVector Destination);

	/** 让一批 NPC 消费世界唯一的完整循环路线；不发起寻路。单位应已在路线上，推荐沿路线生成。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool FollowSharedPatrolRoute(const TArray<FCastleMassUnitHandle>& Units, FText& OutFailureReason);

	/** 人形设置持续期望速度；子弹设置实际弹道速度。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool SetUnitsVelocity(const TArray<FCastleMassUnitHandle>& Units, FVector Velocity);

	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool StopUnits(const TArray<FCastleMassUnitHandle>& Units);

	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool DamageUnits(const TArray<FCastleMassUnitHandle>& Units, float Damage);

	/** 标记死亡，由生命周期 Processor 批量销毁实体及其显示实例。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool DespawnUnits(const TArray<FCastleMassUnitHandle>& Units);

	UFUNCTION(BlueprintPure, Category="移动城堡|Mass")
	bool IsUnitValid(const FCastleMassUnitHandle& Unit) const;

	/** 供选中/交互查询；Mass 正在并行处理时返回 false，避免读取正在被写入的数据。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool GetUnitSnapshot(const FCastleMassUnitHandle& Unit, FCastleMassUnitSnapshot& OutSnapshot) const;

	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	FMassEntityManager* GetManager() const;
	bool CollectHandles(const TArray<FCastleMassUnitHandle>& Units, TArray<FMassEntityHandle>& OutHandles) const;

	/** 配置只在首次使用时验证，热路径不重建模板或遍历 Trait。 */
	UPROPERTY(Transient)
	TSet<TObjectPtr<UMassEntityConfigAsset>> ValidatedConfigurations;
};
