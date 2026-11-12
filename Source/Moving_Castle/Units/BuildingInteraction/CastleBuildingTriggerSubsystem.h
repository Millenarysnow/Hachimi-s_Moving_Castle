#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "Units/BuildingInteraction/CastleBuildingTriggerPath.h"
#include "CastleBuildingTriggerSubsystem.generated.h"

class ACastleBuildingBase;
struct FCastleSharedNavigationPath;

/** 工作线程到建筑 UObject 的值类型消息；使用登记代号，绝不保存跨线程 Actor 指针。 */
struct FCastleBuildingEntryEvent
{
	FMassEntityHandle NPC;
	uint64 BuildingId = 0;
	FVector Location = FVector::ZeroVector;
};

/** 每个游戏世界一份的建筑登记、路线编译和事件派发边界，平时不 Tick。 */
UCLASS()
class MOVING_CASTLE_API UCastleBuildingTriggerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 棋盘完成占用校验后登记逻辑盒子；同一实例重复登记属于错误。 */
	bool RegisterBuilding(ACastleBuildingBase* Building, const FTransform& LocalToWorld,
		TArray<FBox>&& LocalBoxes, FText& OutFailure);

	/** 注销与销毁清理允许重复调用，但登记代号永不复用。 */
	void UnregisterBuilding(ACastleBuildingBase* Building);

	/** 放置完成开关改变时仅标记快照过期，不立刻触发建筑或重新寻路。 */
	void NotifyBuildingStateChanged(ACastleBuildingBase* Building);

	/** 游戏线程按两个版本缓存编译结果，返回对象可供整批 Mass 工作线程只读。 */
	TSharedPtr<const FCastleBuildingTriggerPath, ESPMode::ThreadSafe> AcquireTriggerPath(
		const TSharedPtr<const FCastleSharedNavigationPath, ESPMode::ThreadSafe>& Path);

	/** 并行检测结束后在游戏线程执行；校验快照与登记版本，再按每个 NPC 的行走顺序派发。 */
	void DispatchEntries(const FCastleBuildingTriggerPath& Path, TConstArrayView<FCastleBuildingEntryEvent> Events);

	/** 缓存命中时不递增，供验证预处理成本没有随 NPC 数量增长。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑|进入触发")
	int32 CompilationCount = 0;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

private:
	/** 弱实例身份与不含 UObject 的区域分开，旧快照不能延长建筑生命周期。 */
	struct FRegistration
	{
		TWeakObjectPtr<ACastleBuildingBase> Building;
		FCastleBuildingTriggerRegion Region;
	};
	TMap<uint64, FRegistration> Buildings;
	TMap<TWeakObjectPtr<ACastleBuildingBase>, uint64> IdByBuilding;
	uint64 NextBuildingId = 1;
	uint64 RegistryRevision = 1;
	TSharedPtr<const FCastleBuildingTriggerPath, ESPMode::ThreadSafe> CachedPath;
};
