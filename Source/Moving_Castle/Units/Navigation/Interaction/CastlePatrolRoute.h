#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastlePatrolRoute.generated.h"

class ACastlePatrolWaypointMarker;

/** 路径编辑模式开关变化通知；HUD 与建造栏据此在建造页和路径编辑页之间即时切换。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCastlePatrolEditingChanged, bool);

/** 世界唯一的玩家巡逻路径编辑入口；只管理路径点和反馈，实际整条寻路交给导航子系统。 */
UCLASS()
class MOVING_CASTLE_API ACastlePatrolRoute : public AActor
{
	GENERATED_BODY()
public:
	ACastlePatrolRoute();

	/** 编辑器可直接增加数组元素并拖动三维手柄；坐标相对本 Actor，顺序就是巡逻顺序。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="共享巡逻", meta=(MakeEditWidget=true))
	TArray<FVector> LocalWaypoints;

	/** 允许本地玩家用 P 进入编辑，左键放点，Backspace 撤回，Enter 提交，Delete 清空。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="共享巡逻")
	bool bAllowPlayerEditing = true;

	/** 只控制路点圆圈、悬浮编号和编辑状态提示；正式巡逻线由当前世界的可视化子系统控制。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="共享巡逻")
	bool bShowRoute = true;

	/** 统一接收世界坐标，编辑输入和外部 UI 都走此入口，没有蓝图业务逻辑。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|NPC导航")
	bool SetWorldWaypoints(const TArray<FVector>& Waypoints, FText& OutFailureReason);

	virtual void Tick(float DeltaSeconds) override;
	bool IsEditing() const { return bEditing; }

	/** 供路径编辑 UI 按钮与 Backspace 快捷键共用同一撤回入口，不重复维护草稿逻辑。 */
	void UndoDraftPoint();

	/** 供路径编辑 UI 删除按钮与 Delete 快捷键共用同一清空入口。 */
	void ClearPatrolRoute();

	/** 路径编辑进入或退出时广播，界面只订阅唯一编辑状态。 */
	FCastlePatrolEditingChanged OnEditingStateChanged;

	/** 开战只取消草稿和输入占用，不提交、不清除正式路线。 */
	void CancelEditingDraft();

	/** 与视口放点共用：功能楼板按命中的楼层取格心；选中建筑美术时取该占格的楼内底面。 */
	static bool TryResolveWaypointFromRay(UWorld* World, const FVector& RayStart, const FVector& RayEnd,
		FVector& OutPoint, FText& OutFailureReason);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 正式蓝图类随入口一起加载，既有地图不需要再布置标记 Actor。 */
	UPROPERTY()
	TSubclassOf<ACastlePatrolWaypointMarker> WaypointMarkerClass;

	/** 只持有本入口生成的临时实例。草稿和正式点在同一显示列表中切换，不同时叠两套标记。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACastlePatrolWaypointMarker>> WaypointMarkers;

	/** 只在显示点列变化时同步；同一份无效配置不会在 Tick 中重试并重复报错。 */
	TArray<FVector> LastMarkerPoints;
	void RefreshWaypointMarkers(const TArray<FVector>& Points);
	void DestroyWaypointMarkers();

	/** 草稿只在 Enter 时整批提交，拖动/连续放点不会逐帧或逐次计算半条路线。 */
	TArray<FVector> DraftWaypoints;
	bool bEditing = false;
	bool bOwnsRoute = false;
	void ToggleEditing();
	void AddPointAtViewCenter();
	void UndoPoint();
	void ApplyDraft();
	void ClearRoute();
	void SetEditing(bool bNewEditing);
	bool CanEdit() const;
	void ShowMessage(const FString& Message, bool bSuccess) const;
};
