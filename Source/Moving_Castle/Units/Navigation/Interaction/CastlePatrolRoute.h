#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastlePatrolRoute.generated.h"

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="共享巡逻")
	bool bShowRoute = true;

	/** 统一接收世界坐标，编辑输入和外部 UI 都走此入口，没有蓝图业务逻辑。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|NPC导航")
	bool SetWorldWaypoints(const TArray<FVector>& Waypoints, FText& OutFailureReason);

	virtual void Tick(float DeltaSeconds) override;
	bool IsEditing() const { return bEditing; }

	/** 开战只取消草稿和输入占用，不提交、不清除正式路线。 */
	void CancelEditingDraft();

	/** 与视口放点共用：建筑命中格取本层底面锚点；棋盘内的其他顶面取格心 XY 并保留命中高度。 */
	static bool TryResolveWaypointFromRay(UWorld* World, const FVector& RayStart, const FVector& RayEnd,
		FVector& OutPoint, FText& OutFailureReason);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
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
