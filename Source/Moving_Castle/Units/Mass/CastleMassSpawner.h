#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleMassTypes.h"
#include "CastleMassSpawner.generated.h"

class UMassEntityConfigAsset;

/** 一个 Actor 管理一整批实体；无 Tick，可用于关卡配置、压力场景或接入波次系统。 */
UCLASS()
class MOVING_CASTLE_API ACastleMassSpawner : public AActor
{
	GENERATED_BODY()
public:
	ACastleMassSpawner();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mass")
	TObjectPtr<UMassEntityConfigAsset> Configuration;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mass", meta=(ClampMin="1"))
	int32 Count = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mass", meta=(ClampMin="1"))
	int32 Columns = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mass", meta=(ClampMin="1.0", Units="cm"))
	float Spacing = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mass")
	bool bSpawnOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mass")
	bool bStartMoving = false;

	/** 相对生成器的移动方向；人形按配置速度持续移动，子弹默认沿生成变换的 +X 发射。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mass", meta=(EditCondition="bStartMoving"))
	FVector LocalMovementDirection = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Transient, Category="Mass")
	TArray<FCastleMassUnitHandle> SpawnedUnits;

	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	bool SpawnUnits();

	UFUNCTION(BlueprintCallable, Category="移动城堡|Mass")
	void DespawnUnits();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
