#pragma once

#include "CoreMinimal.h"
#include "CastleNavigationSubsystem.h"
#include "GameFramework/Actor.h"
#include "CastleNavigationDemo.generated.h"

class UCameraComponent;
class UStaticMesh;
class UBuildingMergeComponent;

/** 独立验收场景：真实碰撞楼梯 + 两段飞入墙体合并，自动显示共用路线及刷新计数。 */
UCLASS()
class MOVING_CASTLE_API ACastleNavigationDemo : public AActor
{
	GENERATED_BODY()
public:
	ACastleNavigationDemo();
	virtual void Tick(float DeltaSeconds) override;

	/** 两段初始墙体归位后触发真实合并；自动演示在路线出现 4 秒后调用。 */
	UFUNCTION(BlueprintCallable, Category="导航演示")
	bool StartMerge();

	UPROPERTY(VisibleAnywhere, Category="导航演示")
	TObjectPtr<UCameraComponent> DemoCamera;

	/** 0=飞入/初次构建，1=初始路线，2=合并中，3=新路线；-1 为明确失败。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="导航演示")
	int32 Phase = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="导航演示")
	FCastleSharedNavigationPath InitialPath;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="导航演示")
	FCastleSharedNavigationPath MergedPath;

protected:
	virtual void BeginPlay() override;
private:
	UPROPERTY()
	TObjectPtr<UStaticMesh> Cube;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> Walls;
	UPROPERTY(Transient)
	TObjectPtr<UBuildingMergeComponent> Merge;
	double ReadyTime = 0.0;
	bool bAutomation = false;
	AActor* SpawnWall(const TCHAR* Label, int32 FirstPiece, int32 NumPieces, double YOffset);
	void StopWithError(const FString& Error);
};
