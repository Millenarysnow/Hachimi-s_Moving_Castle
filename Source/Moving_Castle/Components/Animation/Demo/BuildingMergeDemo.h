#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildingMergeDemo.generated.h"

class ACastleBoard;
class ACastleBuildingBase;
class UCameraComponent;
class UCastleBuildingDefinition;

/**
 * 合并效果验收用原生 Actor：使用正式棋盘、正式建筑定义及正式合并流程自动循环。
 * 独立关卡无需蓝图逻辑、背包购买或手工对准格子，点击 Play 就能观察真实拆件建筑。
 */
UCLASS(meta=(DisplayName="Building Merge Demo"))
class MOVING_CASTLE_API ABuildingMergeDemo : public AActor
{
	GENERATED_BODY()

public:
	ABuildingMergeDemo();

	/** 必须配置有一级和二级的正式建筑定义；未配置时明确报错并停止演示。 */
	UPROPERTY(EditAnywhere, Category="Merge Demo")
	TObjectPtr<UCastleBuildingDefinition> BuildingDefinition;

	/** 合并前展示的秒数；每轮完成后也保留两秒查看最终模型。 */
	UPROPERTY(EditAnywhere, Category="Merge Demo", meta=(ClampMin="0.1", Units="s"))
	double StartDelay = 2.0;

	/** 完成后自动清理、重新放置并重复合并，便于连续观察。 */
	UPROPERTY(EditAnywhere, Category="Merge Demo")
	bool bLoop = true;

	/** 两组相邻的不同数字建筑同时改变，共用同一传播原点；关闭后只演示单组升级。 */
	UPROPERTY(EditAnywhere, Category="Merge Demo")
	bool bShowAdjacentChanges = true;

	/** 相机属于演示 Actor，Play 时自动取景；停止运行后不改变关卡编辑视口。 */
	UPROPERTY(VisibleAnywhere, Category="Merge Demo")
	TObjectPtr<UCameraComponent> DemoCamera;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EDemoPhase : uint8
	{
		WaitingForInitialFlights,
		ShowingInitialBuildings,
		WaitingForMergeFlights,
		ShowingMergedBuildings
	};

	UPROPERTY(Transient)
	TObjectPtr<ACastleBoard> DemoBoard;

	UPROPERTY(Transient)
	TObjectPtr<ACastleBuildingBase> CurrentBuilding;

	UPROPERTY(Transient)
	TObjectPtr<ACastleBuildingBase> AdjacentBuilding;

	double PhaseTime = 0.0;
	EDemoPhase Phase = EDemoPhase::WaitingForInitialFlights;
	bool bCaptureFrames = false;
	int32 CaptureFrameIndex = 0;

	bool PlaceFirstBuilding();
	bool PlaceMergeCards();
	bool AreInitialFlightsComplete() const;
	bool AreMergedBuildingsReady(double& OutAnimationDuration) const;
	void StopWithError(const FString& Message);
};
