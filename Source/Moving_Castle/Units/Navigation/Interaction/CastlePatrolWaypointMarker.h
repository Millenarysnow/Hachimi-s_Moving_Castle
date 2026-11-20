#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastlePatrolWaypointMarker.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/** 仅负责巡逻路点的外观；地面圆圈标出落点，主体和数字沿用模型的悬浮高度。 */
UCLASS(Abstract, Blueprintable)
class MOVING_CASTLE_API ACastlePatrolWaypointMarker : public AActor
{
	GENERATED_BODY()

public:
	ACastlePatrolWaypointMarker();

	static constexpr int32 MaxDisplayedNumber = 99;

	/** 两个面板同步显示 0～99；越界或资产配置错误会明确失败，不替换成别的数字。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|巡逻标记")
	bool SetDisplayedNumber(int32 Number);

	/** 读取当前已显示的数值；失败的更新不会改变这个数值。 */
	int32 GetDisplayedNumber() const { return DisplayedNumber; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	/** 蓝图只保存模型引用，正反面槽位由主体静态网格上的命名 Socket 提供。 */
	UPROPERTY(EditDefaultsOnly, Category="巡逻标记")
	TObjectPtr<UStaticMesh> MarkerBodyMesh;

	/** 下标就是数字值，必须完整配置十个网格；两侧只共享资产，不共享组件。 */
	UPROPERTY(EditDefaultsOnly, Category="巡逻标记")
	TArray<TObjectPtr<UStaticMesh>> DigitMeshes;

	/** 可直接在关卡实例上预览任意号码；运行时用 SetDisplayedNumber 修改。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="巡逻标记", meta=(AllowPrivateAccess="true"))
	int32 DisplayedNumber = 0;

	/** 地面样式固定引用工具包的原始网格和材质；Cook 不依赖示例地图。 */
	UPROPERTY()
	TObjectPtr<UStaticMesh> GroundCircleMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GroundCircleMaterial;

	UPROPERTY(VisibleAnywhere, Category="巡逻标记")
	TObjectPtr<UStaticMeshComponent> GroundCircle;
	UPROPERTY(VisibleAnywhere, Category="巡逻标记")
	TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category="巡逻标记")
	TObjectPtr<UStaticMeshComponent> FrontOnes;
	UPROPERTY(VisibleAnywhere, Category="巡逻标记")
	TObjectPtr<UStaticMeshComponent> FrontTens;
	UPROPERTY(VisibleAnywhere, Category="巡逻标记")
	TObjectPtr<UStaticMeshComponent> BackOnes;
	UPROPERTY(VisibleAnywhere, Category="巡逻标记")
	TObjectPtr<UStaticMeshComponent> BackTens;
};
