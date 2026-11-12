#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleMassTypes.h"
#include "Projectiles/CastleMassHomingProjectileTypes.h"
#include "CastleMassUnitSubsystem.generated.h"

class UMassEntityConfigAsset;
struct FMassEntityManager;

/** 蓝图/建筑系统与 Mass 的批量接口。写操作延迟到安全边界，返回 true 表示命令被接收。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassUnitSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	/** 游戏线程且 Mass 未处理时，查询三维半径内最近的存活敌人；正常无目标返回 false。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool FindNearestEnemy(const FVector& Origin, float Radius, FCastleMassUnitHandle& OutEnemy) const;

	/** 整批先校验后生成追踪子弹；配置必须具有追踪 Trait，每发目标和伤害必须有效。 */
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
