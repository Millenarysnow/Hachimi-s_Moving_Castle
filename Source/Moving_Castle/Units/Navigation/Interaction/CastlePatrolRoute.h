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
	/** 教程关卡允许使用同一个 P 键进入与提交，正式局继续使用既有按键。 */
	UPROPERTY(EditAnywhere, Category="共享巡逻")
	bool bSubmitDraftOnToggle = false;

	/** 教学导演明确启用双门寻路，并在讲解弹窗期间保留正在编辑的草稿。 */
	void ConfigureTutorialRoute() { bTutorialRoute = true; }
	bool IsTutorialRoute() const { return bTutorialRoute; }

	/** 只控制路点圆圈、悬浮编号和编辑状态提示；正式巡逻线由当前世界的可视化子系统控制。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="共享巡逻")
	bool bShowRoute = true;

	/** 统一接收世界坐标，编辑输入和外部 UI 都走此入口，没有蓝图业务逻辑。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|NPC导航")
	bool SetWorldWaypoints(const TArray<FVector>& Waypoints, FText& OutFailureReason);

	/** 改变玩家草稿的放点上限，已经提交的路线保持原样。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|NPC导航")
	bool SetMaxPlayerWaypoints(int32 NewLimit);

	/** 外部升级可以在现有上限上增加点数，同样检查结果是否合法。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|NPC导航")
	bool AddMaxPlayerWaypoints(int32 Delta);

	UFUNCTION(BlueprintPure, Category="移动城堡|NPC导航")
	int32 GetMaxPlayerWaypoints() const { return MaxPlayerWaypoints; }

	/** 只计玩家草稿，不包括双门自动附加的两个端点。 */
	int32 GetUsedWaypointCount() const { return DraftWaypoints.Num(); }

	/** 上限减去已用草稿点；预设或下调上限超量时保留实际差值，方便界面明确反馈。 */
	int32 GetRemainingWaypointCount() const { return MaxPlayerWaypoints - GetUsedWaypointCount(); }

	virtual void Tick(float DeltaSeconds) override;
	bool IsEditing() const { return bEditing; }

	/** 控制器的主交互入口只在路径编辑独占世界点击时调用。 */
	void AddPointAtAim();

	/** 供路径编辑 UI 按钮与 Backspace 快捷键共用同一撤回入口，不重复维护草稿逻辑。 */
	void UndoDraftPoint();

	/** 供路径编辑 UI 删除按钮与 Delete 快捷键共用同一清空入口。 */
	void ClearPatrolRoute();

	/** 路径编辑进入或退出时广播，界面只订阅唯一编辑状态。 */
	FCastlePatrolEditingChanged OnEditingStateChanged;

	/** 草稿或上限发生变化时发送，监听者读取本次提交后的已用与剩余点数。 */
	FSimpleMulticastDelegate OnWaypointDraftChanged;

	/** 开战只取消草稿和输入占用，不提交、不清除正式路线。 */
	void CancelEditingDraft();

	/** 与视口放点共用：功能楼板按命中的楼层取格心；选中建筑美术时取该占格的楼内底面。 */
	static bool TryResolveWaypointFromRay(UWorld* World, const FVector& RayStart, const FVector& RayEnd,
		FVector& OutPoint, FText& OutFailureReason);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 只约束玩家放点，关卡作者提交的预设路线继续走原导航校验。 */
	UPROPERTY(EditAnywhere, Category="共享巡逻")
	int32 MaxPlayerWaypoints = 2;

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
	bool bTutorialRoute = false;
	void ToggleEditing();
	bool CheckDraftWaypointCapacity(FText& OutReason) const;
	bool TryAppendDraftWaypoint(FVector Point, FText& OutReason);
	void UndoPoint();
	void ApplyDraft();
	void ClearRoute();
	void SetEditing(bool bNewEditing);
	bool CanEdit() const;
	void ShowMessage(const FString& Message, bool bSuccess) const;

	friend class FCastleMainTowerWaypointLimitTest;
	/** 教学 PIE 验收将确定的格心送进实际路径草稿，用 P 入口提交。 */
	friend class FCastleTutorialPIEDriver;
};
