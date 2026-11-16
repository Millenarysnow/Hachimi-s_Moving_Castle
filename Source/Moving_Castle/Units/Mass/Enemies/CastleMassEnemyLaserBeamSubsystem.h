#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Mass/EntityHandle.h"
#include "CastleMassEnemyLaserBeamSubsystem.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** 一帧内一条活跃激光的纯值渲染输入，不创建逐单位或逐光束 Actor。 */
struct FCastleMassEnemyLaserBeam
{
	FMassEntityHandle Entity;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	float Thickness = 0.0f;
	UStaticMesh* Mesh = nullptr;
	UMaterialInterface* Material = nullptr;
};

/** 世界级瞬态光束批次；每帧以当前存活攻击态完整替换实例集合。 */
UCLASS()
class MOVING_CASTLE_API UCastleMassEnemyLaserBeamSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	bool UpdateBeams(const TArray<FCastleMassEnemyLaserBeam>& Beams, FString& OutFailureReason);
	/** 删除 Observer 在实体被回收时移除对应光束，不能等下一次 Movement。 */
	void RemoveBeam(FMassEntityHandle Entity);
	int32 GetActiveBeamCount() const;
	virtual void Deinitialize() override;
private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> BeamComponents;
	TArray<FCastleMassEnemyLaserBeam> ActiveBeams;
};
